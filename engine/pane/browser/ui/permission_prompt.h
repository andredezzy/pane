// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_PERMISSION_PROMPT_H_
#define PANE_BROWSER_UI_PERMISSION_PROMPT_H_

#include <memory>
#include <optional>
#include <vector>

#include "components/permissions/permission_prompt.h"

@class PanePermissionRequest;

namespace content {
class WebContents;
}

namespace pane {

// A permission request of a Pane tab, asked through the app
// (webContents:requestPermission:) instead of Chrome's bubble. Chrome's
// PermissionRequestManager owns it and destroys it once the request is
// decided, or to re-create it.
class PermissionPrompt : public permissions::PermissionPrompt {
 public:
  // nullptr when the tab is not Pane's or the app takes no requests.
  static std::unique_ptr<permissions::PermissionPrompt> Create(
      content::WebContents* web_contents,
      Delegate* delegate);

  PermissionPrompt(const PermissionPrompt&) = delete;
  PermissionPrompt& operator=(const PermissionPrompt&) = delete;
  ~PermissionPrompt() override;

  // permissions::PermissionPrompt:
  bool UpdateAnchor() override;
  TabSwitchingBehavior GetTabSwitchingBehavior() override;
  permissions::PermissionPromptDisposition GetPromptDisposition()
      const override;
  bool IsAskPrompt() const override;
  std::optional<gfx::Rect> GetViewBoundsInScreen() const override;
  std::vector<permissions::ElementAnchoredBubbleVariant> GetPromptVariants()
      const override;
  std::optional<permissions::feature_params::PermissionElementPromptPosition>
  GetPromptPosition() const override;

 private:
  PermissionPrompt();

  PanePermissionRequest* __strong request_;
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_PERMISSION_PROMPT_H_
