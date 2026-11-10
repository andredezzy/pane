// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_PERMISSION_PROMPT_H_
#define PANE_UI_DIALOGS_PERMISSION_PROMPT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "components/permissions/permission_prompt.h"
#include "pane/ui/dialogs/page_dialogs.h"

namespace content {
class WebContents;
}

namespace pane {

// A site's permission request in the window's own dialog (ADR 0021), as the
// Swift asked it: "<host> wants to", a line for each permission, and Allow,
// Allow This Time, and Don't Allow, stacked; Esc dismisses it, so the site
// may ask again. Chrome's PermissionRequestManager owns it, and destroys it
// once the request is decided, or to make it again.
class PermissionPrompt : public permissions::PermissionPrompt {
 public:
  // Gives the page's requests to the window's dialog, through Chrome's one
  // view factory, PermissionRequestManager's (named for tests): Chromium has
  // no other seam for it, as the embedder's (PermissionsClient::CreatePrompt)
  // is Chrome's own ChromePermissionsClient's, which makes only Chrome's chip
  // and bubble, anchored in a BrowserView (ADR 0021). The name going away at
  // an upgrade is a compile error, as a conflict would be (ADR 0004).
  static void Install(content::WebContents* web_contents);

  // What Chrome's permission line reads for a permission's key (the Swift's
  // DialogPresenter.describe).
  static std::u16string Describe(std::string_view permission);

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

  // Null when the request can't be shown, as Chrome's factory's, where no
  // window of Pane's is open.
  static std::unique_ptr<permissions::PermissionPrompt> Create(
      content::WebContents* web_contents,
      Delegate* delegate);

  std::optional<PageDialogs::Id> id_;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_PERMISSION_PROMPT_H_
