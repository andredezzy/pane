// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TABS_TAB_SWITCHER_VIEW_H_
#define PANE_UI_TABS_TAB_SWITCHER_VIEW_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"

namespace gfx {
class RenderText;
}

namespace views {
class AXVirtualView;
}

namespace pane {

// The Ctrl+Tab switcher (the Swift's TabSwitcherView, the Electron app's):
// the recently used tabs, grouped under their profile, in a 320-point panel
// in the middle of the window over a black 40% scrim. Each group's header is
// its profile's dot and name; each row, its tab's favicon and title. The
// selected row takes 14% of its profile's color with a 40% ring outside it,
// and moves at once (DESIGN.md › Motion). It opens and closes at once, as the
// Swift's did. Presses on the scrim reach what is under it, and those on the
// panel go nowhere. Laid out on fractions of a point, as SwiftUI lays out its
// 16.5-point headers, and drawn there: its rows sit where the Swift's do, to
// the pixel. The scrim is a solid layer, drawn without paint; the panel and
// its shadow paint a layer of their own, which a moved selection draws two
// rows of again.
class TabSwitcherView : public views::View, public views::ViewTargeterDelegate {
  METADATA_HEADER(TabSwitcherView, views::View)

 public:
  struct Tab {
    std::string id;
    std::u16string title;
    // Null draws the placeholder square.
    gfx::ImageSkia favicon;
  };

  struct Group {
    Group();
    Group(Group&&);
    Group& operator=(Group&&);
    ~Group();

    std::u16string name;
    // The profile's swatch, the same in both appearances.
    SkColor color = SK_ColorTRANSPARENT;
    std::vector<Tab> tabs;
  };

  TabSwitcherView();
  TabSwitcherView(const TabSwitcherView&) = delete;
  TabSwitcherView& operator=(const TabSwitcherView&) = delete;
  ~TabSwitcherView() override;

  // Lists `groups`, the tab at `selected` (counting down through the groups)
  // selected.
  void SetGroups(std::vector<Group> groups, int selected);
  void SetSelected(int selected);

  // The panel's frame and each row's, in this view, by the names the Swift
  // app gave them ("pane.switcher", "pane.switcher.tab.<id>").
  std::vector<std::pair<std::string, gfx::Rect>> NamedFrames() const;

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                  float new_device_scale_factor) override;

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

 private:
  // The panel and its shadow, painted.
  class Panel;

  // A group or a row as laid out, in this view's points.
  struct Line {
    Line();
    Line(Line&&);
    Line& operator=(Line&&);
    ~Line();

    gfx::RectF bounds;
    std::unique_ptr<gfx::RenderText> text;
    // A row's.
    raw_ptr<const Tab> tab = nullptr;
    raw_ptr<views::AXVirtualView> accessible = nullptr;
  };

  // The rows' and the headers' places from the view's size, and their text.
  void Place();
  // The panel, its rows, and its shadow, in this view's points, which the
  // panel's view paints.
  void PaintPanel(gfx::Canvas* canvas);
  // A row's ring and its edge's antialiasing, drawn again.
  void RepaintRow(int index);
  // The text colors, from the appearance.
  void Tint();
  gfx::RectF RowRingBounds(int index) const;
  float Scale() const;

  raw_ptr<Panel> panel_view_ = nullptr;
  std::vector<Group> groups_;
  int selected_ = 0;
  gfx::RectF panel_;
  // Each group's header, and each tab's row, in reading order.
  std::vector<Line> headers_;
  std::vector<Line> rows_;
  // Each row's group, for its color.
  std::vector<size_t> row_groups_;
};

}  // namespace pane

#endif  // PANE_UI_TABS_TAB_SWITCHER_VIEW_H_
