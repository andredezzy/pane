// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_DIALOGS_INTERNAL_H_
#define PANE_BROWSER_UI_DIALOGS_INTERNAL_H_

#import "pane/public/PaneDialogs.h"

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

class GURL;

namespace content {
struct ContextMenuParams;
class RenderFrameHost;
}  // namespace content

@class PaneBrowserWindow;
@class PaneWebContents;

NS_ASSUME_NONNULL_BEGIN

// Each request answers Chrome through a callback it runs once, from the app's
// answer or from -withdraw…, which the engine calls when Chrome takes the
// request back; a withdrawal also tells the app (didDismissRequest).

@interface PaneJavaScriptDialog ()
- (instancetype)initWithType:(PaneJavaScriptDialogType)type
                     message:(const std::u16string&)message
           defaultPromptText:(const std::u16string&)defaultPromptText
                   originURL:(const GURL&)originURL
                      reload:(BOOL)reload
                         tab:(nullable PaneWebContents*)tab
                  completion:
                      (base::OnceCallback<void(bool, const std::u16string&)>)
                          completion;
- (void)withdrawAccepting:(BOOL)accept text:(const std::u16string&)text;
// The page went (its tab closed): nothing is answered.
- (void)withdraw;
@end

@interface PanePermissionRequest ()
// `answer` gets 0 allow, 1 allow this time, 2 deny, 3 dismiss.
- (instancetype)initWithPermissions:(NSArray<NSString*>*)permissions
                          originURL:(const GURL&)originURL
                                tab:(nullable PaneWebContents*)tab
                             answer:(base::OnceCallback<void(int)>)answer;
// Chrome dropped the prompt unanswered; nothing is run.
- (void)withdraw;
@end

@interface PaneAuthenticationChallenge ()
// `answer` gets the credentials, or nullopt to cancel.
- (instancetype)
    initWithHost:(NSString*)host
           realm:(NSString*)realm
          scheme:(NSString*)scheme
           proxy:(BOOL)proxy
             tab:(nullable PaneWebContents*)tab
          answer:(base::OnceCallback<
                     void(std::optional<std::pair<std::u16string,
                                                  std::u16string>>)>)answer;
- (void)withdraw;
@end

@interface PaneContextMenu ()
- (instancetype)initWithParams:(const content::ContextMenuParams&)params
                         frame:(content::RenderFrameHost&)frame;
@end

@interface PaneFileChooser ()
// `answer` gets the chosen paths, or nullopt to cancel.
- (instancetype)
    initWithMode:(PaneFileChooserMode)mode
           title:(NSString*)title
      extensions:(NSArray<NSString*>*)extensions
       directory:(nullable NSURL*)directory
     defaultName:(NSString*)defaultName
          window:(nullable PaneBrowserWindow*)window
          answer:(base::OnceCallback<
                     void(std::optional<std::vector<base::FilePath>>)>)answer;
- (void)withdraw;
@end

@interface PaneExternalProtocolRequest ()
- (instancetype)initWithURL:(const GURL&)URL
            applicationName:(const std::u16string&)applicationName
                  originURL:(const GURL&)originURL
                        tab:(nullable PaneWebContents*)tab
                     answer:(base::OnceCallback<void(bool)>)answer;
// The tab closed unanswered; nothing is run.
- (void)withdraw;
@end

@interface PaneUnresponsivePage ()
// `answer` gets YES to end the renderer, NO to keep waiting.
- (instancetype)initWithTab:(PaneWebContents*)tab
                     answer:(base::OnceCallback<void(bool)>)answer;
- (void)withdraw;
@end

NS_ASSUME_NONNULL_END

#endif  // PANE_BROWSER_UI_DIALOGS_INTERNAL_H_
