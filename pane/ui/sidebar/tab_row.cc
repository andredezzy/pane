// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/tab_row.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/focus_ring.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "pane/ui/tabs/tab_favicon.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/label.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The close X: Lucide's x at 14 points, 8 from the row's end.
constexpr int kCloseSize = 14;

// What a tab with no title yet shows, and reads as.
constexpr char16_t kLoading[] = u"Loading...";

}  // namespace

std::u16string TabTitle(const SidebarTab& tab) {
  return tab.title.empty() ? kLoading : tab.title;
}

// A tab's close X (the Swift's TabCloseButton), in the row's text color,
// shown as the row's hover fades in, and with no fill of its own.
class TabRow::CloseButton : public views::Button {
  METADATA_HEADER(CloseButton, views::Button)

 public:
  CloseButton(PressedCallback pressed, TabRow* row)
      : views::Button(std::move(pressed)), row_(row) {
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    SetInstallFocusRingOnFocus(false);
    FocusRing::Install(this, ControlShape::Circle());
    GetViewAccessibility().SetName(u"Close tab");
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const double shown = std::clamp(row_->hover(), 0.0, 1.0);
    const SkColor tint = row_->Tint();
    if (shown <= 0 || SkColorGetA(tint) == 0) {
      return;
    }
    PaintIcon(*canvas, Icon::kX, gfx::RectF(GetLocalBounds()), 2,
              SkColorSetA(tint, static_cast<U8CPU>(
                                    std::lround(SkColorGetA(tint) * shown))));
  }

 private:
  const raw_ptr<TabRow> row_;
};

BEGIN_METADATA(TabRow, CloseButton)
END_METADATA

TabRow::Actions::Actions() = default;
TabRow::Actions::Actions(Actions&&) = default;
TabRow::Actions& TabRow::Actions::operator=(Actions&&) = default;
TabRow::Actions::~Actions() = default;

TabRow::TabRow(const SidebarTab& tab, Actions actions)
    : RowButton(actions.select), actions_(std::move(actions)) {
  SetID(kViewIdTabRow);
  set_context_menu_controller(this);
  title_ =
      AddChildView(MakeLabel(u"", text::kSidebar, kPaneColorMutedForeground));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetCanProcessEventsWithinSubtree(false);
  close_ = AddChildView(std::make_unique<CloseButton>(
      base::BindRepeating(&TabRow::Close, base::Unretained(this)), this));
  MakeContextMenu(this).AddItem(kCloseTab, u"Close tab");
  Update(tab);
}

TabRow::~TabRow() = default;

void TabRow::Update(const SidebarTab& tab) {
  // A refresh that changed another row leaves this one alone.
  if (tab == tab_) {
    return;
  }
  tab_ = tab;
  const std::u16string title = TabTitle(tab);
  if (title_->GetText() != title) {
    title_->SetText(title);
  }
  GetViewAccessibility().SetName(tab.title.empty() ? u"Loading" : tab.title);
  GetViewAccessibility().SetIsSelected(tab.selected);
  // A selection shows at once; the dimming of a tab whose page went too.
  SetSelected(tab.selected);
  SetDimmed(!tab.loaded && !tab.selected);
  SchedulePaint();
}

gfx::Size TabRow::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kHeight);
}

void TabRow::Layout(PassKey) {
  const int close_x = std::max(width() - kTrailing - kCloseSize, 0);
  close_->SetBounds(close_x, (height() - kCloseSize) / 2, kCloseSize,
                    kCloseSize);
  const int start = kLeading + kIconSize + kIconSpacing;
  title_->SetBounds(start, 0, std::max(close_x - kIconSpacing - start, 0),
                    height());
}

void TabRow::PaintButtonContents(gfx::Canvas* canvas) {
  // The globe in the row's tint, which dims with its contents; a favicon at
  // the contents' opacity.
  PaintFavicon(
      *canvas,
      gfx::RectF(kLeading, (height() - kIconSize) / 2, kIconSize, kIconSize),
      tab_.favicon, FaviconPlaceholder::kGlobe, Tint(), ContentsOpacity());
}

void TabRow::LookChanged() {
  if (GetColorProvider()) {
    title_->SetEnabledColor(Tint());
  }
  close_->SchedulePaint();
  RowButton::LookChanged();
}

void TabRow::ShowContextMenuForViewImpl(views::View* source,
                                        const gfx::Point& point,
                                        ui::mojom::MenuSourceType source_type) {
  RunContextMenu(point, source_type);
}

void TabRow::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kCloseTab) {
    Close();
  }
}

void TabRow::Close() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabRow> row) {
                       if (row) {
                         row->actions_.close.Run();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

BEGIN_METADATA(TabRow)
END_METADATA

}  // namespace pane
