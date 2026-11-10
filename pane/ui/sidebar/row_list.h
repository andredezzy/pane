// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_ROW_LIST_H_
#define PANE_UI_SIDEBAR_ROW_LIST_H_

#include <map>
#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace pane {

// The sidebar's rows (H2), stacked 4 apart, 8 in from its sides and 4 from
// its ends, each where its order puts it: their own, or the one a drag draws
// them in, the dragged row at the place it would drop (H3, ADR 0066). The
// rows a drag reorders paint to layers of their own while it runs, and one
// whose place changes slides there from where it stands, by its layer's
// transform, on Motion.slide: it moves as one, its favicon and title
// together (the Swift's geometry group), and no frame repaints. They are
// drawn in the order they stand, so a row crossing another passes over it as
// the Swift's later row did.
class RowList : public views::View, public views::ViewObserver {
  METADATA_HEADER(RowList, views::View)

 public:
  static constexpr int kInset = 8;
  static constexpr int kEnds = 4;
  static constexpr int kSpacing = 4;

  RowList();
  RowList(const RowList&) = delete;
  RowList& operator=(const RowList&) = delete;
  ~RowList() override;

  // Rows came or went: the list's height changes.
  void RowsChanged();
  int HeightOfRows() const;

  // The rows in the order they stand, each row once; empty, their own. A row
  // whose place changes slides there if `animated` and it may (SetSliding),
  // and otherwise jumps.
  void SetOrder(std::vector<views::View*> order, bool animated);
  // `rows` may slide, each on a layer of its own, until EndSliding: then,
  // once every slide has settled, they paint into the list again.
  void SetSliding(const std::vector<views::View*>& rows);
  void EndSliding();
  // Whether a row is on its way to its place.
  bool IsSliding() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  Views GetChildrenInZOrder() override;
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override;

  // views::ViewObserver:
  void OnViewIsDeleting(views::View* view) override;

 private:
  class Slide;

  // The rows in the order they stand.
  std::vector<views::View*> Order() const;
  // Each row to its place, sliding there when `animated` and it may.
  void PlaceRows(bool animated);
  // A slide settled: once none runs and the drag is done, the layers go, on
  // the next turn (a slide's own step is on the stack).
  void Settled();
  void TakeLayers();

  std::vector<raw_ptr<views::View>> order_;
  std::map<const views::View*, std::unique_ptr<Slide>> slides_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver> sliding_{
      this};
  bool ending_ = false;
  base::WeakPtrFactory<RowList> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_ROW_LIST_H_
