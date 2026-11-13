// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PANELS_SETTINGS_VIEW_H_
#define PANE_UI_PANELS_SETTINGS_VIEW_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "pane/model/app_settings.h"
#include "pane/model/pin_entry.h"
#include "pane/ui/panels/app_icon_images.h"
#include "ui/views/view.h"

namespace views {
class ScrollView;
}

namespace pane {

class MenuButton;
class TextButton;

// The update row as Settings shows it (the Swift's UpdateRow), from the one
// Updater the pill and the Lock read (DESIGN.md › Updates): what the last
// check found, or the version on offer and its download; and the button,
// Check for updates (Checking…, disabled, while it runs), or the download's.
struct SettingsUpdate {
  // The status before any check has answered.
  static constexpr char16_t kChecksOnItsOwn[] =
      u"Pane checks for a new version on its own.";

  std::u16string status;
  std::u16string button;
  bool button_enabled = true;
  // The button downloads, then restarts into the update or quits for the
  // user to install it; otherwise it checks.
  bool downloads = false;
  // A check found nothing newer.
  bool up_to_date = false;

  friend bool operator==(const SettingsUpdate&,
                         const SettingsUpdate&) = default;
};

// Settings (the Swift's SettingsView; ADR 0065; Paper 06.1 to 06.4), in the
// content panel in the page's place, as Aside lays out its own: "Settings" at
// 22, then each section's muted label at 13 over its card, rounded 22 in a
// 1-point border, 40 in from the panel and at most 640 wide, scrolling when
// taller than the panel. Each row holds its title at 13 and its description
// at 12, muted, wrapping where the control leaves too little room, and its
// control 11 from the card's edge, at least 56 points tall, with a hairline
// under it but the last. Appearance: Theme's menu and the App icon, which
// opens the picker. Memory & disk: Sleep background tabs, Unload hidden
// profiles, and Trim cache, presets in menus, as the scheduler ticks once a
// minute. Security: the PIN's buttons, which open the Lock's PIN screen, and
// Lock after once a PIN is set. Updates: the update row, then the version.
class SettingsView : public views::View, public AppIconImages::Observer {
  METADATA_HEADER(SettingsView, views::View)

 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // A choice in a menu, as the settings take it.
    base::RepeatingCallback<void(
        base::RepeatingCallback<void(model::AppSettings&)>)>
        change;
    // The App icon row's press: the picker over the window.
    base::RepeatingClosure choose_app_icon;
    // Set up, Change, and Remove PIN.
    base::RepeatingCallback<void(model::PinEntry::Mode)> pin;
    // The update row's button.
    base::RepeatingClosure check_updates;
    base::RepeatingClosure download_update;
  };

  // What it shows.
  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    model::AppSettings settings;
    bool has_pin = false;
    SettingsUpdate update;
    // Pane's own version.
    std::u16string version;

    friend bool operator==(const Shows&, const Shows&) = default;
  };

  SettingsView(Callbacks callbacks, AppIconImages& images);
  SettingsView(const SettingsView&) = delete;
  SettingsView& operator=(const SettingsView&) = delete;
  ~SettingsView() override;

  // Only what changed changes.
  void SetShows(const Shows& shows);
  // Its controls by the Swift's names ("pane.settings.theme"), each that
  // shows, for the channel.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;
  // A menu by its name, to read and choose in as a click in it does.
  MenuButton* FindMenu(std::string_view name);

  // views::View:
  void Layout(PassKey) override;

  // AppIconImages::Observer:
  void OnAppIconThumbnailsChanged() override;

 private:
  class AppIconButton;
  class Card;
  class Contents;
  class Row;

  // The Security card's rows for whether a PIN is set.
  void ShowPinRows(bool has_pin);
  // A menu's press timed to its system menu about to show.
  void TimeOpening(MenuButton* menu);

  Callbacks callbacks_;
  const raw_ref<AppIconImages> images_;
  Shows shows_;
  bool shown_once_ = false;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<Contents> contents_ = nullptr;
  raw_ptr<MenuButton> theme_ = nullptr;
  raw_ptr<AppIconButton> app_icon_ = nullptr;
  raw_ptr<MenuButton> tab_sleep_ = nullptr;
  raw_ptr<MenuButton> profile_unload_ = nullptr;
  raw_ptr<MenuButton> cache_budget_ = nullptr;
  raw_ptr<Row> pin_ = nullptr;
  raw_ptr<Row> lock_after_row_ = nullptr;
  raw_ptr<TextButton> set_up_pin_ = nullptr;
  raw_ptr<TextButton> change_pin_ = nullptr;
  raw_ptr<TextButton> remove_pin_ = nullptr;
  raw_ptr<MenuButton> lock_after_ = nullptr;
  raw_ptr<Row> update_row_ = nullptr;
  raw_ptr<TextButton> update_button_ = nullptr;
  raw_ptr<Row> version_row_ = nullptr;
  // A menu's opening, while timed.
  std::optional<uint64_t> opening_menu_;
  base::ScopedObservation<AppIconImages, AppIconImages::Observer>
      images_observation_{this};
};

}  // namespace pane

#endif  // PANE_UI_PANELS_SETTINGS_VIEW_H_
