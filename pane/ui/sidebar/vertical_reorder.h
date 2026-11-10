// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_VERTICAL_REORDER_H_
#define PANE_UI_SIDEBAR_VERTICAL_REORDER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/events/event_handler.h"
#include "ui/gfx/geometry/point_f.h"

namespace ui {
class Compositor;
}

namespace views {
class View;
}

namespace pane {

class DragCopy;
class RowList;
class ValueMotion;

// Reordering the sidebar's rows by dragging (the Swift's VerticalReorder, ADR
// 0066), as the Electron app's sortable lists did (dnd-kit, its vertical-axis
// modifier): profiles among themselves, each with its tabs, and a profile's
// tabs among themselves, never across profiles. A drag starts 5 points from
// a press on a profile's header or a tab's row and moves up and down only,
// never out of its list. The row stays in the list at 40%, standing where it
// would drop as the rows around it slide aside, while a copy of it rides the
// pointer, at once; on release the copy settles into the row's place, the
// order is kept, and the copy goes as the row comes back. It watches the
// rows' mouse events ahead of them (a pre-target handler on their list) and
// lets each through: a row's press shows at once, and a click once a drag
// has begun does nothing (is_dragging).
class VerticalReorder : public ui::EventHandler {
 public:
  // A list a drag reorders: its rows in groups, one for each id, in their
  // order: each profile's header with its tabs and New tab while it is open,
  // or one profile's tabs.
  struct List {
    List();
    List(const List&);
    List& operator=(const List&);
    ~List();

    // A tab list's profile; none for the profiles'.
    std::string profile_id;
    std::vector<std::string> ids;
    std::vector<std::vector<raw_ptr<views::View>>> rows;
    // The copy's height: a profile's header's, or a tab's row's.
    int copy_height = 0;
  };

  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // The list a press on `row`, one of the list's rows, would drag, if any.
    base::RepeatingCallback<std::optional<List>(views::View* row)> list_at;
    // What the copy of the row `id` of `list` shows.
    base::RepeatingCallback<
        void(DragCopy& copy, const List& list, const std::string& id)>
        fill_copy;
    // The drop: the row at `from` moved to `to`.
    base::RepeatingCallback<void(const List& list, int from, int to)> move;
  };

  // `rows`, the list; `contents`, the scroll's contents that hold it from
  // their top left, where the copy rides over it.
  VerticalReorder(RowList* rows, views::View* contents, Callbacks callbacks);
  VerticalReorder(const VerticalReorder&) = delete;
  VerticalReorder& operator=(const VerticalReorder&) = delete;
  ~VerticalReorder() override;

  // A row is dragged, or settling after a drag: a press's action then is the
  // drag's release, and does nothing.
  bool is_dragging() const { return drag_.has_value(); }
  // The copy riding the pointer, or settling, if any.
  DragCopy* copy() const { return copy_; }
  base::WeakPtr<VerticalReorder> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // The rows are about to come, go, or move other than by a drop: a drag
  // ends there, its row back in place, nothing kept. Before they change, so
  // what it ends is still there.
  void RowsChanging();
  // The rows' values changed: the copy shows them.
  void RowsChanged();
  // The pressed row lost the mouse: the drag ends as a release would.
  void CaptureLost();

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;

 private:
  struct Press {
    raw_ptr<views::View> row;
    gfx::PointF at;
  };
  struct Drag {
    Drag();
    Drag(const Drag&);
    Drag& operator=(const Drag&);
    ~Drag();

    List list;
    std::string id;
    int start = 0;
    int target = 0;
    // Each group's height as the drag began, the list's first top and its
    // height, in the list's coordinates, and the press's distance below the
    // dragged row's top.
    std::map<std::string, double> heights;
    double first_top = 0;
    double height = 0;
    double grab = 0;
    // The pointer's height in the list.
    double pointer = 0;
    // Released: the copy moves onto the row, then goes.
    bool settling = false;
  };

  // The list's row an event's target is, or is in.
  views::View* RowOf(ui::EventTarget* target) const;
  // Where an event is, in the list's coordinates.
  gfx::PointF PointOf(const ui::MouseEvent& event) const;

  void Begin();
  void Move(double pointer);
  void Release();
  // The order kept, the copy fading as the row comes back.
  void Drop();
  // Ends the drag at once, nothing kept.
  void Cancel();

  // The copy's top in the list: under the pointer, held inside the list.
  double CopyTop() const;
  // Every row of the list in the order they stand, the dragged one at its
  // target.
  std::vector<views::View*> DrawnOrder() const;
  std::vector<views::View*> DraggedRows() const;
  // A step of the drop's fade, `share` of the way: the row back to 100%,
  // the copy gone. Once it ends, on the next turn, the copy goes and the
  // rows paint into the list again.
  void Fade(double share);
  void FinishFade();

  ui::Compositor* compositor() const;

  const raw_ptr<RowList> rows_;
  const raw_ptr<views::View> contents_;
  Callbacks callbacks_;
  std::optional<Press> press_;
  std::optional<Drag> drag_;
  raw_ptr<DragCopy> copy_ = nullptr;
  // The copy settling into the row's place.
  std::unique_ptr<ValueMotion> settle_;
  // A drop's fade: its copy, the copy's settle, the rows coming back.
  raw_ptr<DragCopy> fading_ = nullptr;
  std::unique_ptr<ValueMotion> fading_settle_;
  std::vector<raw_ptr<views::View>> fading_rows_;
  std::unique_ptr<ValueMotion> fade_;
  // A drop 0.22 s after the release, as the copy has all but settled.
  base::OneShotTimer drop_;
  // The drop's own change of the rows, which ends nothing.
  bool committing_ = false;
  base::WeakPtrFactory<VerticalReorder> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_VERTICAL_REORDER_H_
