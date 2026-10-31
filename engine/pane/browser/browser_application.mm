// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserApplication.h"

#include <limits.h>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/embedder_support/user_agent_utils.h"
#include "content/public/browser/gpu_data_manager.h"
#include "gpu/config/gpu_info.h"
#include "base/version_info/version_info.h"
#include "pane/browser/app_callback.h"
#include "pane/browser/fingerprints.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/window_host.h"
#include "pane/common/host.h"

#import "pane/browser/browser_application_internal.h"
#import "pane/browser/browser_context_internal.h"

extern "C" int ChromeMain(int argc, const char** argv);

NSNotificationName const PaneBrowserApplicationShouldReopenNotification =
    @PANE_SHOULD_REOPEN_NOTIFICATION;
NSString* const PaneReopenHasVisibleWindowsKey =
    @PANE_REOPEN_HAS_VISIBLE_WINDOWS_KEY;

namespace {

PaneBrowserApplication* g_application = nil;

NSString* const kErrorDomain = @"PaneBrowserApplication";

bool HasSwitch(const std::vector<std::string>& args, std::string_view name) {
  const std::string prefix = "--" + std::string(name);
  for (const std::string& arg : args) {
    if (arg == prefix || arg.starts_with(prefix + "=")) {
      return true;
    }
  }
  return false;
}

// Adds `value` to a comma-separated switch (--disable-features=…), keeping
// whatever the command line already lists there.
void AddToSwitchList(std::vector<std::string>& args,
                     std::string_view name,
                     std::string_view value) {
  const std::string prefix = "--" + std::string(name) + "=";
  for (std::string& arg : args) {
    if (arg.starts_with(prefix)) {
      arg += "," + std::string(value);
      return;
    }
  }
  args.push_back(prefix + std::string(value));
}

// A profile identifier becomes a directory name under the engine's user data
// dir, so it may not name anything else, nor be longer than a file name can
// be (255 bytes on APFS).
bool IsValidIdentifier(NSString* identifier) {
  if (identifier.length == 0 || [identifier isEqualToString:@"."] ||
      [identifier isEqualToString:@".."] ||
      [identifier lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > NAME_MAX) {
    return false;
  }
  return [identifier rangeOfString:@"/"].location == NSNotFound &&
         [identifier rangeOfString:@":"].location == NSNotFound;
}

NSError* ContextError(NSInteger code, NSString* description) {
  return [NSError errorWithDomain:kErrorDomain
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey : description}];
}

// Closes every page of `profile`, in every window, without beforeunload:
// Chrome's deletion asks each page that has one, and a page that answers
// "Stay" (or never gets its dialog shown) would cancel the deletion and leave
// the profile loaded.
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

// Calls back once Chrome has marked a profile for permanent deletion: its
// browsing data is being removed, and its directory goes when the profile is
// destroyed or, at the latest, at the next start. Deletes itself.
class ProfileDeletionWaiter : public ProfileManagerObserver {
 public:
  ProfileDeletionWaiter(base::FilePath path, base::OnceCallback<void(bool)> done)
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
  g_application = [[PaneBrowserApplication alloc] initWithDelegate:delegate
                                                     dataDirectory:dataDirectory];

  // A launch on the mock keychain, a scenario's, whose secret opens none of
  // the user's data (ADR 0113), passes Chrome its switches. Any other passes
  // the URLs it opens and Pane's own switches alone: a process of the user's
  // starting Pane with --remote-debugging-port, --enable-logging, or
  // --load-extension would read every page once the PIN opened the data.
  NSArray<NSString*>* arguments = NSProcessInfo.processInfo.arguments;
  const bool scenario = [arguments containsObject:@"--use-mock-keychain"];
  std::vector<std::string> args;
  for (NSUInteger index = 0; index < arguments.count; index++) {
    NSString* arg = arguments[index];
    if (index > 0 && !scenario && [arg hasPrefix:@"-"] &&
        ![arg hasPrefix:@"--pane-wipe-after="]) {
      continue;
    }
    args.push_back(base::SysNSStringToUTF8(arg));
  }

  // Chrome's own UI stays off: the app draws every window and the menu bar.
  args.push_back("--" PANE_HOSTED_SWITCH);
  args.push_back("--no-startup-window");
  args.push_back("--no-first-run");
  args.push_back("--no-default-browser-check");
  // Printing goes straight to the system print panel, not Chrome's preview.
  args.push_back("--disable-print-preview");
  // Features that put Chrome's own UI outside the app's window: Glic (Gemini
  // in Chrome) adds a menu bar item, the Media Router a Cast dialog (the
  // Electron app had no casting).
  AddToSwitchList(args, "disable-features", "Glic,MediaRouter");
  // Requests to Google that Chrome makes on its own and the Electron app
  // never did (found by their traffic annotations in a net log): the
  // component updater, the network time check, and the on-device AI model
  // go out from the browser itself, past every profile's proxy; AI Mode's
  // eligibility check carries a profile's Google cookies and country; the
  // Translate ranker fetches its model. The default search engine's
  // preconnect opens connections to Google no page asked for, at start and
  // again while a page shows, which a net log shows as connections, not
  // requests.
  args.push_back("--disable-component-update");
  AddToSwitchList(args, "disable-features",
                  "NetworkTimeServiceQuerying,"
                  "AimEnabled,AimServerEligibilityEnabled,"
                  "AimServerRequestOnStartupEnabled,"
                  "OptimizationGuideModelExecution,"
                  "OnDeviceModelBackgroundDownload,OptimizationHints,"
                  "TranslateRankerQuery,TranslateRankerEnforcement,"
                  "PreconnectToSearch");
  // The segmentation platform keeps a database of the pages visited
  // (segmentation_platform/ukm_db) at the user data dir's root, outside the
  // volume every profile's data is in (ADR 0113), for guesses Chrome's own UI
  // makes, which Pane does not show.
  AddToSwitchList(args, "disable-features",
                  "SegmentationPlatform,SegmentationPlatformUkmEngine");
  // Nor its histograms, kept in BrowserMetrics at the same root for metrics
  // Pane never sends: counts of the pages loaded and the tabs open.
  AddToSwitchList(args, "disable-features", "PersistentHistograms");
  // Chrome discards background tabs under memory pressure (its urgent
  // discarding), swapping an empty WebContents into the tab. Pane's sleep
  // owns that job, so Chrome's stays off. An extension's chrome.tabs.discard
  // still works, and the TabBridge follows the tab into its new WebContents.
  AddToSwitchList(args, "enable-features", "DisableTabDiscarding");

  // An explicit --user-data-dir (scenarios use throwaway ones) wins.
  if (!HasSwitch(args, "user-data-dir")) {
    NSURL* engine = [dataDirectory URLByAppendingPathComponent:@"Engine"
                                                   isDirectory:YES];
    args.push_back("--user-data-dir=" + base::SysNSStringToUTF8(engine.path));
  }

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

- (id<PaneBrowserApplicationDelegate>)delegate {
  return _delegate;
}

- (NSURL*)dataDirectory {
  return _dataDirectory;
}

- (void)openContextWithIdentifier:(NSString*)identifier
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

  if (!IsValidIdentifier(identifier)) {
    completion(nil, ContextError(1, @"Invalid profile identifier."));
    return;
  }

  ProfileManager* manager = g_browser_process->profile_manager();
  base::FilePath path =
      manager->user_data_dir().Append(base::SysNSStringToUTF8(identifier));
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

  if (manager->GetProfileByPath(path)) {
    manager->CreateProfileAsync(path, std::move(created));
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
          [](base::FilePath path, base::OnceCallback<void(Profile*)> created,
             bool exists) {
            ProfileManager* manager = g_browser_process->profile_manager();
            if (!exists || !manager) {
              std::move(created).Run(nullptr);
              return;
            }
            manager->CreateProfileAsync(path, std::move(created));
          },
          path, std::move(created)));
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
  CloseEveryPage(profile);
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
          path, base::DoNothing(),
          ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
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
  const std::string vendor =
      content::GpuDataManager::GetInstance()->GetGPUInfo().gl_vendor;
  return vendor.empty() ? nil : base::SysUTF8ToNSString(vendor);
}

// ANGLE names its renderer "ANGLE (vendor, device, version)" and gives WebGL
// "Unspecified Version" instead of the OS version Chrome's GPU info has
// (DisplayMtl::getVersionString), so the version is swapped the same way.
- (NSString*)hostWebGLRenderer {
  std::string renderer =
      content::GpuDataManager::GetInstance()->GetGPUInfo().gl_renderer;
  if (renderer.empty()) {
    return nil;
  }
  const size_t version = renderer.rfind(", ");
  if (renderer.starts_with("ANGLE (") && renderer.ends_with(")") &&
      version != std::string::npos) {
    renderer = renderer.substr(0, version) + ", Unspecified Version)";
  }
  return base::SysUTF8ToNSString(renderer);
}

- (NSString*)chromiumVersion {
  return base::SysUTF8ToNSString(
      std::string(version_info::GetVersionNumber()));
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
  if ([_delegate respondsToSelector:@selector
                 (browserApplicationWillShutDown:)]) {
    [_delegate browserApplicationWillShutDown:self];
  }
  for (PaneBrowserContext* context in _contexts.allValues) {
    [context shutDown];
  }
  [_contexts removeAllObjects];
}

@end
