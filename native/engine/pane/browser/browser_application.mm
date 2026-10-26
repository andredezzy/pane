// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserApplication.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/scoped_observation.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "components/embedder_support/user_agent_utils.h"
#include "base/version_info/version_info.h"
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
// dir, so it may not name anything else.
bool IsValidIdentifier(NSString* identifier) {
  if (identifier.length == 0 || [identifier isEqualToString:@"."] ||
      [identifier isEqualToString:@".."]) {
    return false;
  }
  return [identifier rangeOfString:@"/"].location == NSNotFound &&
         [identifier rangeOfString:@":"].location == NSNotFound;
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
  BOOL _shutDown;
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

  std::vector<std::string> args;
  for (NSString* arg in NSProcessInfo.processInfo.arguments) {
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

  // Before the first Browser exists: Pane's windows host Chrome's Browsers.
  pane::InstallBrowserHooks();

  pane::Trace("call", "PaneBrowserApplication.run",
              base::DictValue().Set("dataDirectory",
                                    base::SysNSStringToUTF8(dataDirectory.path)));

  return ChromeMain(static_cast<int>(argv.size()), argv.data());
}

- (instancetype)initWithDelegate:(id<PaneBrowserApplicationDelegate>)delegate
                   dataDirectory:(NSURL*)dataDirectory {
  if ((self = [super init])) {
    _delegate = delegate;
    _dataDirectory = [dataDirectory copy];
    _contexts = [NSMutableDictionary dictionary];
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
  pane::Trace("call", "PaneBrowserApplication.openContext",
              base::DictValue().Set("identifier",
                                    base::SysNSStringToUTF8(identifier)));

  if (PaneBrowserContext* open = _contexts[identifier]) {
    completion(open, nil);
    return;
  }

  if (!IsValidIdentifier(identifier)) {
    completion(nil, [NSError errorWithDomain:kErrorDomain
                                        code:1
                                    userInfo:@{
                                      NSLocalizedDescriptionKey :
                                          @"Invalid profile identifier."
                                    }]);
    return;
  }

  ProfileManager* manager = g_browser_process->profile_manager();
  base::FilePath path =
      manager->user_data_dir().Append(base::SysNSStringToUTF8(identifier));
  NSString* key = [identifier copy];
  __weak PaneBrowserApplication* weakSelf = self;

  manager->CreateProfileAsync(path, base::BindOnce(^(Profile* profile) {
    PaneBrowserApplication* strongSelf = weakSelf;
    if (!strongSelf || !profile) {
      completion(nil, [NSError errorWithDomain:kErrorDomain
                                          code:2
                                      userInfo:@{
                                        NSLocalizedDescriptionKey :
                                            @"The profile could not be loaded."
                                      }]);
      return;
    }

    PaneBrowserContext* context = strongSelf->_contexts[key];
    if (!context) {
      context = [[PaneBrowserContext alloc] initWithProfile:profile
                                                 identifier:key];
      strongSelf->_contexts[key] = context;
    }

    pane::Trace("callback", "PaneBrowserApplication.openContext.completion",
                base::DictValue().Set("identifier",
                                      base::SysNSStringToUTF8(key)));
    completion(context, nil);
  }));
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
  pane::Trace("call", "PaneBrowserApplication.deleteContext",
              base::DictValue().Set("identifier",
                                    base::SysNSStringToUTF8(context.identifier)));
  Profile* profile = context.profile;
  if (!profile || _contexts[context.identifier] != context) {
    completion(NO);
    return;
  }
  const base::FilePath path = profile->GetPath();
  [_contexts removeObjectForKey:context.identifier];
  [context shutDown];
  // Chrome closes the profile's Browsers (Pane's included), then marks the
  // profile for deletion and removes its data; the directory goes once nothing
  // holds the profile, or at the next start.
  new ProfileDeletionWaiter(
      path, base::BindOnce(
                [](void (^completion)(BOOL), NSString* identifier,
                   bool deleted) {
                  pane::Trace("callback", "PaneBrowserApplication.didDeleteContext",
                              base::DictValue()
                                  .Set("identifier",
                                       base::SysNSStringToUTF8(identifier))
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
  pane::Trace("call", "PaneBrowserApplication.terminate");
  chrome::AttemptExit();
}

- (NSString*)userAgent {
  return base::SysUTF8ToNSString(embedder_support::GetUserAgent());
}

- (NSString*)chromiumVersion {
  return base::SysUTF8ToNSString(
      std::string(version_info::GetVersionNumber()));
}

- (BOOL)setFingerprint:(NSString*)json
    forContextIdentifier:(NSString*)identifier
                   error:(NSError**)error {
  pane::Trace("call", "PaneBrowserApplication.setFingerprint",
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
  pane::Trace("callback", "PaneBrowserApplicationDelegate.didStart");
  [_delegate browserApplicationDidStart:self];
}

// Runs as quitting starts, and again after the message loop as a backstop;
// only the first call does anything.
- (void)engineWillShutDown {
  if (_shutDown) {
    return;
  }
  _shutDown = YES;
  pane::Trace("callback", "PaneBrowserApplication.willShutDown");
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
