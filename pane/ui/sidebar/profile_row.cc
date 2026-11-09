// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/profile_row.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/menu_icon.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// 8 in, its parts 8 apart (the Swift's HStack).
constexpr int kInset = 8;
constexpr int kSpacing = 8;
// The bar: 4 by 16 points, rounded 2; shown, a 16-point square rounded 4.
constexpr int kMarkHeight = 16;
constexpr float kBarWidth = 4;
constexpr float kSquareWidth = 16;
// The proxy's dot: 6 points across.
constexpr float kDot = 6;
// The row's tint of its color while its page shows.
constexpr float kTint = 0.04f;

// A profile's name: 12 points, medium (ADR 0060); its count: 12, regular.
constexpr TextStyle kName{12, weight::kMedium, 16};

using ProxyStatus = model::BrowserModel::ProxyStatus;

ui::ColorId DotColor(ProxyStatus status) {
  switch (status) {
    case ProxyStatus::kTesting:
      return kPaneColorWarning;
    case ProxyStatus::kConnected:
      return kPaneColorSuccess;
    case ProxyStatus::kFailed:
      return kPaneColorFailure;
  }
}

std::u16string_view StatusName(ProxyStatus status) {
  switch (status) {
    case ProxyStatus::kTesting:
      return u"testing";
    case ProxyStatus::kConnected:
      return u"connected";
    case ProxyStatus::kFailed:
      return u"failed";
  }
}

// What the row reads as besides its name (ADR 0071): "3 tabs, collapsed,
// proxy connected".
std::u16string AccessibleValue(const SidebarProfile& profile) {
  std::u16string value =
      base::StrCat({base::NumberToString16(profile.tab_count),
                    profile.tab_count == 1 ? u" tab, " : u" tabs, ",
                    profile.expanded ? u"expanded" : u"collapsed"});
  if (profile.proxy) {
    base::StrAppend(&value, {u", proxy ", StatusName(*profile.proxy)});
  }
  return value;
}

// A sidebar row's, with its text its own; while its page shows, no fill, and
// none under the pointer.
ControlLook LookOf(const SidebarProfile& profile) {
  if (!profile.shown) {
    return RowLook(/*selected=*/false, /*tints_on_hover=*/false);
  }
  ControlLook look(ControlShape::Squircle(radius::kLg));
  look.disabled_opacity = 1;
  return look;
}

}  // namespace

ProfileRow::Actions::Actions() = default;
ProfileRow::Actions::Actions(Actions&&) = default;
ProfileRow::Actions& ProfileRow::Actions::operator=(Actions&&) = default;
ProfileRow::Actions::~Actions() = default;

ProfileRow::ProfileRow(const SidebarProfile& profile, Actions actions)
    : RowButton(actions.toggle),
      actions_(std::move(actions)),
      morph_(
          profile.shown ? 1 : 0,
          base::BindRepeating(&ProfileRow::Morphed, base::Unretained(this))) {
  SetID(kViewIdProfileRow);
  set_context_menu_controller(this);
  name_ = AddChildView(MakeLabel(u"", kName, kPaneColorMutedForeground));
  name_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name_->SetCanProcessEventsWithinSubtree(false);
  count_ =
      AddChildView(MakeLabel(u"", text::kSidebar, kPaneColorMutedForeground));
  count_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  count_->SetCanProcessEventsWithinSubtree(false);

  // The Electron app's menu, with its Lucide icons.
  menu_model_.AddItemWithIcon(kEditProfile, u"Edit profile",
                              MenuIcon(Icon::kPencil));
  menu_model_.AddCheckItem(kKeepLoaded, u"Keep loaded");
  menu_model_.SetIcon(menu_model_.GetIndexOfCommandId(kKeepLoaded).value(),
                      MenuIcon(Icon::kTimerOff));
  menu_model_.AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_.AddItemWithIcon(kDeleteProfile, u"Delete profile",
                              MenuIcon(Icon::kTrash2));

  profile_.shown = profile.shown;
  SetLook(LookOf(profile));
  Update(profile);
}

ProfileRow::~ProfileRow() = default;

void ProfileRow::Update(const SidebarProfile& profile) {
  // A refresh that changed another row, or only this profile's tabs, leaves
  // its header alone.
  if (!profile_.id.empty() && profile.id == profile_.id &&
      profile.name == profile_.name && profile.color == profile_.color &&
      profile.shown == profile_.shown &&
      profile.expanded == profile_.expanded &&
      profile.proxy == profile_.proxy &&
      profile.tab_count == profile_.tab_count &&
      profile.keep_loaded == profile_.keep_loaded) {
    return;
  }
  const bool morphs = profile.shown != profile_.shown;
  const bool counts = profile.expanded != profile_.expanded ||
                      profile.tab_count != profile_.tab_count ||
                      profile.proxy != profile_.proxy;
  profile_ = profile;
  // Its tabs are their own rows.
  profile_.tabs.clear();

  if (name_->GetText() != profile.name) {
    name_->SetText(profile.name);
  }
  // The count while collapsed, of a profile with tabs.
  const bool shows_count = !profile.expanded && profile.tab_count > 0;
  count_->SetVisible(shows_count);
  if (shows_count) {
    count_->SetText(base::NumberToString16(profile.tab_count));
  }
  GetViewAccessibility().SetName(profile.name);
  GetViewAccessibility().SetValue(AccessibleValue(profile));
  GetViewAccessibility().SetIsSelected(profile.shown);

  if (morphs) {
    SetLook(LookOf(profile));
    morph_.MoveTo(profile.shown ? 1 : 0, motion::Morph(),
                  GetWidget() ? GetWidget()->GetCompositor() : nullptr);
  }
  if (counts) {
    InvalidateLayout();
  }
  SchedulePaint();
}

float ProfileRow::MarkWidth() const {
  return kBarWidth + (kSquareWidth - kBarWidth) * morph_.value();
}

double ProfileRow::Shown() const {
  return std::clamp(morph_.value(), 0.0, 1.0);
}

gfx::Size ProfileRow::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kHeight);
}

void ProfileRow::Layout(PassKey) {
  int end = width() - kInset;
  if (count_->GetVisible()) {
    const int count_width = count_->GetPreferredSize().width();
    count_->SetBounds(end - count_width, 0, count_width, height());
    end -= count_width + kSpacing;
  }
  if (profile_.proxy) {
    dot_x_ = end - kDot;
    end -= kDot + kSpacing;
  }
  const int start = std::round(kInset + MarkWidth() + kSpacing);
  name_->SetBounds(start, 0, std::max(end - start, 0), height());
}

void ProfileRow::PaintButtonContents(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  // The tint of its color, under its content.
  if (const double shown = Shown(); shown > 0) {
    flags.setColor(SkColorSetA(
        profile_.color, static_cast<U8CPU>(std::lround(255 * kTint * shown))));
    canvas->DrawPath(SquirclePath(gfx::RectF(GetLocalBounds()), radius::kLg),
                     flags);
  }
  // The bar, rounding off as it widens: 2 to 4.
  flags.setColor(profile_.color);
  canvas->DrawPath(
      SquirclePath(gfx::RectF(kInset, (height() - kMarkHeight) / 2.0f,
                              MarkWidth(), kMarkHeight),
                   2 + 2 * static_cast<float>(morph_.value())),
      flags);
  // The proxy's dot, in its status's color, the same in both appearances.
  if (const ui::ColorProvider* colors = GetColorProvider();
      colors && profile_.proxy) {
    flags.setColor(colors->GetColor(DotColor(*profile_.proxy)));
    canvas->DrawCircle(gfx::PointF(dot_x_ + kDot / 2, height() / 2.0f),
                       kDot / 2, flags);
  }
}

void ProfileRow::LookChanged() {
  // Its text follows the profile, not the hover: muted, darkening as its bar
  // grows.
  if (const ui::ColorProvider* colors = GetColorProvider()) {
    name_->SetEnabledColor(Mix(colors->GetColor(kPaneColorMutedForeground),
                               colors->GetColor(kPaneColorAccentForeground),
                               Shown()));
  }
  RowButton::LookChanged();
}

void ProfileRow::Morphed(double value) {
  // The name moves along with the bar.
  InvalidateLayout();
  LookChanged();
}

void ProfileRow::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!GetWidget()) {
    return;
  }
  menu_runner_ = std::make_unique<views::MenuRunner>(
      &menu_model_, views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

bool ProfileRow::IsCommandIdChecked(int command_id) const {
  return command_id == kKeepLoaded && profile_.keep_loaded;
}

void ProfileRow::ExecuteCommand(int command_id, int event_flags) {
  // Once the menu has gone: an alert shows, or the profile's row changes.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<ProfileRow> row, int command_id) {
            if (!row) {
              return;
            }
            switch (command_id) {
              case kEditProfile:
                row->actions_.edit.Run();
                break;
              case kKeepLoaded:
                row->actions_.set_keep_loaded.Run(!row->profile_.keep_loaded);
                break;
              case kDeleteProfile:
                row->actions_.ask_delete.Run();
                break;
            }
          },
          weak_factory_.GetWeakPtr(), command_id));
}

BEGIN_METADATA(ProfileRow)
END_METADATA

}  // namespace pane
