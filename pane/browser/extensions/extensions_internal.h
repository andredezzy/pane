// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_

#include <memory>

#include "base/functional/callback.h"
#import "pane/public/PaneExtensions.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace extensions {
class Extension;
class InstallPromptData;
}  // namespace extensions

NS_ASSUME_NONNULL_BEGIN

@interface PaneExtensionInstallRequest ()
// `answer` gets YES to install.
- (instancetype)initWithPrompt:(const extensions::InstallPromptData&)prompt
                        answer:(base::OnceCallback<void(bool)>)answer;
@end

@interface PaneExtensionDetails ()
// What chrome://extensions' details page shows of `extension` in `profile`.
- (instancetype)initWithExtension:(const extensions::Extension&)extension
                          profile:(Profile*)profile;
@end

@interface PaneExtensionCommand ()
- (instancetype)initWithName:(NSString*)name
                     summary:(NSString*)summary
                    shortcut:(NSString*)shortcut;
@end

NS_ASSUME_NONNULL_END

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_
