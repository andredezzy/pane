// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/vertical_reorder.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/model/reorder.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/sidebar/drag_copy.h"
#include "pane/ui/sidebar/row_button.h"
#include "pane/ui/sidebar/row_list.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// A drag starts this far from its press, as DragGesture's minimumDistance.
constexpr double kDragStart = 5;
// The dragged row stands in the list at 40%.
constexpr float kDraggedOpacity = 0.4f;
// The drop comes 0.22 s after the release, the copy all but settled into
// the row's place by then (the spring's 98.6%).
constexpr base::TimeDelta kDropAfter = base::Milliseconds(220);

// `copy`'s card at `top` in the list, as wide as its rows and `height` tall.
void PlaceCard(DragCopy* copy, const RowList* rows, int height, double top) {
  copy->SetCard(gfx::RectF(RowList::kInset, top,
                           std::max(rows->width() - 2 * RowList::kInset, 0),
                           height));
}

}  // namespace

VerticalReorder::List::List() = default;
VerticalReorder::List::List(const List&) = default;
VerticalReorder::List& VerticalReorder::List::operator=(const List&) = default;
VerticalReorder::List::~List() = default;

VerticalReorder::Callbacks::Callbacks() = default;
VerticalReorder::Callbacks::Callbacks(Callbacks&&) = default;
VerticalReorder::Callbacks& VerticalReorder::Callbacks::operator=(Callbacks&&) =
    default;
VerticalReorder::Callbacks::~Callbacks() = default;

VerticalReorder::Drag::Drag() = default;
VerticalReorder::Drag::Drag(const Drag&) = default;
VerticalReorder::Drag& VerticalReorder::Drag::operator=(const Drag&) = default;
VerticalReorder::Drag::~Drag() = default;

VerticalReorder::VerticalReorder(RowList* rows,
                                 views::View* contents,
                                 Callbacks callbacks)
    : rows_(rows), contents_(contents), callbacks_(std::move(callbacks)) {}

// The copies are views of the list's, which go with it.
VerticalReorder::~VerticalReorder() = default;

void VerticalReorder::RowsChanging() {
  if (committing_) {
    return;
  }
  press_.reset();
  if (drag_) {
    Cancel();
  }
  // A drop's fade ends at once, while its rows are all still there.
  FinishFade();
}

void VerticalReorder::RowsChanged() {
  if (drag_ && copy_) {
    callbacks_.fill_copy.Run(*copy_, drag_->list, drag_->id);
  }
}

void VerticalReorder::CaptureLost() {
  press_.reset();
  if (drag_ && !drag_->settling) {
    Release();
  }
}

void VerticalReorder::OnMouseEvent(ui::MouseEvent* event) {
  switch (event->type()) {
    case ui::EventType::kMousePressed: {
      press_.reset();
      // A press while a drop settles starts nothing: its click is the drag's
      // release, which does nothing.
      if (drag_ || !event->IsOnlyLeftMouseButton() || event->IsControlDown()) {
        return;
      }
      if (views::View* row = RowOf(event->target())) {
        press_ = Press{row, PointOf(*event)};
      }
      return;
    }
    case ui::EventType::kMouseDragged: {
      if (!drag_ && press_ &&
          (PointOf(*event) - press_->at).Length() >= kDragStart) {
        Begin();
      }
      if (drag_ && !drag_->settling) {
        Move(PointOf(*event).y());
      }
      return;
    }
    case ui::EventType::kMouseReleased:
      press_.reset();
      if (drag_ && !drag_->settling) {
        Release();
      }
      return;
    default:
      return;
  }
}

views::View* VerticalReorder::RowOf(ui::EventTarget* target) const {
  for (auto* view = static_cast<views::View*>(target); view;
       view = view->parent()) {
    if (view->parent() == rows_) {
      return view;
    }
  }
  return nullptr;
}

gfx::PointF VerticalReorder::PointOf(const ui::MouseEvent& event) const {
  // From the view the event went to, as it stands now (a row that slid
  // included), to the list's coordinates.
  return views::View::ConvertRectToTarget(
             static_cast<views::View*>(event.target()), rows_,
             gfx::RectF(event.location_f(), gfx::SizeF()))
      .origin();
}

void VerticalReorder::Begin() {
  views::View* pressed = press_->row;
  const gfx::PointF at = press_->at;
  press_.reset();
  std::optional<List> list = callbacks_.list_at.Run(pressed);
  if (!list || list->rows.empty()) {
    return;
  }
  const auto group = std::ranges::find_if(list->rows, [&](const auto& rows) {
    return std::ranges::find(rows, pressed) != rows.end();
  });
  if (group == list->rows.end()) {
    return;
  }
  // A copy still fading from the drag before goes at once.
  FinishFade();

  const int index = group - list->rows.begin();
  Drag drag;
  drag.list = std::move(*list);
  drag.id = drag.list.ids[index];
  drag.start = drag.target = index;
  // Each group as laid out as the drag begins, its rows' places, which a
  // slide still under way has already reached.
  for (size_t i = 0; i < drag.list.ids.size(); ++i) {
    const auto& rows = drag.list.rows[i];
    drag.heights[drag.list.ids[i]] =
        rows.back()->bounds().bottom() - rows.front()->y();
  }
  drag.first_top = drag.list.rows.front().front()->y();
  drag.height =
      drag.list.rows.back().back()->bounds().bottom() - drag.first_top;
  drag.grab = at.y() - drag.list.rows[index].front()->y();
  drag.pointer = at.y();
  drag_ = std::move(drag);

  // The list's rows may slide now, each on its layer; the dragged one stands
  // at 40%, with no hover or press of its own, at once.
  std::vector<views::View*> rows;
  for (const auto& group_rows : drag_->list.rows) {
    rows.insert(rows.end(), group_rows.begin(), group_rows.end());
  }
  rows_->SetSliding(rows);
  for (views::View* row : DraggedRows()) {
    if (row->layer()) {
      row->layer()->SetOpacity(kDraggedOpacity);
    }
    if (auto* button = views::AsViewClass<RowButton>(row)) {
      button->SetDragged(true);
    }
  }
  // The copy over the rows, under the pointer.
  copy_ = contents_->AddChildView(std::make_unique<DragCopy>());
  callbacks_.fill_copy.Run(*copy_, drag_->list, drag_->id);
}

void VerticalReorder::Move(double pointer) {
  drag_->pointer = pointer;
  const double top = CopyTop();
  // The copy rides the pointer at once.
  PlaceCard(copy_, rows_, drag_->list.copy_height, top);
  // The place whose top is nearest the copy's (Reorder in PaneKit's port).
  const int target =
      model::ReorderTarget(drag_->list.ids, drag_->heights, drag_->first_top,
                           drag_->id, top, RowList::kSpacing);
  if (target != drag_->target) {
    drag_->target = target;
    rows_->SetOrder(DrawnOrder(), /*animated=*/true);
  }
}

void VerticalReorder::Release() {
  drag_->settling = true;
  // The copy settles onto the row's place, which is where it will drop, on
  // the slide's spring.
  settle_ = std::make_unique<ValueMotion>(
      CopyTop(), base::BindRepeating(&PlaceCard, base::Unretained(copy_.get()),
                                     base::Unretained(rows_.get()),
                                     drag_->list.copy_height));
  settle_->MoveTo(DraggedRows().front()->y(), motion::Slide(), compositor());
  drop_.Start(FROM_HERE, kDropAfter, this, &VerticalReorder::Drop);
}

void VerticalReorder::Drop() {
  const List list = drag_->list;
  const int from = drag_->start;
  const int to = drag_->target;
  std::vector<views::View*> dragged = DraggedRows();
  // The order kept, as the model takes it: the rows' own order is then the
  // one they stand in, and none moves.
  if (from != to) {
    committing_ = true;
    callbacks_.move.Run(list, from, to);
    committing_ = false;
  }
  rows_->SetOrder({}, /*animated=*/true);
  drag_.reset();

  // The copy goes as the row comes back to 100%, on the drop's curve, the
  // copy still settling as it fades; the row's hover returns with it.
  fading_ = std::exchange(copy_, nullptr);
  fading_settle_ = std::move(settle_);
  fading_rows_.assign(dragged.begin(), dragged.end());
  for (views::View* row : dragged) {
    if (auto* button = views::AsViewClass<RowButton>(row)) {
      button->SetDragged(false);
    }
  }
  fade_ = std::make_unique<ValueMotion>(
      0, base::BindRepeating(&VerticalReorder::Fade, base::Unretained(this)));
  fade_->MoveTo(1, motion::Drop(), compositor());
}

void VerticalReorder::Cancel() {
  drop_.Stop();
  const std::vector<views::View*> dragged = DraggedRows();
  for (views::View* row : dragged) {
    if (row->layer()) {
      row->layer()->SetOpacity(1);
    }
    if (auto* button = views::AsViewClass<RowButton>(row)) {
      button->SetDragged(false);
    }
  }
  settle_.reset();
  if (copy_) {
    contents_->RemoveChildViewT(std::exchange(copy_, nullptr).get());
  }
  rows_->SetOrder({}, /*animated=*/false);
  drag_.reset();
  rows_->EndSliding();
}

double VerticalReorder::CopyTop() const {
  return drag_->first_top +
         std::clamp(drag_->pointer - drag_->grab - drag_->first_top, 0.0,
                    std::max(drag_->height - drag_->list.copy_height, 0.0));
}

std::vector<views::View*> VerticalReorder::DrawnOrder() const {
  // The list's rows in the order the drop would leave them...
  std::vector<views::View*> moved;
  for (const std::string& id :
       model::ReorderedIds(drag_->list.ids, drag_->id, drag_->target)) {
    const auto index = std::ranges::find(drag_->list.ids, id);
    const auto& rows = drag_->list.rows[index - drag_->list.ids.begin()];
    moved.insert(moved.end(), rows.begin(), rows.end());
  }
  // ...each standing where one of them stood, among the others.
  const std::set<views::View*> in_list(moved.begin(), moved.end());
  std::vector<views::View*> order;
  size_t next = 0;
  for (views::View* row : rows_->children()) {
    order.push_back(in_list.contains(row) ? moved[next++] : row);
  }
  return order;
}

std::vector<views::View*> VerticalReorder::DraggedRows() const {
  if (!drag_) {
    return {};
  }
  const auto& rows = drag_->list.rows[drag_->start];
  return std::vector<views::View*>(rows.begin(), rows.end());
}

void VerticalReorder::Fade(double share) {
  for (views::View* row : fading_rows_) {
    if (row->layer()) {
      row->layer()->SetOpacity(kDraggedOpacity + (1 - kDraggedOpacity) * share);
    }
  }
  if (fading_ && fading_->layer()) {
    fading_->layer()->SetOpacity(1 - share);
  }
  if (fade_ && !fade_->is_moving()) {
    // Its own step is on the stack.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&VerticalReorder::FinishFade,
                                  weak_factory_.GetWeakPtr()));
  }
}

void VerticalReorder::FinishFade() {
  if (!fade_) {
    return;
  }
  for (views::View* row : fading_rows_) {
    if (row->layer()) {
      row->layer()->SetOpacity(1);
    }
  }
  fading_rows_.clear();
  fade_.reset();
  fading_settle_.reset();
  if (fading_) {
    contents_->RemoveChildViewT(std::exchange(fading_, nullptr).get());
  }
  // Once every row has settled, they paint into the list again.
  rows_->EndSliding();
}

ui::Compositor* VerticalReorder::compositor() const {
  views::Widget* widget = rows_->GetWidget();
  return widget ? widget->GetCompositor() : nullptr;
}

}  // namespace pane
