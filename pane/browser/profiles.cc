// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profiles.h"

#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/pref_service_builder_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/language/core/browser/language_prefs.h"
#include "components/language/core/browser/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/pane_storage_service.h"
#include "content/public/browser/storage_partition.h"
#include "gpu/config/gpu_info.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/fingerprints.h"
#include "pane/browser/proto_databases.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/handover.h"
#include "pane/model/engine_fingerprint.h"
#include "pane/model/profile.h"
#include "services/network/enterprise/encryption/pane_cache_hook.h"
#include "services/network/public/mojom/network_service.mojom.h"

namespace pane {

namespace {

// Calls back once Chrome has destroyed a profile: true; false past the
// timeout, or when the profile manager goes first. Deletes itself.
class ProfileDestructionWaiter : public ProfileObserver,
                                 public ProfileManagerObserver {
 public:
  ProfileDestructionWaiter(Profile* profile,
                           base::TimeDelta timeout,
                           base::OnceCallback<void(bool)> done)
      : done_(std::move(done)) {
    profile_observation_.Observe(profile);
    manager_observation_.Observe(g_browser_process->profile_manager());
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ProfileDestructionWaiter::Finish,
                       weak_factory_.GetWeakPtr(), false),
        timeout);
  }

  void OnProfileWillBeDestroyed(Profile* profile) override { Finish(true); }
  void OnProfileManagerDestroying() override { Finish(false); }

 private:
  void Finish(bool destroyed) {
    profile_observation_.Reset();
    manager_observation_.Reset();
    std::move(done_).Run(destroyed);
    delete this;
  }

  base::OnceCallback<void(bool)> done_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      manager_observation_{this};
  base::WeakPtrFactory<ProfileDestructionWaiter> weak_factory_{this};
};

// Calls back once Chrome has marked a profile for permanent deletion: its
// browsing data is being removed, and its directory goes when the profile is
// destroyed or, at the latest, at the next start. Deletes itself.
class ProfileDeletionWaiter : public ProfileManagerObserver {
 public:
  ProfileDeletionWaiter(base::FilePath path,
                        base::OnceCallback<void(bool)> done)
      : path_(std::move(path)), done_(std::move(done)) {
    observation_.Observe(g_browser_process->profile_manager());
  }

  void OnProfileMarkedForPermanentDeletion(Profile* profile) override {
    if (profile->GetPath() == path_) {
      Finish(true);
    }
  }

  void OnProfileManagerDestroying() override { Finish(false); }

 private:
  void Finish(bool deleted) {
    observation_.Reset();
    std::move(done_).Run(deleted);
    delete this;
  }

  const base::FilePath path_;
  base::OnceCallback<void(bool)> done_;
  base::ScopedObservation<ProfileManager, ProfileManagerObserver> observation_{
      this};
};

// What each process answered of its keys.
struct KeysLeft : base::RefCountedThreadSafe<KeysLeft> {
  uint32_t network = 0;
  uint32_t storage = 0;

 private:
  friend class base::RefCountedThreadSafe<KeysLeft>;
  ~KeysLeft() = default;
};

// What Chrome holds of a profile's stores goes with the profile, which a
// lock's detach waits for (ADR 0116). Made with every profile; its Shutdown
// comes with the profile's other services', the last of ~ProfileImpl's steps
// before its storage partitions go, so a shared proto database opened at any
// time before, however late, is covered.
class ProfileStores : public KeyedService {
 public:
  explicit ProfileStores(Profile* profile) : profile_(profile) {}

  void Shutdown() override {
    LetSharedProtoDatabaseGo(
        *profile_->GetDefaultStoragePartition()->GetProtoDatabaseProvider());
  }

 private:
  const raw_ptr<Profile> profile_;
};

class ProfileStoresFactory : public ProfileKeyedServiceFactory {
 public:
  static ProfileStoresFactory* GetInstance() {
    static base::NoDestructor<ProfileStoresFactory> instance;
    return instance.get();
  }

 private:
  friend base::NoDestructor<ProfileStoresFactory>;

  ProfileStoresFactory() : ProfileKeyedServiceFactory("PaneProfileStores") {}
  ~ProfileStoresFactory() override = default;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    return std::make_unique<ProfileStores>(
        Profile::FromBrowserContext(context));
  }

  bool ServiceIsCreatedWithBrowserContext() const override { return true; }
};

// The profile's folder, and its README. Chrome writes the README as it creates
// a profile that has none (CreateProfileDirectory, profile_impl.cc), in a
// BEST_EFFORT task, which a loaded Mac held past a lock that had closed the
// profile and dropped its keys: the encryption's check of a profile's file
// with no key then ended Pane (ADR 0116). Written here, under the key
// OpenProfile has just registered and before the profile loads, so that Chrome
// posts none.
bool MakeProfileDirectory(const base::FilePath& path) {
  if (!base::CreateDirectory(path)) {
    return false;
  }
  if (!base::PathExists(path.Append(chrome::kReadmeFilename))) {
    CreateProfileReadme(path);
  }
  return true;
}

}  // namespace

void BuildProfileServices() {
  ProfileStoresFactory::GetInstance();
}

void RegisterEngineKey(const base::FilePath& user_data_dir,
                       const std::string& identifier,
                       base::span<const uint8_t> key) {
  encryption::InstallHooks();
  network::enterprise_encryption::InstallPaneCacheHook();

  std::vector<base::FilePath> roots = {user_data_dir,
                                       user_data_dir.AppendASCII("Volume")};

  // Their resolved forms: /var is /private/var, and the caches' folder links
  // into the volume too. A realpath, once per open.
  const base::FilePath resolved = base::MakeAbsoluteFilePath(user_data_dir);

  if (!resolved.empty()) {
    roots.push_back(resolved);
    roots.push_back(resolved.AppendASCII("Volume"));
  }

  base::FilePath caches;
  chrome::GetUserCacheDirectory(
      user_data_dir.Append(base::FilePath::FromUTF8Unsafe(identifier)),
      &caches);

  if (!caches.empty() && caches.DirName() != user_data_dir) {
    roots.push_back(caches.DirName());
  }

  encryption::AddRoots(roots);
  encryption::RegisterKey(identifier, key.first<encryption::kKeySize>());
}

void OpenProfile(const std::string& identifier,
                 base::span<const uint8_t> engine_key,
                 base::OnceCallback<void(Profile*)> done) {
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!manager || !model::IsProfileFolderName(identifier) ||
      engine_key.size() != encryption::kKeySize) {
    std::move(done).Run(nullptr);
    return;
  }

  const base::FilePath path = manager->user_data_dir().Append(
      base::FilePath::FromUTF8Unsafe(identifier));

  // Before anything of the profile opens, in this process or another.
  RegisterEngineKey(manager->user_data_dir(), identifier, engine_key);

  if (manager->GetProfileByPath(path)) {
    manager->CreateProfileAsync(path, std::move(done));
    return;
  }

  // Chrome loads a new profile from a directory it does not check it could
  // make: without one, the profile's preferences fail to load, the failure
  // is never reported (a segregated pref store waits for both of its files),
  // and the profile never finishes loading. The directory is made first,
  // off the main thread, and a profile that cannot have one fails.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&MakeProfileDirectory, path),
      base::BindOnce(
          [](base::FilePath path, base::OnceCallback<void(Profile*)> done,
             bool exists) {
            ProfileManager* manager = g_browser_process->profile_manager();
            if (!exists || !manager) {
              std::move(done).Run(nullptr);
              return;
            }
            manager->CreateProfileAsync(path, std::move(done));
          },
          path, std::move(done)));
}

void ApplyProfileDefaults(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  // Downloads go straight to the download directory, never to a dialog.
  prefs->SetBoolean(prefs::kPromptForDownload, false);
  // Device choosers stay closed, as in the Electron app: a page's
  // requestDevice() is declined at once, with no UI (ADR 0022).
  HostContentSettingsMap* settings =
      HostContentSettingsMapFactory::GetForProfile(profile);
  for (const ContentSettingsType guard :
       {ContentSettingsType::USB_GUARD, ContentSettingsType::HID_GUARD,
        ContentSettingsType::SERIAL_GUARD,
        ContentSettingsType::BLUETOOTH_GUARD}) {
    settings->SetDefaultContentSetting(guard, CONTENT_SETTING_BLOCK);
  }
  // Chrome's password manager and autofill stay off: Pane relies on
  // extension password managers, so their bubbles never appear.
  prefs->SetBoolean(password_manager::prefs::kCredentialsEnableService, false);
  prefs->SetBoolean(password_manager::prefs::kCredentialsEnableAutosignin,
                    false);
  prefs->SetBoolean(autofill::prefs::kAutofillProfileEnabled, false);
  prefs->SetBoolean(autofill::prefs::kAutofillCreditCardEnabled, false);
  // No browser sign-in (its account list goes to accounts.google.com with
  // the profile's cookies) and no Translate (its ranker model downloads
  // from gstatic): Pane has neither, as the Electron app had neither.
  // Signing in to Google's websites is unaffected.
  prefs->SetBoolean(prefs::kSigninAllowed, false);
  prefs->SetBoolean(translate::prefs::kOfferTranslateEnabled, false);
  // No incognito: a profile is apart already (D3, Paper 10.2). Chrome's
  // extensions page then hides Allow in Incognito, as it does wherever
  // incognito is off (detail_view.ts), and Chrome's ways into an incognito
  // window (an extension's windows.create, a link's menu item) stay shut. A
  // pref the profile holds, not a policy's: nothing reads as managed.
  IncognitoModePrefs::SetAvailability(
      prefs, policy::IncognitoModeAvailability::kDisabled);
}

bool SetProfileFingerprint(
    const std::string& identifier,
    const std::optional<model::Fingerprint>& fingerprint) {
  PANE_TRACE("call", "PaneBrowserApplication.setFingerprint",
             base::DictValue()
                 .Set("identifier", identifier)
                 .Set("set", fingerprint.has_value()));
  std::string json;
  if (fingerprint) {
    json = model::EngineFingerprint::For(*fingerprint, identifier,
                                         embedder_support::GetUserAgent(),
                                         version_info::GetVersionNumber())
               .ToJson();
  }
  std::string error;
  return Fingerprints::Get().Set(identifier, json, &error);
}

void ApplyFingerprintLanguages(Profile* profile) {
  // Chrome derives the Accept-Language list from the user's selected
  // languages (LanguagePrefs), so both are set.
  PrefService* prefs = profile->GetPrefs();
  const Fingerprint* fingerprint = Fingerprints::Get().For(profile);
  if (fingerprint && !fingerprint->languages.empty()) {
    const std::string languages = base::JoinString(fingerprint->languages, ",");
    prefs->SetString(language::prefs::kSelectedLanguages, languages);
    prefs->SetString(language::prefs::kAcceptLanguages, languages);
  } else {
    language::ResetLanguagePrefs(prefs);
  }
  PANE_TRACE("engine", "PaneBrowserContext.applyFingerprint",
             base::DictValue()
                 .Set("identifier", profile->GetBaseName().AsUTF8Unsafe())
                 .Set("acceptLanguages",
                      prefs->GetString(language::prefs::kAcceptLanguages)));
}

// ANGLE names its renderer "ANGLE (vendor, device, version)" and gives WebGL
// "Unspecified Version" instead of the OS version Chrome's GPU info has
// (DisplayMtl::getVersionString), so the version is swapped the same way.
std::optional<model::Fingerprint::WebGL> HostWebGL() {
  const gpu::GPUInfo& info =
      content::GpuDataManager::GetInstance()->GetGPUInfo();
  std::string renderer = info.gl_renderer;
  if (info.gl_vendor.empty() || renderer.empty()) {
    return std::nullopt;
  }
  const size_t version = renderer.rfind(", ");
  if (renderer.starts_with("ANGLE (") && renderer.ends_with(")") &&
      version != std::string::npos) {
    renderer = renderer.substr(0, version) + ", Unspecified Version)";
  }
  return model::Fingerprint::WebGL{.vendor = info.gl_vendor,
                                   .renderer = std::move(renderer)};
}

void CloseEveryPage(Profile* profile) {
  std::vector<base::WeakPtr<BrowserWindowInterface>> browsers;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetProfile()->GetOriginalProfile() == profile) {
          browsers.push_back(browser->GetWeakPtr());
        }
        return true;
      });
  for (const base::WeakPtr<BrowserWindowInterface>& browser : browsers) {
    TabStripModel* model = browser ? browser->GetTabStripModel() : nullptr;
    while (browser && model && !model->empty()) {
      model->DetachAndDeleteWebContentsAt(model->count() - 1);
    }
  }
}

void WaitForProfileDestruction(Profile* profile,
                               base::TimeDelta timeout,
                               base::OnceCallback<void(bool)> done) {
  new ProfileDestructionWaiter(profile, timeout, std::move(done));
}

void CloseProfile(Profile* profile,
                  std::unique_ptr<ScopedProfileKeepAlive> keep_alive,
                  base::OnceCallback<void(bool closed)> done) {
  PANE_TRACE("call", "PaneBrowserApplication.closeContext",
             base::DictValue().Set("identifier",
                                   profile->GetBaseName().AsUTF8Unsafe()));
  // Chrome destroys a profile once nothing holds it (its
  // DestroyProfileOnBrowserClose): the hold goes here, the Browsers' with the
  // Browsers. A hosted Browser stays open without tabs
  // (Browser::TabStripEmpty), so each is closed once its pages are gone;
  // until then the profile kept every database on the volume open.
  WaitForProfileDestruction(
      profile, base::Seconds(10),
      base::BindOnce(
          [](std::string identifier, base::OnceCallback<void(bool)> done,
             bool closed) {
            PANE_TRACE("callback", "PaneBrowserApplication.didCloseContext",
                       base::DictValue()
                           .Set("identifier", identifier)
                           .Set("closed", closed));
            std::move(done).Run(closed);
          },
          profile->GetBaseName().AsUTF8Unsafe(), std::move(done)));
  // A download under way holds the profile too: it stops as at a quit, its
  // partial file removed.
  DownloadCoreServiceFactory::GetForBrowserContext(profile)->CancelDownloads(
      DownloadCoreService::CancelDownloadsTrigger::kShutdown);
  keep_alive.reset();
  CloseEveryPage(profile);
  chrome::CloseAllBrowsersWithProfile(profile);
}

void DeleteProfile(Profile* profile,
                   std::unique_ptr<ScopedProfileKeepAlive> keep_alive,
                   base::OnceCallback<void(bool deleted)> done) {
  const std::string identifier = profile->GetBaseName().AsUTF8Unsafe();
  PANE_TRACE("call", "PaneBrowserApplication.deleteContext",
             base::DictValue().Set("identifier", identifier));
  const base::FilePath path = profile->GetPath();
  keep_alive.reset();
  // Without beforeunload: Chrome's deletion asks each page that has one, and
  // a page that answers "Stay" (or never gets its dialog shown) would cancel
  // the deletion and leave the profile loaded.
  CloseEveryPage(profile);
  new ProfileDeletionWaiter(
      path, base::BindOnce(
                [](std::string identifier, base::OnceCallback<void(bool)> done,
                   bool deleted) {
                  PANE_TRACE("callback",
                             "PaneBrowserApplication.didDeleteContext",
                             base::DictValue()
                                 .Set("identifier", identifier)
                                 .Set("deleted", deleted));
                  std::move(done).Run(deleted);
                },
                identifier, std::move(done)));
  g_browser_process->profile_manager()
      ->GetDeleteProfileHelper()
      .MaybeScheduleProfileForDeletion(
          path, base::DoNothing(), ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
}

void DropEngineKeys(
    base::OnceCallback<void(size_t browser, uint32_t network, uint32_t storage)>
        done) {
  PANE_TRACE("call", "PaneBrowserApplication.dropEngineKeys");
  encryption::DropKeys();

  // The network service and the Storage Service answer on their own.
  auto left = base::MakeRefCounted<KeysLeft>();
  base::RepeatingClosure answered = base::BarrierClosure(
      2, base::BindOnce(
             [](scoped_refptr<KeysLeft> left,
                base::OnceCallback<void(size_t, uint32_t, uint32_t)> done) {
               const size_t browser = encryption::KeyCount();
               PANE_TRACE("callback",
                          "PaneBrowserApplication.engineKeysDropped",
                          base::DictValue()
                              .Set("browser", static_cast<int>(browser))
                              .Set("network", static_cast<int>(left->network))
                              .Set("storage", static_cast<int>(left->storage)));
               std::move(done).Run(browser, left->network, left->storage);
             },
             left, std::move(done)));

  content::GetNetworkService()->PaneDropEngineKeys(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](scoped_refptr<KeysLeft> left, base::RepeatingClosure answered,
                 uint32_t count) {
                left->network = count;
                answered.Run();
              },
              left, answered),
          0u));
  content::PaneDropStorageServiceKeys(base::BindOnce(
      [](scoped_refptr<KeysLeft> left, base::RepeatingClosure answered,
         uint32_t count) {
        left->storage = count;
        answered.Run();
      },
      left, answered));
}

}  // namespace pane
