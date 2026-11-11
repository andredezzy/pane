// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_PROFILE_ROW_H_
#define PANE_UI_SIDEBAR_PROFILE_ROW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/text.h"
#include "pane/ui/sidebar/row_button.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"

namespace views {
class Label;
}  // namespace views

namespace gfx {
class Canvas;
}

namespace ui {
class ColorProvider;
}

namespace pane {

// A profile header's contents (the Swift's ProfileHeaderContent), which its
// row and a drag's copy of it (H3) both draw: its color as a mark, 4 by 16
// points as a bar, rounded 2, grown into a 16-point square, rounded 4; its
// name at 12 medium; its proxy's dot; and, collapsed, how many tabs it has,
// muted. 8 in, its parts 8 apart.
inline constexpr TextStyle kProfileName{12, weight::kMedium, 16};

// Where the name, the count, and the dot sit in a header of `size`, with its
// mark `mark_width` wide, a count `count_width` wide (0: none), and a dot or
// not.
struct ProfileHeaderPlaces {
  gfx::Rect name;
  gfx::Rect count;
  float dot_x = 0;
};
ProfileHeaderPlaces PlaceProfileHeader(const gfx::Size& size,
                                       float mark_width,
                                       int count_width,
                                       bool has_dot);
// The mark's width `morph` of the way from the bar to the square, the
// spring's overshoot included.
float ProfileMarkWidth(double morph);
// The mark, `morph` of the way, in `color`, in a header `height` tall.
void PaintProfileMark(gfx::Canvas& canvas,
                      int height,
                      SkColor color,
                      double morph);
// The proxy's dot from `x`, in its status's color, the same in both
// appearances.
void PaintProxyDot(gfx::Canvas& canvas,
                   const ui::ColorProvider& colors,
                   float x,
                   int height,
                   model::BrowserModel::ProxyStatus status);

// A profile's header (the Swift's ProfileSection header and
// ProfileHeaderContent), 36 points: its color as a 4-point bar, grown into a
// 16-point square while its page shows, rounding off as it widens, with the
// name moving along, its text darkening, and the row's 4% tint of its color
// fading in, on the spring an indicator's own change of shape takes (DESIGN.md
// › Motion, ADR 0064); its name at 12 medium; its proxy's dot; and, collapsed,
// how many tabs it has. Under the pointer the accent fades in, unless its
// page shows; its text follows the profile, not the hover. A click opens or
// collapses it; its right-click menu is the Mac's own (Paper 03.1).
class ProfileRow : public RowButton,
                   public views::ContextMenuController,
                   public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ProfileRow, RowButton)

 public:
  static constexpr int kHeight = 36;

  // What the row's presses ask, by its profile.
  struct Actions {
    Actions();
    Actions(Actions&&);
    Actions& operator=(Actions&&);
    ~Actions();

    base::RepeatingClosure toggle;
    base::RepeatingClosure edit;
    base::RepeatingCallback<void(bool keep_loaded)> set_keep_loaded;
    // Delete profile: asked first (the alert), then deleted.
    base::RepeatingClosure ask_delete;
  };

  // The menu's commands.
  enum Command { kEditProfile = 1, kKeepLoaded, kDeleteProfile };

  ProfileRow(const SidebarProfile& profile, Actions actions);
  ProfileRow(const ProfileRow&) = delete;
  ProfileRow& operator=(const ProfileRow&) = delete;
  ~ProfileRow() override;

  void Update(const SidebarProfile& profile);
  const SidebarProfile& profile() const { return profile_; }
  // The bar's width now, 4 to 16 points, as the spring carries it.
  float MarkWidth() const;
  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // RowButton:
  void LookChanged() override;

  // A step of the morph.
  void Morphed(double value);
  // How far the morph is, 0 to 1, without the spring's overshoot.
  double Shown() const;

  SidebarProfile profile_;
  const Actions actions_;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  // Where the proxy's dot sits, as last laid out.
  float dot_x_ = 0;
  ValueMotion morph_;
  base::WeakPtrFactory<ProfileRow> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_PROFILE_ROW_H_
