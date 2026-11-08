// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_ROOT_VIEW_H_
#define PANE_UI_WINDOW_ROOT_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace views {
class Textfield;
class WebView;
}  // namespace views

namespace pane {

// What the sidebar lists: each profile, then its tabs.
struct SidebarRow {
  std::string profile_id;
  // Empty for the profile's own row.
  std::string tab_id;
  std::u16string title;
  bool selected = false;
};

// Everything a Pane window draws (Paper's main window): the sidebar on the
// left, 260 points wide, and the content panel beside it, inset 8, holding
// the toolbar and, under it, the page in its rounded card. The page is the
// selected tab's WebContents, shown in the one WebView the window has.
class RootView : public views::View, public views::TextfieldController {
  METADATA_HEADER(RootView, views::View)

 public:
  using RowPressed = base::RepeatingCallback<void(const SidebarRow&)>;
  // Return in the address field, with what it holds.
  using AddressEntered = base::RepeatingCallback<void(const std::u16string&)>;

  RootView(RowPressed row_pressed, AddressEntered address_entered);
  RootView(const RootView&) = delete;
  RootView& operator=(const RootView&) = delete;
  ~RootView() override;

  void SetRows(const std::vector<SidebarRow>& rows);
  void ShowPage(content::WebContents* contents);
  content::WebContents* page() const;
  void SetAddress(const std::u16string& address);
  void FocusAddress();
  // The prototype's sheet over the page (G4): a scrim and a card with a
  // field, keys into it and not the page, Esc and Done closing it.
  void ShowSheet(bool show);
  bool sheet_visible() const;
  // Where the page is, in this view.
  gfx::Rect PageBounds() const;
  // Whether a press at `point` drags the window: the header rows, where no
  // control is (Paper's window).
  bool IsDragArea(const gfx::Point& point);

  // views::View:
  void Layout(PassKey) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  RowPressed row_pressed_;
  AddressEntered address_entered_;
  raw_ptr<views::View> sidebar_ = nullptr;
  raw_ptr<views::View> panel_ = nullptr;
  raw_ptr<views::Textfield> address_ = nullptr;
  raw_ptr<views::WebView> page_ = nullptr;
  raw_ptr<views::View> sheet_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_ROOT_VIEW_H_
