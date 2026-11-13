// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/devtools.h"

#include <cstdint>
#include <utility>

#include "base/functional/callback.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/devtools/devtools_toggle_action.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "pane/ui/interaction_log.h"

namespace pane {

namespace {

// DevTools opened, timed until their page first paints something, at the
// frame that shows it; gone with their page.
class FirstPaint : public content::WebContentsObserver {
 public:
  FirstPaint(content::WebContents* devtools,
             uint64_t interval,
             base::RepeatingCallback<::ui::Compositor*()> compositor)
      : content::WebContentsObserver(devtools),
        interval_(interval),
        compositor_(std::move(compositor)) {}

  // content::WebContentsObserver:
  void DidFirstVisuallyNonEmptyPaint() override {
    EndInteractionAtNextFrame(Interaction::kOpenDevTools, interval_,
                              compositor_.Run());
    delete this;
  }
  void WebContentsDestroyed() override { delete this; }

 private:
  const uint64_t interval_;
  base::RepeatingCallback<::ui::Compositor*()> compositor_;
};

}  // namespace

DockedDevTools DockedDevToolsFor(content::WebContents* page,
                                 const gfx::Rect& place) {
  DockedDevTools docked;
  docked.page_bounds = place;
  if (!page) {
    return docked;
  }
  DevToolsContentsResizingStrategy strategy;
  docked.devtools = DevToolsWindow::GetInTabWebContents(page, &strategy);
  if (!docked.devtools) {
    return docked;
  }
  gfx::Rect devtools;
  gfx::Rect contents;
  ApplyDevToolsContentsResizingStrategy(strategy, gfx::Rect(place.size()),
                                        &devtools, &contents);
  docked.devtools_bounds = devtools + place.OffsetFromOrigin();
  docked.page_bounds =
      contents.IsEmpty() ? gfx::Rect() : contents + place.OffsetFromOrigin();
  return docked;
}

void ToggleDevTools(content::WebContents* page,
                    base::RepeatingCallback<::ui::Compositor*()> compositor) {
  if (!page) {
    return;
  }
  // Docked, they close as their own close button closes them. Undocked,
  // what the page shows docked is Chrome's device toolbar, which stays.
  DevToolsWindow* open =
      DevToolsWindow::GetInstanceForInspectedWebContents(page);
  if (open && open->IsDocked() &&
      DevToolsWindow::GetInTabWebContents(page, nullptr)) {
    open->GetDevToolsWebContents()->ClosePage();
    return;
  }
  const bool opening = !open;
  const uint64_t interval =
      BeginInteractionInterval(Interaction::kOpenDevTools);
  DevToolsWindow::OpenDevToolsWindow(
      page, DevToolsToggleAction::Show(),
      DevToolsOpenedByAction::kMainMenuOrMainShortcut);
  DevToolsWindow* window =
      DevToolsWindow::GetInstanceForInspectedWebContents(page);
  content::WebContents* devtools =
      window ? window->GetDevToolsWebContents().get() : nullptr;
  // An undocked window brought forward has painted already: its frame.
  if (!opening || !devtools) {
    EndInteractionAtNextFrame(Interaction::kOpenDevTools, interval,
                              compositor.Run());
    return;
  }
  new FirstPaint(devtools, interval, std::move(compositor));
}

}  // namespace pane
