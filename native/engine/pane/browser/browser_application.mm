// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserApplication.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "pane/browser/engine_trace.h"
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

}  // namespace

@implementation PaneBrowserApplication {
  __weak id<PaneBrowserApplicationDelegate> _delegate;
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

- (void)terminate {
  pane::Trace("call", "PaneBrowserApplication.terminate");
  chrome::AttemptExit();
}

- (void)engineDidStart {
  pane::Trace("callback", "PaneBrowserApplicationDelegate.didStart");
  [_delegate browserApplicationDidStart:self];
}

- (void)engineWillShutDown {
  pane::Trace("callback", "PaneBrowserApplication.willShutDown");
  for (PaneBrowserContext* context in _contexts.allValues) {
    [context shutDown];
  }
  [_contexts removeAllObjects];
}

@end
