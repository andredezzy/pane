// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profiles.h"

#include <limits.h>

#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/translate/core/browser/translate_pref_names.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/handover.h"
#include "services/network/enterprise/encryption/pane_cache_hook.h"

namespace pane {

bool IsValidProfileIdentifier(std::string_view identifier) {
  return !identifier.empty() && identifier != "." && identifier != ".." &&
         identifier.size() <= NAME_MAX &&
         identifier.find_first_of("/:") == std::string_view::npos;
}

void RegisterEngineKey(const base::FilePath& user_data_dir,
                       const std::string& identifier,
                       base::span<const uint8_t> key) {
  encryption::InstallHooks();
  network::enterprise_encryption::InstallPaneCacheHook();

  std::vector<base::FilePath> roots = {user_data_dir,
                                       user_data_dir.Append("Volume")};

  // Their resolved forms: /var is /private/var, and the caches' folder links
  // into the volume too. A realpath, once per open.
  const base::FilePath resolved = base::MakeAbsoluteFilePath(user_data_dir);

  if (!resolved.empty()) {
    roots.push_back(resolved);
    roots.push_back(resolved.Append("Volume"));
  }

  base::FilePath caches;
  chrome::GetUserCacheDirectory(user_data_dir.Append(identifier), &caches);

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
  if (!manager || !IsValidProfileIdentifier(identifier) ||
      engine_key.size() != encryption::kKeySize) {
    std::move(done).Run(nullptr);
    return;
  }

  const base::FilePath path = manager->user_data_dir().Append(identifier);

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
      base::BindOnce(&base::CreateDirectory, path),
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
}

}  // namespace pane
