// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TOOLBAR_TOOLBAR_VIEW_H_
#define PANE_UI_TOOLBAR_TOOLBAR_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace pane {

namespace model {
class ToolbarProgress;
}

class AddressField;
class IconButton;

// The selected tab's toolbar at the top of the content panel (the Swift's
// ToolbarView), at Aside's sizes (ADR 0060): Back, Forward, and Reload, or
// Stop while the page loads, 32-point circles 2 apart; the address, which
// takes what the row leaves; the extensions' pinned actions and puzzle,
// whose place it keeps for X1; and the profile's chip. Back and Forward dim
// to 40% while the tab has nowhere to go that way, as Chrome's and Aside's
// do. The address follows the selected tab: another tab shows its own, and
// the same tab's new address replaces the text unless the user is typing
// over it.
class ToolbarView : public views::View, public views::TextfieldController {
  METADATA_HEADER(ToolbarView, views::View)

 public:
  // The page starts 43 points down the panel (the Electron app's
  // TOOLBAR_HEIGHT 51 less the 8-point gutter), a point under the row's 42.
  static constexpr int kHeight = 43;

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    base::RepeatingClosure back;
    base::RepeatingClosure forward;
    base::RepeatingClosure reload;
    base::RepeatingClosure stop;
    // Return in the address, with what it holds.
    base::RepeatingCallback<void(const std::u16string&)> address_entered;
    // Esc in the address: its text is the page's again, and the page takes
    // the keys.
    base::RepeatingClosure address_left;
  };

  // What the toolbar shows, as values the app gives it (the Swift's read the
  // model).
  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    // The selected tab, its address, and its title; none without one.
    std::optional<std::string> tab_id;
    std::u16string url;
    std::u16string title;
    // From the tab's page: its history, and whether a new document loads.
    bool can_go_back = false;
    bool can_go_forward = false;
    bool loading = false;
    // The shown profile's chip: its name, in its color, fixed in both
    // appearances. None without a profile shown.
    std::optional<std::u16string> profile_name;
    SkColor profile_color = SK_ColorTRANSPARENT;
  };

  // `progress`, the app's, outlives the toolbar.
  ToolbarView(Callbacks callbacks, model::ToolbarProgress* progress);
  ToolbarView(const ToolbarView&) = delete;
  ToolbarView& operator=(const ToolbarView&) = delete;
  ~ToolbarView() override;

  void SetShows(const Shows& shows);
  // ⌘L, and a new tab: the address takes the keys, all of it selected.
  void FocusAddress();
  // What the address holds.
  std::u16string address() const;
  AddressField* address_field() { return address_; }
  // The controls, by the names the channel reads them by.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  void Layout(PassKey) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  class ProfileChip;

  // The address gives up the keys, unless the app gave them to the page
  // (the Swift's endEditingAddress).
  void EndEditing();

  Callbacks callbacks_;
  Shows shows_;
  // The tab whose address the field shows, and that address as the tab
  // last gave it.
  std::optional<std::string> address_tab_id_;
  std::u16string address_url_;
  raw_ptr<IconButton> back_ = nullptr;
  raw_ptr<IconButton> forward_ = nullptr;
  raw_ptr<IconButton> reload_ = nullptr;
  raw_ptr<AddressField> address_ = nullptr;
  // X1's: the extensions' pinned actions and the puzzle, 32 points while a
  // profile shows (ADR 0054). Empty until X1 fills it.
  raw_ptr<views::View> actions_ = nullptr;
  raw_ptr<ProfileChip> chip_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_TOOLBAR_TOOLBAR_VIEW_H_
