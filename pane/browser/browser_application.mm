// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <limits.h>

#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/common/chrome_paths_internal.h"
#include "components/embedder_support/user_agent_utils.h"
#include "pane/app/engine_switches.h"
#include "pane/browser/app_callback.h"
#import "pane/browser/browser_application_internal.h"
#import "pane/browser/browser_context_internal.h"
#include "pane/browser/caches_in_memory.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/fingerprints.h"
#include "pane/browser/profiles.h"
#include "pane/browser/ui/window_host.h"
#include "pane/common/host.h"
#include "pane/encryption/engine_keys.h"
#include "pane/encryption/handover.h"
#include "pane/encryption/profile_migration.h"
#include "pane/model/profile.h"
#import "pane/public/PaneBrowserApplication.h"
#include "services/network/enterprise/encryption/pane_cache_hook.h"

extern "C" int ChromeMain(int argc, const char** argv);

NSNotificationName const PaneBrowserApplicationShouldReopenNotification =
    @PANE_SHOULD_REOPEN_NOTIFICATION;

namespace {

PaneBrowserApplication* g_application = nil;

NSString* const kErrorDomain = @"PaneBrowserApplication";

NSError* ContextError(NSInteger code, NSString* description) {
  return [NSError errorWithDomain:kErrorDomain
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey : description}];
}

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

}  // namespace

@implementation PaneBrowserApplication {
  __weak id<PaneBrowserApplicationDelegate> _delegate;
  BOOL _started;
  BOOL _shutDown;
  // What other apps asked to open before the engine started.
  NSMutableArray<NSURL*>* _pendingURLs;
  NSURL* _dataDirectory;
  NSMutableDictionary<NSString*, PaneBrowserContext*>* _contexts;
}

+ (PaneBrowserApplication*)sharedApplication {
  return g_application;
}

+ (int)runWithDelegate:(id<PaneBrowserApplicationDelegate>)delegate
         dataDirectory:(NSURL*)dataDirectory {
  if (g_application) {
    [NSException raise:NSInternalInconsistencyException
                format:@"The Pane engine runs once per process."];
  }
  g_application =
      [[PaneBrowserApplication alloc] initWithDelegate:delegate
                                         dataDirectory:dataDirectory];

  // What Pane's engine starts with (SetEngineSwitches): the switches the
  // launch may keep, then the ones every launch runs with.
  NSArray<NSString*>* arguments = NSProcessInfo.processInfo.arguments;
  const bool scenario = [arguments containsObject:@"--use-mock-keychain"];
  std::vector<std::string> given;
  for (NSString* arg in arguments) {
    given.push_back(base::SysNSStringToUTF8(arg));
  }
  base::CommandLine command_line(given);
  pane::SetEngineSwitches(command_line,
                          base::apple::NSURLToFilePath(dataDirectory));
  const std::vector<std::string>& args = command_line.argv();

  std::vector<const char*> argv;
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }

  if (scenario) {
    pane::AllowTracing();
  }

  // Before the first Browser exists: Pane's windows host Chrome's Browsers.
  pane::InstallBrowserHooks();

  PANE_TRACE("call", "PaneBrowserApplication.run",
             base::DictValue().Set(
                 "dataDirectory", base::SysNSStringToUTF8(dataDirectory.path)));

  return ChromeMain(static_cast<int>(argv.size()), argv.data());
}

- (instancetype)initWithDelegate:(id<PaneBrowserApplicationDelegate>)delegate
                   dataDirectory:(NSURL*)dataDirectory {
  if ((self = [super init])) {
    _delegate = delegate;
    _dataDirectory = [dataDirectory copy];
    _contexts = [NSMutableDictionary dictionary];
    _pendingURLs = [NSMutableArray array];
    // From the start: Launch Services hands over the URLs it launched the
    // app for before the engine has started.
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(engineOpenURLs:)
                                               name:@PANE_OPEN_URLS_NOTIFICATION
                                             object:nil];
  }
  return self;
}

- (void)openContextWithIdentifier:(NSString*)identifier
                        engineKey:(NSData*)engineKey
                       completion:
                           (void (^)(PaneBrowserContext* _Nullable context,
                                     NSError* _Nullable error))completion {
  PANE_TRACE(
      "call", "PaneBrowserApplication.openContext",
      base::DictValue().Set("identifier", base::SysNSStringToUTF8(identifier)));

  // Once quitting started, every context was released, and one opened now
  // would hold its profile past the engine's teardown.
  if (_shutDown) {
    completion(nil, ContextError(4, @"The engine is shutting down."));
    return;
  }

  if (PaneBrowserContext* open = _contexts[identifier]) {
    completion(open, nil);
    return;
  }

  if (!pane::model::IsProfileFolderName(base::SysNSStringToUTF8(identifier))) {
    completion(nil, ContextError(1, @"Invalid profile identifier."));
    return;
  }

  if (engineKey.length != pane::encryption::kKeySize) {
    completion(nil, ContextError(5, @"The profile's key is not a key."));
    return;
  }

  NSString* key = [identifier copy];
  __weak PaneBrowserApplication* weakSelf = self;

  auto created = base::BindOnce(^(Profile* profile) {
    PaneBrowserApplication* strongSelf = weakSelf;
    if (!strongSelf || strongSelf->_shutDown || !profile) {
      completion(nil,
                 ContextError(2, strongSelf && strongSelf->_shutDown
                                     ? @"The engine is shutting down."
                                     : @"The profile could not be loaded."));
      return;
    }

    PaneBrowserContext* context = strongSelf->_contexts[key];
    if (!context) {
      context = [[PaneBrowserContext alloc] initWithProfile:profile
                                                 identifier:key];
      strongSelf->_contexts[key] = context;
    }

    PANE_TRACE(
        "callback", "PaneBrowserApplication.openContext.completion",
        base::DictValue().Set("identifier", base::SysNSStringToUTF8(key)));
    completion(context, nil);
  });

  pane::OpenProfile(base::SysNSStringToUTF8(identifier),
                    base::apple::NSDataToSpan(engineKey), std::move(created));
}

- (void)migrateContextWithIdentifier:(NSString*)identifier
                           engineKey:(NSData*)engineKey
                              folder:(NSURL*)folder
                            progress:(void (^)(double share))progress
                          completion:(void (^)(BOOL migrated))completion {
  PANE_TRACE(
      "call", "PaneBrowserApplication.migrateContext",
      base::DictValue().Set("identifier", base::SysNSStringToUTF8(identifier)));
  ProfileManager* manager = g_browser_process->profile_manager();
  pane::RegisterEngineKey(manager->user_data_dir(),
                          base::SysNSStringToUTF8(identifier),
                          base::apple::NSDataToSpan(engineKey));
  // Resolved: Foundation has /var for /private/var, where the roots have both
  // only as the user data dir was given.
  const base::FilePath root =
      base::MakeAbsoluteFilePath(base::apple::NSURLToFilePath(folder));

  // No folder: paths from an empty root would lead into the working directory.
  if (root.empty()) {
    completion(NO);
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       // A quit mid-way goes on where it was, at the next launch.
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(
          [](base::FilePath root, void (^progress)(double)) {
            return pane::MigrateProfileFolder(
                root, base::BindRepeating([](void (^progress)(double),
                                             double share) { progress(share); },
                                          progress));
          },
          root, [progress copy]),
      base::BindOnce(
          [](void (^completion)(BOOL), bool migrated) {
            PANE_TRACE("callback",
                       "PaneBrowserApplication.migrateContext.completion",
                       base::DictValue().Set("migrated", migrated));
            completion(migrated);
          },
          [completion copy]));
}

- (void)restoreContextWithIdentifier:(NSString*)identifier
                           engineKey:(NSData*)engineKey
                              folder:(NSURL*)folder
                          completion:(void (^)(BOOL restored))completion {
  PANE_TRACE(
      "call", "PaneBrowserApplication.restoreContext",
      base::DictValue().Set("identifier", base::SysNSStringToUTF8(identifier)));
  ProfileManager* manager = g_browser_process->profile_manager();
  pane::RegisterEngineKey(manager->user_data_dir(),
                          base::SysNSStringToUTF8(identifier),
                          base::apple::NSDataToSpan(engineKey));
  // Resolved: Foundation has /var for /private/var, where the roots have both
  // only as the user data dir was given.
  const base::FilePath root =
      base::MakeAbsoluteFilePath(base::apple::NSURLToFilePath(folder));

  if (root.empty()) {
    completion(NO);
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&pane::RestoreSafetyCopy, root.AppendASCII("Profile"),
                     root.AppendASCII("SafetyCopy")),
      base::BindOnce(
          [](void (^completion)(BOOL), bool restored) { completion(restored); },
          [completion copy]));
}

- (void)repairContextWithIdentifier:(NSString*)identifier
                          engineKey:(NSData*)engineKey
                             folder:(NSURL*)folder
                         completion:(void (^)(BOOL repaired))completion {
  PANE_TRACE(
      "call", "PaneBrowserApplication.repairContext",
      base::DictValue().Set("identifier", base::SysNSStringToUTF8(identifier)));
  ProfileManager* manager = g_browser_process->profile_manager();
  pane::RegisterEngineKey(manager->user_data_dir(),
                          base::SysNSStringToUTF8(identifier),
                          base::apple::NSDataToSpan(engineKey));
  // Resolved: Foundation has /var for /private/var, where the roots have both
  // only as the user data dir was given.
  const base::FilePath root =
      base::MakeAbsoluteFilePath(base::apple::NSURLToFilePath(folder));

  if (root.empty()) {
    completion(NO);
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&pane::RepairMigratedProfile, root.AppendASCII("Profile"),
                     root.AppendASCII("SafetyCopy")),
      base::BindOnce(
          [](void (^completion)(BOOL), bool repaired) {
            PANE_TRACE("callback",
                       "PaneBrowserApplication.repairContext.completion",
                       base::DictValue().Set("repaired", repaired));
            completion(repaired);
          },
          [completion copy]));
}

- (void)dropEngineKeysWithCompletion:
    (void (^)(NSDictionary<NSString*, NSNumber*>* left))completion {
  pane::DropEngineKeys(base::BindOnce(
      [](void (^completion)(NSDictionary<NSString*, NSNumber*>*),
         size_t browser, uint32_t network, uint32_t storage) {
        completion(@{
          @"browser" : @(browser),
          @"network" : @(network),
          @"storage" : @(storage),
        });
      },
      [completion copy]));
}

- (PaneBrowserContext*)contextForProfile:(Profile*)profile {
  for (PaneBrowserContext* context in _contexts.allValues) {
    if (context.profile == profile) {
      return context;
    }
  }
  return nil;
}

- (void)deleteContext:(PaneBrowserContext*)context
           completion:(void (^)(BOOL deleted))completion {
  // Deleting closes the profile's pages: never from inside a tab strip
  // notification or a tab's callback.
  if (pane::InAppCallback()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self deleteContext:context completion:completion];
    });
    return;
  }
  PANE_TRACE("call", "PaneBrowserApplication.deleteContext",
             base::DictValue().Set(
                 "identifier", base::SysNSStringToUTF8(context.identifier)));
  Profile* profile = context.profile;
  if (!profile || _contexts[context.identifier] != context) {
    completion(NO);
    return;
  }
  const base::FilePath path = profile->GetPath();
  [_contexts removeObjectForKey:context.identifier];
  [context shutDown];
  // Without beforeunload: Chrome's deletion asks each page that has one, and
  // a page that answers "Stay" (or never gets its dialog shown) would cancel
  // the deletion and leave the profile loaded.
  pane::CloseEveryPage(profile);
  // Chrome closes the profile's Browsers (Pane's included), then marks the
  // profile for deletion and removes its data; the directory goes once nothing
  // holds the profile, or at the next start.
  new ProfileDeletionWaiter(
      path,
      base::BindOnce(
          [](void (^completion)(BOOL), NSString* identifier, bool deleted) {
            PANE_TRACE(
                "callback", "PaneBrowserApplication.didDeleteContext",
                base::DictValue()
                    .Set("identifier", base::SysNSStringToUTF8(identifier))
                    .Set("deleted", deleted));
            completion(deleted);
          },
          completion, context.identifier));
  g_browser_process->profile_manager()
      ->GetDeleteProfileHelper()
      .MaybeScheduleProfileForDeletion(
          path, base::DoNothing(), ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
}

- (void)closeContext:(PaneBrowserContext*)context
          completion:(void (^)(BOOL closed))completion {
  // Closing closes the profile's pages: never from inside a tab strip
  // notification or a tab's callback.
  if (pane::InAppCallback()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self closeContext:context completion:completion];
    });
    return;
  }
  PANE_TRACE("call", "PaneBrowserApplication.closeContext",
             base::DictValue().Set(
                 "identifier", base::SysNSStringToUTF8(context.identifier)));
  Profile* profile = context.profile;
  if (!profile || _contexts[context.identifier] != context) {
    completion(NO);
    return;
  }
  [_contexts removeObjectForKey:context.identifier];
  // Chrome destroys a profile once nothing holds it (its
  // DestroyProfileOnBrowserClose): the context's hold goes with its
  // shutdown, the Browsers' with the Browsers. A hosted Browser stays open
  // without tabs (Browser::TabStripEmpty), so each is closed once its pages
  // are gone; until then the profile kept every database on the volume open.
  pane::WaitForProfileDestruction(
      profile, base::Seconds(10),
      base::BindOnce(
          [](void (^completion)(BOOL), NSString* identifier, bool closed) {
            PANE_TRACE(
                "callback", "PaneBrowserApplication.didCloseContext",
                base::DictValue()
                    .Set("identifier", base::SysNSStringToUTF8(identifier))
                    .Set("closed", closed));
            completion(closed);
          },
          completion, context.identifier));
  // A download under way holds the profile too: it stops as at a quit, its
  // partial file removed.
  DownloadCoreServiceFactory::GetForBrowserContext(profile)->CancelDownloads(
      DownloadCoreService::CancelDownloadsTrigger::kShutdown);
  [context shutDown];
  pane::CloseEveryPage(profile);
  chrome::CloseAllBrowsersWithProfile(profile);
}

- (void)terminate {
  PANE_TRACE("call", "PaneBrowserApplication.terminate");
  // Quitting closes every Browser: never from inside a tab strip
  // notification or a tab's callback.
  if (pane::InAppCallback()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      chrome::AttemptExit();
    });
    return;
  }
  chrome::AttemptExit();
}

- (NSString*)userAgent {
  return base::SysUTF8ToNSString(embedder_support::GetUserAgent());
}

- (NSString*)hostWebGLVendor {
  const std::optional<pane::model::Fingerprint::WebGL> webgl =
      pane::HostWebGL();
  return webgl ? base::SysUTF8ToNSString(webgl->vendor) : nil;
}

- (NSString*)hostWebGLRenderer {
  const std::optional<pane::model::Fingerprint::WebGL> webgl =
      pane::HostWebGL();
  return webgl ? base::SysUTF8ToNSString(webgl->renderer) : nil;
}

- (NSString*)chromiumVersion {
  return base::SysUTF8ToNSString(std::string(version_info::GetVersionNumber()));
}

- (void)setKeepsCachesInMemory:(BOOL)inMemory
          forContextIdentifier:(NSString*)identifier {
  PANE_TRACE("call", "PaneBrowserApplication.setKeepsCachesInMemory",
             base::DictValue()
                 .Set("identifier", base::SysNSStringToUTF8(identifier))
                 .Set("inMemory", static_cast<bool>(inMemory)));
  pane::SetKeepsCachesInMemory(base::SysNSStringToUTF8(identifier), inMemory);
}

- (BOOL)setFingerprint:(NSString*)json
    forContextIdentifier:(NSString*)identifier
                   error:(NSError**)error {
  PANE_TRACE("call", "PaneBrowserApplication.setFingerprint",
             base::DictValue()
                 .Set("identifier", base::SysNSStringToUTF8(identifier))
                 .Set("set", json != nil));

  std::string message;
  if (!pane::Fingerprints::Get().Set(base::SysNSStringToUTF8(identifier),
                                     json ? base::SysNSStringToUTF8(json) : "",
                                     &message)) {
    if (error) {
      *error = [NSError
          errorWithDomain:kErrorDomain
                     code:3
                 userInfo:@{
                   NSLocalizedDescriptionKey : base::SysUTF8ToNSString(message)
                 }];
    }
    return NO;
  }

  [_contexts[identifier] applyFingerprint];
  return YES;
}

- (void)engineDidStart {
  PANE_TRACE("callback", "PaneBrowserApplicationDelegate.didStart");
  [_delegate browserApplicationDidStart:self];
  _started = YES;

  if (_pendingURLs.count > 0) {
    NSArray<NSURL*>* urls = [_pendingURLs copy];
    [_pendingURLs removeAllObjects];
    [self deliverURLs:urls];
  }
}

// Chrome's AppController hands over what another app asked Pane to open
// (PANE_OPEN_URLS_NOTIFICATION); until the engine has started, it waits.
- (void)engineOpenURLs:(NSNotification*)notification {
  NSArray<NSURL*>* urls = notification.userInfo[@PANE_OPEN_URLS_KEY];
  if (urls.count == 0 || _shutDown) {
    return;
  }

  if (!_started) {
    [_pendingURLs addObjectsFromArray:urls];
    return;
  }

  [self deliverURLs:urls];
}

- (void)deliverURLs:(NSArray<NSURL*>*)urls {
  PANE_TRACE("callback", "PaneBrowserApplicationDelegate.openURLs",
             base::DictValue().Set("count", static_cast<int>(urls.count)));
  if ([_delegate respondsToSelector:@selector(browserApplication:openURLs:)]) {
    [_delegate browserApplication:self openURLs:urls];
  }
}

// Runs as quitting starts, and again after the message loop as a backstop;
// only the first call does anything.
- (void)engineWillShutDown {
  if (_shutDown) {
    return;
  }
  _shutDown = YES;
  PANE_TRACE("callback", "PaneBrowserApplication.willShutDown");
  if ([_delegate
          respondsToSelector:@selector(browserApplicationWillShutDown:)]) {
    [_delegate browserApplicationWillShutDown:self];
  }
  for (PaneBrowserContext* context in _contexts.allValues) {
    [context shutDown];
  }
  [_contexts removeAllObjects];
}

@end
