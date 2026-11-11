// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/devtools_window.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/ui/window_host.h"
#include "pane/ui/app/app_events.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace pane {

namespace {

constexpr gfx::Size kSize(960, 640);

// The window's one view, the DevTools page filling it.
class DevToolsWindowView : public WindowHost::Delegate,
                           public views::WidgetObserver {
 public:
  DevToolsWindowView() {
    auto web_view = std::make_unique<views::WebView>(nullptr);
    web_view_ = web_view.get();
    host_ = std::make_unique<WindowHost>(
        this, gfx::Rect(kSize), std::move(web_view), kDevToolsWindowName);
    host_->SetTitle(u"DevTools");
    host_->widget()->AddObserver(this);
  }

  WindowHost* host() { return host_.get(); }

  // WindowHost::Delegate:
  void TabInserted(content::WebContents* contents,
                   content::WebContents* opener,
                   bool foreground) override {
    ShowPage(contents);
  }
  void TabRemoved(content::WebContents* contents) override {
    if (web_view_->web_contents() == contents) {
      ShowPage(nullptr);
    }
  }
  void ActiveTabChanged(Browser* browser,
                        content::WebContents* contents) override {
    ShowPage(contents);
  }
  void WindowWillClose() override { ShowPage(nullptr); }
  gfx::Rect PageBounds() const override { return web_view_->bounds(); }
  // Chrome shows the window as it makes it, before its page has joined: it
  // shows once the page has.
  void ShowRequested(bool active) override {
    if (web_view_->web_contents()) {
      Show();
    }
  }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override {
    widget->RemoveObserver(this);
    // Gone once the widget's own close call is over.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

 private:
  void ShowPage(content::WebContents* contents) {
    if (web_view_->web_contents() == contents) {
      return;
    }
    web_view_->SetWebContents(contents);
    if (contents && !shown_) {
      Show();
    }
  }

  // In the middle of the screen the first time, as AppKit centers a window,
  // and in front only where Pane may take it.
  void Show() {
    views::Widget* widget = host_->widget();
    if (!widget || widget->IsClosed()) {
      return;
    }
    if (!shown_) {
      shown_ = true;
      widget->CenterWindow(kSize);
    }
    if (MayTakeFront()) {
      widget->Show();
    } else {
      widget->ShowInactive();
    }
  }

  raw_ptr<views::WebView> web_view_ = nullptr;
  std::unique_ptr<WindowHost> host_;
  bool shown_ = false;
};

}  // namespace

WindowHost* MakeDevToolsWindow() {
  // Keeps itself until its widget goes.
  return (new DevToolsWindowView())->host();
}

}  // namespace pane
