// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_WINDOW_H_
#define PANE_UI_WINDOW_WINDOW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/browser/ui/window_host.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"

class GURL;
class Profile;

namespace content {
class WebContents;
}

namespace pane {

class RootView;
struct SidebarRow;

// One Pane window in Views: its widget, holding the hidden Browser of each
// profile it shows (WindowHost, ADR 0014), and the root view that draws the
// sidebar, the toolbar, and the selected tab's page.
class Window : public WindowHost::Delegate, public ui::AcceleratorTarget {
 public:
  // ⌘T's tab between the frame that answers the key and its page: its row
  // and the address field show at once, and the engine makes the page (11
  // ms) after that frame, as the Swift app's AfterFrame orders it.
  struct PendingTab {
    std::string id;
    raw_ptr<Profile> profile;
    std::string url;
    // Still the tab shown: nothing else was chosen meanwhile.
    bool foreground = true;
  };

  // The sidebar's rows, from the app, which knows the profiles.
  using Rows = base::RepeatingCallback<std::vector<SidebarRow>(Window&)>;
  // Told whenever what the window shows changes: a tab came or went, or
  // another was selected.
  using Changed = base::RepeatingClosure;

  Window(const gfx::Rect& bounds, Rows rows, Changed changed);
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  ~Window() override;

  // Shows the window, in front of the app in use only when `activate`.
  void Show(bool activate);
  WindowHost& host() { return *host_; }
  RootView& root() { return *root_; }

  // A new tab of `profile` at `url`, selected at once when `foreground`.
  content::WebContents* OpenTab(Profile* profile,
                                const GURL& url,
                                bool foreground);
  // Shows `contents`, a tab of one of the window's profiles, and its profile.
  void Select(content::WebContents* contents);
  content::WebContents* selected() const;
  // The tab shown's id, ⌘T's before its page exists; empty with none.
  std::string selected_id() const;
  const std::optional<PendingTab>& pending_tab() const { return pending_tab_; }

  // ⌘T: a new tab of the profile shown, at the new tab page.
  void NewTab();
  // ⌘L: the address field, its text selected.
  void FocusAddress();
  // ⌘1 to ⌘8: the profile's tab at that place; ⌘9, its last.
  void SelectTabAt(int number);

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // WindowHost::Delegate:
  void TabInserted(content::WebContents* contents,
                   content::WebContents* opener,
                   bool foreground) override;
  void TabRemoved(content::WebContents* contents) override;
  void ActiveTabChanged(Browser* browser,
                        content::WebContents* contents) override;
  void WindowWillClose() override;
  gfx::Rect PageBounds() const override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) override;
  views::NativeWidget* CreateNativeWidget(views::Widget* widget) override;
  int NonClientHitTest(const gfx::Point& point) override;

 private:
  // Shows `contents` and its profile; a tab chosen takes the keyboard,
  // where ⌘T's keeps it in the address field.
  void ShowTab(content::WebContents* contents, bool chosen);
  // Has the engine make ⌘T's tab `id`, at `url`, if it still waits. `id` is
  // a copy: the pending tab it names goes as the page is made.
  void OpenPendingTab(std::string id, const GURL& url);
  // Gives `contents`, the page OpenPendingTab made, the id its row showed
  // before it existed, in whichever of Chrome's calls comes first.
  void AdoptOpeningTab(content::WebContents* contents);
  void RowPressed(const SidebarRow& row);
  void AddressEntered(const std::u16string& text);
  void Refresh();

  views::UnhandledKeyboardEventHandler unhandled_keys_;
  Rows rows_;
  Changed changed_;
  raw_ptr<RootView> root_ = nullptr;
  std::unique_ptr<WindowHost> host_;
  std::optional<PendingTab> pending_tab_;
  // ⌘T's tab while the engine inserts it, inside OpenPendingTab.
  std::optional<PendingTab> opening_tab_;
  base::WeakPtrFactory<Window> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_WINDOW_H_
