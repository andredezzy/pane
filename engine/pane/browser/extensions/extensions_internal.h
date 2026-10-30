// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSIONS_INTERNAL_H_

#import "pane/public/PaneExtensions.h"

#include <memory>

#include "base/functional/callback.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace extensions {
class Extension;
class InstallPromptData;
}  // namespace extensions

namespace pane {
class ActionPopup;
class ExtensionMenu;
}  // namespace pane

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
// Its Browser is going: the popup closes now, before the Browser it holds.
- (void)browserDidClose;
@end

@interface PaneExtensionMenuItem ()
// The item's command in the ExtensionMenu that made it, which runs it.
@property(readonly) int commandID;
- (instancetype)initWithKind:(PaneExtensionMenuItemKind)kind
                   commandID:(int)commandID
                       title:(NSString*)title
                     enabled:(BOOL)enabled
                     checked:(BOOL)checked
                        icon:(nullable NSImage*)icon
                    children:(NSArray<PaneExtensionMenuItem*>*)children;
@end

@interface PaneExtensionActionMenu ()
// `menu`'s items and `extension`'s site access on `contents`'s page, run for
// `contents` while it lasts.
- (instancetype)initWithMenu:(std::unique_ptr<pane::ExtensionMenu>)menu
                   extension:(const extensions::Extension&)extension
                    contents:(content::WebContents&)contents;
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
