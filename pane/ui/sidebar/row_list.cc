// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/row_list.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/design/motion.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/widget/widget.h"

namespace pane {

// A row that may slide: where it stands, moving toward its place on
// Motion.slide, its layer drawn that far from the place, and whether the
// layer is the list's to take away once the drag is done.
class RowList::Slide {
 public:
  Slide(views::View* row, bool made_layer, base::RepeatingClosure settled)
      : row_(row),
        made_layer_(made_layer),
        top_(row->y(),
             base::BindRepeating(&Slide::Step, base::Unretained(this))),
        settled_(std::move(settled)) {}
  Slide(const Slide&) = delete;
  Slide& operator=(const Slide&) = delete;
  ~Slide() = default;

  // The row's place moved: it slides there from where it stands, keeping the
  // speed it had (a spring merging into one), or jumps without `compositor`.
  void MoveTo(int top, ui::Compositor* compositor) {
    // Drawn where it stood until the slide's first frame.
    Offset();
    top_.MoveTo(top, motion::Slide(), compositor);
  }

  // Back to its place, at once.
  void Land() {
    top_.MoveTo(row_->y(), motion::Slide(), /*compositor=*/nullptr);
  }

  bool is_moving() const { return top_.is_moving(); }
  bool made_layer() const { return made_layer_; }
  views::View* row() const { return row_; }

 private:
  void Step(double top) {
    Offset();
    if (!top_.is_moving()) {
      settled_.Run();
    }
  }

  void Offset() {
    if (ui::Layer* layer = row_->layer()) {
      layer->SetTransform(
          gfx::Transform::MakeTranslation(0, top_.value() - row_->y()));
    }
  }

  const raw_ptr<views::View> row_;
  const bool made_layer_;
  ValueMotion top_;
  base::RepeatingClosure settled_;
};

RowList::RowList() = default;
RowList::~RowList() = default;

void RowList::RowsChanged() {
  PreferredSizeChanged();
}

int RowList::HeightOfRows() const {
  int height = 2 * kEnds;
  for (const views::View* row : children()) {
    height += row->GetPreferredSize({}).height();
  }
  return height + kSpacing * std::max(int(children().size()) - 1, 0);
}

void RowList::SetOrder(std::vector<views::View*> order, bool animated) {
  std::vector<raw_ptr<views::View>> stands(order.begin(), order.end());
  if (!order.empty() && order.size() != children().size()) {
    stands.clear();
  }
  if (stands == order_) {
    return;
  }
  order_ = std::move(stands);
  // Drawn in the order they stand: the rows that move are on layers of their
  // own, so their layers' order is all that changes.
  ReorderLayers();
  PlaceRows(animated);
}

void RowList::SetSliding(const std::vector<views::View*>& rows) {
  ending_ = false;
  for (views::View* row : rows) {
    if (slides_.contains(row)) {
      continue;
    }
    const bool made_layer = !row->layer();
    if (made_layer) {
      row->SetPaintToLayer();
      if (row->layer()) {
        row->layer()->SetFillsBoundsOpaquely(false);
      }
    }
    slides_[row] = std::make_unique<Slide>(
        row, made_layer,
        base::BindRepeating(&RowList::Settled, base::Unretained(this)));
    sliding_.AddObservation(row);
  }
}

void RowList::EndSliding() {
  ending_ = true;
  Settled();
}

bool RowList::IsSliding() const {
  return std::ranges::any_of(
      slides_, [](const auto& slide) { return slide.second->is_moving(); });
}

gfx::Size RowList::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(width()), HeightOfRows());
}

void RowList::Layout(PassKey) {
  PlaceRows(/*animated=*/false);
}

views::View::Views RowList::GetChildrenInZOrder() {
  const std::vector<views::View*> order = Order();
  return Views(order.begin(), order.end());
}

void RowList::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  // Rows came or went: they stand in their own order again.
  if (details.parent == this) {
    order_.clear();
  }
}

void RowList::OnViewIsDeleting(views::View* view) {
  sliding_.RemoveObservation(view);
  slides_.erase(view);
}

std::vector<views::View*> RowList::Order() const {
  if (order_.empty()) {
    return std::vector<views::View*>(children().begin(), children().end());
  }
  return std::vector<views::View*>(order_.begin(), order_.end());
}

void RowList::PlaceRows(bool animated) {
  views::Widget* widget = GetWidget();
  ui::Compositor* compositor =
      animated && widget ? widget->GetCompositor() : nullptr;
  int y = kEnds;
  for (views::View* row : Order()) {
    const int height = row->GetPreferredSize({}).height();
    const gfx::Rect place(kInset, y, std::max(width() - 2 * kInset, 0), height);
    const bool moved = row->y() != place.y();
    row->SetBoundsRect(place);
    if (const auto slide = slides_.find(row); moved && slide != slides_.end()) {
      slide->second->MoveTo(place.y(), compositor);
    }
    y += height + kSpacing;
  }
}

void RowList::Settled() {
  if (!ending_ || IsSliding()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&RowList::TakeLayers, weak_factory_.GetWeakPtr()));
}

void RowList::TakeLayers() {
  if (!ending_ || IsSliding()) {
    return;
  }
  ending_ = false;
  for (const auto& [row, slide] : slides_) {
    slide->Land();
    sliding_.RemoveObservation(slide->row());
    if (slide->made_layer()) {
      slide->row()->DestroyLayer();
    }
  }
  slides_.clear();
}

BEGIN_METADATA(RowList)
END_METADATA

}  // namespace pane
