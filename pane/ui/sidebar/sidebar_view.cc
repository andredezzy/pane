// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/sidebar_view.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/scroll_fade.h"
#include "pane/ui/design/text.h"
#include "pane/ui/dialogs/system_alert.h"
#include "pane/ui/sidebar/drag_copy.h"
#include "pane/ui/sidebar/profile_row.h"
#include "pane/ui/sidebar/row_list.h"
#include "pane/ui/sidebar/sidebar_button.h"
#include "pane/ui/sidebar/tab_row.h"
#include "pane/ui/sidebar/update_pill.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The header: the traffic lights' row, 39 points, its title 82 in, after
// them, and Collapse all, a 28-point circle, 8 from the column's edge, as the
// rows below. The title is centered a point above the row's bottom, on the
// lights' line (ADR 0036).
constexpr int kHeaderHeight = 39;
constexpr int kHeaderLeading = 82;
constexpr int kHeaderTrailing = 8;
constexpr int kHeaderBottom = 1;
constexpr int kCollapseAllSize = 28;
constexpr TextStyle kTitle{12, weight::kSemibold, 0};

// The footer (px-2 pb-2): the update pill while it shows, New, a rule,
// Settings, 6 apart.
constexpr int kFooterInset = 8;
constexpr int kFooterSpacing = 6;
constexpr int kRuleHeight = 1;
constexpr int kFooterHeight = RowButton::kHeight + kFooterSpacing +
                              kRuleHeight + kFooterSpacing +
                              RowButton::kHeight + kFooterInset;

std::string ProfileKey(const std::string& id) {
  return "p:" + id;
}
std::string TabKey(const std::string& id) {
  return "t:" + id;
}
std::string NewTabKey(const std::string& profile_id) {
  return "n:" + profile_id;
}

}  // namespace

// The list's contents: its rows from their top left, as tall as they are,
// and a drag's copy over them (H3), which rides in the rows' coordinates.
class SidebarView::ListContents : public views::View {
  METADATA_HEADER(ListContents, views::View)

 public:
  explicit ListContents(RowList* rows) : rows_(rows) {}

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return rows_->GetPreferredSize(available_size);
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    if (child == rows_) {
      PreferredSizeChanged();
    }
  }

  void Layout(PassKey) override { rows_->SetBoundsRect(GetLocalBounds()); }

 private:
  const raw_ptr<RowList> rows_;
};

BEGIN_METADATA(SidebarView, ListContents)
END_METADATA

SidebarView::Callbacks::Callbacks() = default;
SidebarView::Callbacks::Callbacks(Callbacks&&) = default;
SidebarView::Callbacks& SidebarView::Callbacks::operator=(Callbacks&&) =
    default;
SidebarView::Callbacks::~Callbacks() = default;

SidebarView::SidebarView(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  SetID(kViewIdSidebar);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Sidebar");

  header_ = AddChildView(std::make_unique<views::View>());
  header_->SetID(kViewIdSidebarHeader);
  title_ = header_->AddChildView(
      MakeLabel(u"Pane", kTitle, kPaneColorMutedForeground));
  // The title moves the window, as the rest of the header does.
  title_->SetCanProcessEventsWithinSubtree(false);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  collapse_all_ = header_->AddChildView(std::make_unique<IconButton>(
      callbacks_.collapse_all, kCollapseAllSize,
      IconButton::Glyph{Icon::kChevronsDownUp, 16}, u"Collapse all profiles"));
  collapse_all_->SetID(kViewIdCollapseAll);
  collapse_all_->SetEnabled(false);

  // The list fades at both edges, nothing there to mark either (DESIGN.md ›
  // Scroll edges), over the window's own fill.
  list_ = AddChildView(std::make_unique<FadingScrollView>(
      kPaneColorWindowBackground, /*fades_top=*/true, /*fades_bottom=*/true));
  list_->SetID(kViewIdSidebarProfiles);
  list_->scroll_view()->SetUseContentsPreferredSize(true);
  auto rows = std::make_unique<RowList>();
  rows_ = rows.get();
  auto contents = std::make_unique<ListContents>(rows_);
  contents->AddChildView(std::move(rows));
  contents_ = list_->scroll_view()->SetContents(std::move(contents));

  // Profiles and tabs reorder by dragging, as the Electron app's did: the
  // reorder watches the rows' presses ahead of them.
  VerticalReorder::Callbacks reorder;
  reorder.list_at =
      base::BindRepeating(&SidebarView::ListAt, base::Unretained(this));
  reorder.fill_copy =
      base::BindRepeating(&SidebarView::FillCopy, base::Unretained(this));
  reorder.move =
      base::BindRepeating(&SidebarView::MoveRows, base::Unretained(this));
  reorder_ =
      std::make_unique<VerticalReorder>(rows_, contents_, std::move(reorder));
  rows_->AddPreTargetHandler(reorder_.get());

  footer_ = AddChildView(std::make_unique<views::View>());
  pill_ = footer_->AddChildView(std::make_unique<UpdatePill>(
      callbacks_.update, callbacks_.dismiss_update));
  pill_->SetVisible(false);
  new_ = footer_->AddChildView(std::make_unique<SidebarButton>(
      callbacks_.new_profile, SidebarButton::Mark::kPlus, u"New"));
  new_->SetID(kViewIdSidebarNew);
  // New keeps its muted text under the pointer.
  new_->SetTintsOnHover(false);
  rule_ = footer_->AddChildView(std::make_unique<views::View>());
  rule_->SetBackground(views::CreateSolidBackground(kPaneColorSidebarRule));
  settings_ = footer_->AddChildView(std::make_unique<SidebarButton>(
      callbacks_.toggle_settings, SidebarButton::Mark::kSettings, u"Settings"));
  settings_->SetID(kViewIdSidebarSettings);
}

SidebarView::~SidebarView() {
  // The reorder goes before the rows it watches.
  rows_->RemovePreTargetHandler(reorder_.get());
  reorder_.reset();
}

void SidebarView::SetContent(const SidebarContent& content) {
  // Before the rows, which a drag's copy reads as they change.
  content_ = content;
  SetRows(content);
  SetUpdate(content.update);
  settings_->SetSelected(content.settings_shown);
  settings_->GetViewAccessibility().SetIsSelected(content.settings_shown);

  // A newly selected tab, wherever it was chosen, comes into view; at once
  // the first time.
  std::optional<std::string> selected;
  for (const SidebarProfile& profile : content.profiles) {
    for (const SidebarTab& tab : profile.tabs) {
      if (tab.selected) {
        selected = tab.id;
      }
    }
  }
  if (selected && selected != selected_tab_) {
    const bool animated = revealed_once_;
    revealed_once_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SidebarView::RevealSelected,
                                  weak_factory_.GetWeakPtr(), animated));
  }
  selected_tab_ = std::move(selected);
}

void SidebarView::SetUpdate(const std::optional<SidebarUpdate>& update) {
  if (update) {
    pill_->Set(*update);
    if (!pill_->shown()) {
      pill_->SetVisible(true);
      pill_->Show(true, base::OnceClosure());
      InvalidateLayout();
    }
    return;
  }
  if (pill_->shown()) {
    pill_->Show(false, base::BindOnce(
                           [](SidebarView* sidebar) {
                             sidebar->pill_->SetVisible(false);
                             sidebar->InvalidateLayout();
                           },
                           base::Unretained(this)));
  }
}

int SidebarView::FooterHeight() const {
  return kFooterHeight +
         (pill_->GetVisible() ? UpdatePill::kHeight + kFooterSpacing : 0);
}

void SidebarView::SetCanCollapseAll(bool can_collapse_all) {
  collapse_all_->SetEnabled(can_collapse_all);
}

void SidebarView::SetRows(const SidebarContent& content) {
  std::vector<std::string> keys;
  for (const SidebarProfile& profile : content.profiles) {
    keys.push_back(ProfileKey(profile.id));
    if (!profile.expanded) {
      continue;
    }
    for (const SidebarTab& tab : profile.tabs) {
      keys.push_back(TabKey(tab.id));
    }
    keys.push_back(NewTabKey(profile.id));
  }

  // Rows came, went, or moved: each keeps its view, found by its key. A drag
  // under way ends first, unless this is its own drop.
  if (keys != keys_) {
    reorder_->RowsChanging();
    std::map<std::string, std::unique_ptr<views::View>> kept;
    for (const std::string& key : keys_) {
      kept[key] = rows_->RemoveChildViewT(rows_->children().front());
    }
    size_t index = 0;
    for (const SidebarProfile& profile : content.profiles) {
      std::unique_ptr<views::View>& header = kept[keys[index++]];
      rows_->AddChildView(header ? std::move(header)
                                 : MakeRow(keys[index - 1], profile, nullptr));
      if (!profile.expanded) {
        continue;
      }
      for (const SidebarTab& tab : profile.tabs) {
        std::unique_ptr<views::View>& row = kept[keys[index++]];
        rows_->AddChildView(row ? std::move(row)
                                : MakeRow(keys[index - 1], profile, &tab));
      }
      std::unique_ptr<views::View>& new_tab = kept[keys[index++]];
      rows_->AddChildView(new_tab ? std::move(new_tab)
                                  : MakeRow(keys[index - 1], profile, nullptr));
    }
    keys_ = std::move(keys);
    rows_->RowsChanged();
  }

  // Each row hears its own value; one that didn't change does nothing.
  size_t index = 0;
  for (const SidebarProfile& profile : content.profiles) {
    static_cast<ProfileRow*>(rows_->children()[index++])->Update(profile);
    if (!profile.expanded) {
      continue;
    }
    for (const SidebarTab& tab : profile.tabs) {
      static_cast<TabRow*>(rows_->children()[index++])->Update(tab);
    }
    ++index;
  }
  // A drag's copy shows its row as it is now.
  reorder_->RowsChanged();
}

std::unique_ptr<views::View> SidebarView::MakeRow(const std::string& key,
                                                  const SidebarProfile& profile,
                                                  const SidebarTab* tab) {
  // A release that ends a drag is no click, on the row or its X.
  const base::RepeatingClosure capture_lost = base::BindRepeating(
      &VerticalReorder::CaptureLost, reorder_->GetWeakPtr());
  if (tab) {
    TabRow::Actions actions;
    actions.select =
        UnlessDragging(base::BindRepeating(callbacks_.select_tab, tab->id));
    actions.close =
        UnlessDragging(base::BindRepeating(callbacks_.close_tab, tab->id));
    auto row = std::make_unique<TabRow>(*tab, std::move(actions));
    row->SetCaptureLost(capture_lost);
    return row;
  }
  if (key == NewTabKey(profile.id)) {
    auto row = std::make_unique<SidebarButton>(
        base::BindRepeating(callbacks_.new_tab, profile.id),
        SidebarButton::Mark::kPlus, u"New tab");
    row->SetID(kViewIdNewTabRow);
    return row;
  }
  ProfileRow::Actions actions;
  actions.toggle = UnlessDragging(
      base::BindRepeating(callbacks_.toggle_profile, profile.id));
  actions.edit = base::BindRepeating(callbacks_.edit_profile, profile.id);
  actions.set_keep_loaded =
      base::BindRepeating(callbacks_.set_keep_loaded, profile.id);
  actions.ask_delete = base::BindRepeating(
      &SidebarView::AskToDelete, weak_factory_.GetWeakPtr(), profile.id);
  auto row = std::make_unique<ProfileRow>(profile, std::move(actions));
  row->SetCaptureLost(capture_lost);
  return row;
}

base::RepeatingClosure SidebarView::UnlessDragging(
    base::RepeatingClosure action) {
  return base::BindRepeating(
      [](base::WeakPtr<SidebarView> sidebar,
         const base::RepeatingClosure& action) {
        if (sidebar && !sidebar->reorder_->is_dragging()) {
          action.Run();
        }
      },
      weak_factory_.GetWeakPtr(), std::move(action));
}

std::optional<VerticalReorder::List> SidebarView::ListAt(views::View* row) {
  // The rows by their keys, in the model's order.
  std::map<std::string, views::View*> rows;
  for (size_t index = 0; index < keys_.size(); ++index) {
    rows[keys_[index]] = rows_->children()[index];
  }
  VerticalReorder::List list;
  if (row->GetID() == kViewIdProfileRow) {
    // Each profile with its rows: its header, and while it is open its tabs
    // and New tab.
    list.copy_height = ProfileRow::kHeight;
    for (const SidebarProfile& profile : content_.profiles) {
      std::vector<raw_ptr<views::View>> group = {rows[ProfileKey(profile.id)]};
      if (profile.expanded) {
        for (const SidebarTab& tab : profile.tabs) {
          group.push_back(rows[TabKey(tab.id)]);
        }
        group.push_back(rows[NewTabKey(profile.id)]);
      }
      list.ids.push_back(profile.id);
      list.rows.push_back(std::move(group));
    }
    return list;
  }
  if (row->GetID() != kViewIdTabRow) {
    return std::nullopt;
  }
  // The tabs of the row's profile, among themselves.
  const std::string& tab_id = static_cast<TabRow*>(row)->tab().id;
  for (const SidebarProfile& profile : content_.profiles) {
    if (std::ranges::find(profile.tabs, tab_id, &SidebarTab::id) ==
        profile.tabs.end()) {
      continue;
    }
    list.profile_id = profile.id;
    list.copy_height = RowButton::kHeight;
    for (const SidebarTab& tab : profile.tabs) {
      list.ids.push_back(tab.id);
      list.rows.push_back({rows[TabKey(tab.id)]});
    }
    return list;
  }
  return std::nullopt;
}

void SidebarView::FillCopy(DragCopy& copy,
                           const VerticalReorder::List& list,
                           const std::string& id) {
  for (const SidebarProfile& profile : content_.profiles) {
    if (list.profile_id.empty() && profile.id == id) {
      copy.SetProfile(profile);
      return;
    }
    for (const SidebarTab& tab : profile.tabs) {
      if (!list.profile_id.empty() && tab.id == id) {
        copy.SetTab(tab);
        return;
      }
    }
  }
}

void SidebarView::MoveRows(const VerticalReorder::List& list,
                           int from,
                           int to) {
  if (list.profile_id.empty()) {
    callbacks_.move_profile.Run(from, to);
  } else {
    callbacks_.move_tab.Run(list.profile_id, from, to);
  }
}

void SidebarView::AskToDelete(const std::string& profile_id) {
  const auto profile =
      std::ranges::find(content_.profiles, profile_id, &SidebarProfile::id);
  if (profile == content_.profiles.end() || !GetWidget()) {
    return;
  }
  SystemAlert alert;
  alert.title = base::StrCat({u"Delete “", profile->name, u"”?"});
  alert.message = u"This action cannot be undone.";
  alert.action = u"Delete";
  alert.cancel = u"Cancel";
  ShowSystemAlert(GetWidget(), alert,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarView> sidebar,
                         std::string profile_id, bool confirmed) {
                        if (sidebar && confirmed) {
                          sidebar->callbacks_.delete_profile.Run(profile_id);
                        }
                      },
                      weak_factory_.GetWeakPtr(), profile_id));
}

std::vector<std::pair<std::string, const views::View*>>
SidebarView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named = {
      {"pane.sidebar", this},
      {"pane.sidebar.header", header_},
      {"pane.sidebar.collapseAll", collapse_all_},
      {"pane.sidebar.profiles", list_},
      {"pane.sidebar.new", new_},
      {"pane.sidebar.settings", settings_},
  };
  if (pill_->GetVisible()) {
    named.emplace_back("pane.sidebar.updatePill", pill_);
    named.emplace_back("pane.sidebar.update", pill_->button());
    named.emplace_back("pane.sidebar.update.dismiss", pill_->dismiss());
  }
  if (const DragCopy* copy = reorder_->copy()) {
    named.emplace_back("pane.sidebar.dragCopy", copy->card_view());
  }
  for (const views::View* row : rows_->children()) {
    if (row->GetID() == kViewIdProfileRow) {
      named.emplace_back(
          "pane.profile." + static_cast<const ProfileRow*>(row)->profile().id,
          row);
    } else if (row->GetID() == kViewIdTabRow) {
      named.emplace_back(
          "pane.tab." + static_cast<const TabRow*>(row)->tab().id, row);
    }
  }
  return named;
}

ProfileRow* SidebarView::FindProfileRow(const std::string& profile_id) {
  for (views::View* row : rows_->children()) {
    if (row->GetID() == kViewIdProfileRow &&
        static_cast<ProfileRow*>(row)->profile().id == profile_id) {
      return static_cast<ProfileRow*>(row);
    }
  }
  return nullptr;
}

TabRow* SidebarView::FindTabRow(const std::string& tab_id) {
  for (views::View* row : rows_->children()) {
    if (row->GetID() == kViewIdTabRow &&
        static_cast<TabRow*>(row)->tab().id == tab_id) {
      return static_cast<TabRow*>(row);
    }
  }
  return nullptr;
}

int SidebarView::top_fade() const {
  return list_->top_fade();
}

int SidebarView::bottom_fade() const {
  return list_->bottom_fade();
}

void SidebarView::RevealSelected(bool animated) {
  TabRow* row = selected_tab_ ? FindTabRow(*selected_tab_) : nullptr;
  if (!row || !GetWidget()) {
    return;
  }
  // The rows laid out as they now stand, a profile that opened for the tab
  // included.
  GetWidget()->LayoutRootViewIfNecessary();
  views::ScrollView* scroll = list_->scroll_view();
  const gfx::Rect visible = scroll->GetVisibleRect();
  // The row and a fade's height around it, inside the list.
  const int top = std::max(row->y() - FadingScrollView::kFade, 0);
  const int bottom = std::min(row->bounds().bottom() + FadingScrollView::kFade,
                              rows_->height());
  double offset = visible.y();
  if (top < visible.y()) {
    offset = top;
  } else if (bottom > visible.bottom()) {
    offset = bottom - visible.height();
  }
  if (offset == visible.y()) {
    return;
  }
  ui::Compositor* compositor =
      animated ? GetWidget()->GetCompositor() : nullptr;
  reveal_ = std::make_unique<ValueMotion>(
      visible.y(),
      base::BindRepeating(&SidebarView::Revealed, base::Unretained(this)));
  revealed_offset_ = visible.y();
  reveal_->MoveTo(offset, motion::Reveal(), compositor);
}

void SidebarView::Revealed(double offset) {
  views::ScrollView* scroll = list_->scroll_view();
  // The user scrolled meanwhile: the list is theirs.
  if (std::abs(scroll->CurrentOffset().y() - revealed_offset_) > 1) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<SidebarView> sidebar, const ValueMotion* motion) {
              if (sidebar && sidebar->reveal_.get() == motion) {
                sidebar->reveal_.reset();
              }
            },
            weak_factory_.GetWeakPtr(), reveal_.get()));
    revealed_offset_ = scroll->CurrentOffset().y();
    return;
  }
  revealed_offset_ = std::round(offset);
  scroll->ScrollToOffset(gfx::PointF(0, revealed_offset_));
}

void SidebarView::Layout(PassKey) {
  header_->SetBounds(0, 0, width(), kHeaderHeight);
  const int header_row = kHeaderHeight - kHeaderBottom;
  collapse_all_->SetBounds(
      std::max(width() - kHeaderTrailing - kCollapseAllSize, 0),
      (header_row - kCollapseAllSize) / 2, kCollapseAllSize, kCollapseAllSize);
  title_->SetBounds(kHeaderLeading, 0,
                    std::max(collapse_all_->x() - 8 - kHeaderLeading, 0),
                    header_row);

  const int footer_height = FooterHeight();
  footer_->SetBounds(0, std::max(height() - footer_height, kHeaderHeight),
                     width(), footer_height);
  const int row_width = std::max(width() - 2 * kFooterInset, 0);
  int top = 0;
  if (pill_->GetVisible()) {
    pill_->SetBounds(kFooterInset, top, row_width, UpdatePill::kHeight);
    top = pill_->bounds().bottom() + kFooterSpacing;
  }
  new_->SetBounds(kFooterInset, top, row_width, RowButton::kHeight);
  rule_->SetBounds(kFooterInset, new_->bounds().bottom() + kFooterSpacing,
                   row_width, kRuleHeight);
  settings_->SetBounds(kFooterInset, rule_->bounds().bottom() + kFooterSpacing,
                       row_width, RowButton::kHeight);

  list_->SetBounds(0, kHeaderHeight, width(),
                   std::max(footer_->y() - kHeaderHeight, 0));
  // The list's rows at their height and the list's width, as the scroll view
  // without layers (Windows) takes them.
  contents_->SetSize(
      gfx::Size(width(), std::max(rows_->HeightOfRows(), list_->height())));
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace pane
