// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/page_context_menu.h"

#include <memory>
#include <utility>

#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "chrome/browser/ui/views/renderer_context_menu/render_view_context_menu_views.h"
#include "ui/menus/simple_menu_model.h"

namespace pane {

namespace {

// Chrome's own menu for Views, as ChromeWebContentsViewDelegateViews builds
// it, which says when it has shown and when it has closed: Views' menus run
// in no loop of their own on Windows.
class ViewsPageMenu : public RenderViewContextMenuViews {
 public:
  ViewsPageMenu(content::RenderFrameHost& frame,
                const content::ContextMenuParams& params,
                bool paste,
                bool paste_and_match_style)
      : RenderViewContextMenuViews(frame,
                                   params,
                                   paste,
                                   paste_and_match_style) {}

  // `shown` once the menu shows, `closed` once it has closed.
  void Await(base::OnceClosure shown, base::OnceClosure closed) {
    shown_ = std::move(shown);
    closed_ = std::move(closed);
  }

  bool showing() const { return showing_; }

  // RenderViewContextMenuViews:
  void OnMenuWillShow(ui::SimpleMenuModel* source) override {
    RenderViewContextMenuViews::OnMenuWillShow(source);
    if (source != &menu_model()) {
      return;
    }
    showing_ = true;
    if (shown_) {
      std::move(shown_).Run();
    }
  }

  void MenuClosed(ui::SimpleMenuModel* source) override {
    RenderViewContextMenuViews::MenuClosed(source);
    if (source != &menu_model()) {
      return;
    }
    showing_ = false;
    if (closed_) {
      std::move(closed_).Run();
    }
  }

 private:
  bool showing_ = false;
  base::OnceClosure shown_;
  base::OnceClosure closed_;
};

}  // namespace

// static
std::unique_ptr<RenderViewContextMenuBase> PageContextMenus::Make(
    content::RenderFrameHost& frame,
    const content::ContextMenuParams& params,
    bool paste,
    bool paste_and_match_style) {
  return std::make_unique<ViewsPageMenu>(frame, params, paste,
                                         paste_and_match_style);
}

// Views' menu shows as Show returns, at the press, and its model hears it
// close a task later (SimpleMenuModel::MenuWillClose): Run returns once it
// has, as the Mac's returns once the system's menu has closed, from a loop of
// its own that runs the app's tasks meanwhile, the channel's among them, as
// Chrome's own menus allow in theirs (RenderViewContextMenuViews::Show).
// static
void PageContextMenus::Run(RenderViewContextMenuBase& menu,
                           base::OnceClosure tracking) {
  auto& views_menu = static_cast<ViewsPageMenu&>(menu);
  base::RunLoop closed(base::RunLoop::Type::kNestableTasksAllowed);
  views_menu.Await(std::move(tracking), closed.QuitClosure());
  views_menu.Show();
  // Nothing showed: no widget for the page, or no item left.
  if (!views_menu.showing()) {
    return;
  }
  closed.Run();
}

}  // namespace pane
