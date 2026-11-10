// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_DIALOGS_INTERNAL_H_
#define PANE_BROWSER_UI_DIALOGS_INTERNAL_H_

#import "pane/public/PaneDialogs.h"

namespace content {
struct ContextMenuParams;
class RenderFrameHost;
}  // namespace content

NS_ASSUME_NONNULL_BEGIN

@interface PaneContextMenu ()
- (instancetype)initWithParams:(const content::ContextMenuParams&)params
                         frame:(content::RenderFrameHost&)frame;
@end

NS_ASSUME_NONNULL_END

#endif  // PANE_BROWSER_UI_DIALOGS_INTERNAL_H_
