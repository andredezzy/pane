// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_

#import "pane/public/PaneExtensions.h"

#include <memory>

#include "base/functional/callback.h"

class Profile;

namespace extensions {
class Extension;
class InstallPromptData;
}  // namespace extensions

namespace pane {
class ActionPopup;
}

NS_ASSUME_NONNULL_BEGIN

@interface PaneExtension ()
// A snapshot of `extension` in `profile`, its action as for `tabID`.
- (instancetype)initWithExtension:(const extensions::Extension&)extension
                          profile:(Profile*)profile
                            tabID:(int)tabID
                             icon:(nullable NSImage*)icon;
@end

@interface PaneExtensionInstallRequest ()
// `answer` gets YES to install.
- (instancetype)initWithPrompt:(const extensions::InstallPromptData&)prompt
                        answer:(base::OnceCallback<void(bool)>)answer;
@end

@interface PaneExtensionPopup ()
- (instancetype)initWithPopup:(std::unique_ptr<pane::ActionPopup>)popup;
// From the ActionPopup.
- (void)popupDidResize:(NSSize)size;
- (void)popupDidClose;
@end

NS_ASSUME_NONNULL_END

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_
