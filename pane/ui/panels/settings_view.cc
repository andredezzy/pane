// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/panels/settings_view.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/menu_button.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/view_ids.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The page: 40 in from the panel's sides and 32 from its top and bottom,
// "Settings" at 22 medium on a 28-point line, then each section, 28 apart;
// the sections at most 640 wide.
constexpr int kPaddingX = 40;
constexpr int kPaddingY = 32;
constexpr int kSpacing = 28;
constexpr int kMaxWidth = 640;
constexpr TextStyle kPageTitle{22, weight::kMedium, 28};
// A section's label, 13 on an 18-point line, 15 in, 10 over its card.
constexpr TextStyle kGroupLabel{13, weight::kRegular, 18};
constexpr int kGroupLabelInset = 15;
constexpr int kGroupLabelSpacing = 10;
// A card: rounded 22, its rows inside its 1-point border.
constexpr int kBorder = 1;
// A row: its labels 16 in, its control 11 from the card's edge, 16 between
// them, the controls 8 apart; 12 over and under, at least 56 tall. Its title
// on 18-point lines, 2 over its description on 16-point ones.
constexpr int kRowLeading = 16;
constexpr int kRowTrailing = 11;
constexpr int kRowSpacing = 16;
constexpr int kControlSpacing = 8;
constexpr int kRowPadding = 12;
constexpr int kRowMinHeight = 56;
constexpr int kTitleSpacing = 2;
// The version, 13 and muted, at the Updates card's end.
constexpr TextStyle kVersion{13, weight::kRegular, 18};
// The App icon row's control: the icon at 36 points, 2 in, its fill rounded
// 11.
constexpr int kAppIconInset = 2;

// Presets rather than a free field (resource-settings.tsx): the scheduler
// ticks once a minute, so finer values would be false precision; Off is the
// kill switch.
struct Preset {
  const char16_t* title;
  std::optional<int> value;
};
constexpr Preset kTabSleep[] = {{u"5 min", 5},
                                {u"15 min", 15},
                                {u"1 hour", 60},
                                {u"Off", std::nullopt}};
constexpr Preset kProfileUnload[] = {{u"15 min", 15},
                                     {u"30 min", 30},
                                     {u"2 hours", 120},
                                     {u"Off", std::nullopt}};
// Never still locks when the Mac sleeps or locks, and on Lock Pane.
constexpr Preset kLockAfter[] = {{u"1 min", 1},
                                 {u"5 min", 5},
                                 {u"15 min", 15},
                                 {u"1 hour", 60},
                                 {u"Never", std::nullopt}};
// Megabytes, per profile: no cache is shared.
constexpr Preset kCacheBudget[] = {{u"100 MB", 100},
                                   {u"300 MB", 300},
                                   {u"1 GB", 1024},
                                   {u"Off", std::nullopt}};

std::vector<MenuButton::Option> OptionsOf(base::span<const Preset> presets) {
  std::vector<MenuButton::Option> options;
  for (const Preset& preset : presets) {
    options.push_back({.title = preset.title});
  }
  return options;
}

std::optional<size_t> IndexOf(base::span<const Preset> presets,
                              std::optional<int> value) {
  for (size_t index = 0; index < presets.size(); ++index) {
    if (presets[index].value == value) {
      return index;
    }
  }
  return std::nullopt;
}

constexpr std::array<model::Theme, 3> kThemes = {
    model::Theme::kSystem, model::Theme::kLight, model::Theme::kDark};

// A card's border, drawn over its rows as SwiftUI's overlay draws it, so a
// hairline's ends meet it.
class CardBorder : public views::View {
  METADATA_HEADER(CardBorder, views::View)

 public:
  CardBorder() { SetCanProcessEventsWithinSubtree(false); }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kBorder);
    flags.setColor(colors->GetColor(kPaneColorBorder));
    // In pieces, so a tile inside the card rasters no path (ADR 0123).
    StrokeSquircleBorder(*canvas, gfx::RectF(GetLocalBounds()), radius::kXl,
                         kBorder, flags);
  }
};

BEGIN_METADATA(CardBorder)
END_METADATA

}  // namespace

// A row: its title, its description under it, and its controls at its end,
// with a hairline under it unless it is its card's last. A status that
// changes (the update row's) crossfades, as the update pill's does
// (Motion.status).
class SettingsView::Row : public views::View {
  METADATA_HEADER(Row, views::View)

 public:
  Row(const std::u16string& title, const std::u16string& description)
      : fade_(1, base::BindRepeating(&Row::Faded, base::Unretained(this))) {
    title_ =
        AddChildView(MakeLabel(title, text::kRowTitle, kPaneColorForeground));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetMultiLine(true);
    for (raw_ptr<views::Label>* label : {&description_, &outgoing_}) {
      *label = AddChildView(MakeLabel(description, text::kRowDescription,
                                      kPaneColorMutedForeground));
      (*label)->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      (*label)->SetMultiLine(true);
    }
    description_->SetVisible(!description.empty());
    // Read before the row's controls, which carry its title as their names;
    // the title itself stays out, so it isn't read twice.
    ExposeToScreenReaders(*description_);
    outgoing_->SetVisible(false);
    outgoing_->GetViewAccessibility().SetIsIgnored(true);
    controls_ = AddChildView(std::make_unique<views::View>());
  }

  // A row whose control is text, not named by the title (the version's):
  // the title is read before it.
  void ExposeTitle() { ExposeToScreenReaders(*title_); }

  // Each control after the one before, 8 apart.
  template <typename T>
  T* AddControl(std::unique_ptr<T> control) {
    T* added = controls_->AddChildView(std::move(control));
    InvalidateLayout();
    return added;
  }

  // A status that changes in place crossfades (the update row's); a row the
  // Swift drew anew (the PIN's, once one is set) changes at once.
  void SetDescription(const std::u16string& description, bool crossfades) {
    if (description_->GetText() == description) {
      return;
    }
    const std::u16string old(description_->GetText());
    description_->SetText(description);
    description_->SetVisible(!description.empty());
    PreferredSizeChanged();
    ui::Compositor* compositor =
        GetWidget() && IsDrawn() ? GetWidget()->GetCompositor() : nullptr;
    if (!crossfades || !compositor || old.empty() || motion::IsReduced()) {
      return;
    }
    // The old text fades out as the new one fades in, over each other.
    outgoing_->SetText(old);
    outgoing_->SetVisible(true);
    for (views::Label* label : {description_.get(), outgoing_.get()}) {
      if (!label->layer()) {
        label->SetPaintToLayer();
        label->layer()->SetFillsBoundsOpaquely(false);
      }
    }
    fade_.MoveTo(0, motion::Status(), nullptr);
    fade_.MoveTo(1, motion::Status(), compositor);
  }

  void SetLast(bool last) {
    if (last_ != last) {
      last_ = last;
      SchedulePaint();
    }
  }

  int HeightForWidth(int width) const {
    const int labels = LabelsWidth(width);
    int text = title_->GetHeightForWidth(labels);
    if (description_->GetVisible()) {
      text += kTitleSpacing + description_->GetHeightForWidth(labels);
    }
    int controls = 0;
    for (views::View* control : this->controls()) {
      controls = std::max(controls, control->GetPreferredSize().height());
    }
    return std::max(kRowMinHeight, 2 * kRowPadding + std::max(text, controls));
  }

  std::vector<views::View*> controls() const {
    std::vector<views::View*> controls;
    for (views::View* control : controls_->children()) {
      if (control->GetVisible()) {
        controls.push_back(control);
      }
    }
    return controls;
  }

  // views::View:
  void Layout(PassKey) override {
    // The controls, their row's height, centered on it.
    int x = 0;
    int controls_height = 0;
    for (views::View* control : controls()) {
      const gfx::Size size = control->GetPreferredSize();
      control->SetBounds(x, 0, size.width(), size.height());
      x += size.width() + kControlSpacing;
      controls_height = std::max(controls_height, size.height());
    }
    const int controls_width = std::max(x - kControlSpacing, 0);
    for (views::View* control : controls()) {
      control->SetY((controls_height - control->height()) / 2);
    }
    controls_->SetBounds(width() - kRowTrailing - controls_width,
                         (height() - controls_height) / 2, controls_width,
                         controls_height);

    const int labels = LabelsWidth(width());
    const int title = title_->GetHeightForWidth(labels);
    const int description = description_->GetVisible()
                                ? description_->GetHeightForWidth(labels)
                                : 0;
    const int text =
        title + (description_->GetVisible() ? kTitleSpacing + description : 0);
    const int top = (height() - text) / 2;
    title_->SetBounds(kRowLeading, top, labels, title);
    for (views::Label* label : {description_.get(), outgoing_.get()}) {
      label->SetBounds(kRowLeading, top + title + kTitleSpacing, labels,
                       label->GetHeightForWidth(labels));
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const ui::ColorProvider* colors = GetColorProvider();
    if (last_ || !colors) {
      return;
    }
    // The hairline over the row's last point, the card's width.
    canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1),
                     colors->GetColor(kPaneColorBorder));
  }

 private:
  int LabelsWidth(int width) const {
    int controls = 0;
    for (views::View* control : this->controls()) {
      controls += control->GetPreferredSize().width() + kControlSpacing;
    }
    controls = std::max(controls - kControlSpacing, 0);
    return std::max(width - kRowLeading - kRowTrailing - kRowSpacing - controls,
                    0);
  }

  void Faded(double shown) {
    if (!description_->layer() || !outgoing_->layer()) {
      return;
    }
    description_->layer()->SetOpacity(shown);
    outgoing_->layer()->SetOpacity(1 - shown);
    if (shown >= 1 && !fade_.is_moving()) {
      outgoing_->SetVisible(false);
      description_->DestroyLayer();
      outgoing_->DestroyLayer();
    }
  }

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> description_ = nullptr;
  // The description going, while it crossfades.
  raw_ptr<views::Label> outgoing_ = nullptr;
  raw_ptr<views::View> controls_ = nullptr;
  bool last_ = false;
  ValueMotion fade_;
};

BEGIN_METADATA(SettingsView, Row)
END_METADATA

// A section's card: its fill rounded 22, its rows inside its border, which
// is drawn over them (CardBorder).
class SettingsView::Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  Card() { border_ = AddChildView(std::make_unique<CardBorder>()); }

  Row* AddRow(std::unique_ptr<Row> row) {
    Row* added = AddChildViewAt(std::move(row), children().size() - 1);
    rows_.push_back(added);
    return added;
  }

  int HeightForWidth(int width) const {
    int height = 2 * kBorder;
    for (const Row* row : rows_) {
      if (row->GetVisible()) {
        height += row->HeightForWidth(width);
      }
    }
    return height;
  }

  // views::View:
  void Layout(PassKey) override {
    int y = kBorder;
    Row* last = nullptr;
    for (Row* row : rows_) {
      if (!row->GetVisible()) {
        continue;
      }
      const int height = row->HeightForWidth(width());
      row->SetBounds(0, y, width(), height);
      y += height;
      row->SetLast(false);
      last = row;
    }
    if (last) {
      last->SetLast(true);
    }
    border_->SetBoundsRect(GetLocalBounds());
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kPaneColorCard));
    // In pieces, so a tile inside the card rasters no path (ADR 0123).
    FillSquircle(*canvas, gfx::RectF(GetLocalBounds()), radius::kXl, flags);
  }

 private:
  std::vector<raw_ptr<Row>> rows_;
  raw_ptr<CardBorder> border_ = nullptr;
};

BEGIN_METADATA(SettingsView, Card)
END_METADATA

// The page's contents: "Settings", then each section's label over its card.
class SettingsView::Contents : public views::View {
  METADATA_HEADER(Contents, views::View)

 public:
  Contents() {
    title_ = AddChildView(
        MakeLabel(u"Settings", kPageTitle, kPaneColorAccentForeground));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
    ExposeToScreenReaders(*title_);
  }

  Card* AddGroup(const std::u16string& title) {
    views::Label* label =
        AddChildView(MakeLabel(title, kGroupLabel, kPaneColorMutedForeground));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // Each section's label heads it, as the Swift's Form's sections were.
    label->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
    ExposeToScreenReaders(*label);
    Card* card = AddChildView(std::make_unique<Card>());
    groups_.push_back({label, card});
    return card;
  }

  int HeightForWidth(int width) const {
    const int cards = CardsWidth(width);
    int height = kPaddingY + kPageTitle.line_height;
    for (const Group& group : groups_) {
      height += kSpacing + kGroupLabel.line_height + kGroupLabelSpacing +
                group.card->HeightForWidth(cards);
    }
    return height + kPaddingY;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : kMaxWidth + 2 * kPaddingX;
    return gfx::Size(width, HeightForWidth(width));
  }

  void Layout(PassKey) override {
    const int cards = CardsWidth(width());
    title_->SetBounds(kPaddingX, kPaddingY,
                      std::max(width() - 2 * kPaddingX, 0),
                      kPageTitle.line_height);
    int y = kPaddingY + kPageTitle.line_height;
    for (const Group& group : groups_) {
      y += kSpacing;
      group.label->SetBounds(kPaddingX + kGroupLabelInset, y,
                             std::max(cards - kGroupLabelInset, 0),
                             kGroupLabel.line_height);
      y += kGroupLabel.line_height + kGroupLabelSpacing;
      const int height = group.card->HeightForWidth(cards);
      group.card->SetBounds(kPaddingX, y, cards, height);
      y += height;
    }
  }

 private:
  struct Group {
    raw_ptr<views::Label> label;
    raw_ptr<Card> card;
  };

  static int CardsWidth(int width) {
    return std::clamp(width - 2 * kPaddingX, 0, kMaxWidth);
  }

  raw_ptr<views::Label> title_ = nullptr;
  std::vector<Group> groups_;
};

BEGIN_METADATA(SettingsView, Contents)
END_METADATA

// The App icon row's control (the Swift's AppIconButton): the chosen icon
// alone, 36 points, 2 in, the accent fading in behind it under the pointer as
// behind the row's other controls; a press opens every sky.
class SettingsView::AppIconButton : public views::Button {
  METADATA_HEADER(AppIconButton, views::Button)

 public:
  explicit AppIconButton(PressedCallback pressed)
      : views::Button(std::move(pressed)), look_(Look()) {
    SetID(kViewIdSettingsAppIcon);
    highlight_ = std::make_unique<ControlHighlight>(
        this, look_.shape, look_.disabled_opacity,
        base::BindRepeating(&views::View::SchedulePaint,
                            base::Unretained(this)));
    SetBackground(std::make_unique<ControlBackground>(look_, highlight_.get()));
    GetViewAccessibility().SetName(u"App icon");
    GetViewAccessibility().SetDescription(u"Shows every icon");
  }

  void SetIcon(model::AppIcon icon, const gfx::ImageSkia& thumbnail) {
    GetViewAccessibility().SetValue(AppIconTitle(icon));
    if (thumbnail.BackedBySameObjectAs(thumbnail_) && icon == icon_) {
      return;
    }
    icon_ = icon;
    thumbnail_ = thumbnail;
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int side = AppIconImages::kRowSize + 2 * kAppIconInset;
    return gfx::Size(side, side);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!thumbnail_.isNull()) {
      canvas->DrawImageInt(thumbnail_, kAppIconInset, kAppIconInset);
    }
  }

 private:
  static ControlLook Look() {
    ControlLook look(ControlShape::Squircle(radius::kMd));
    look.hover_fill = kPaneColorAccent;
    // A plain button's: the pointer's fill alone.
    look.press = SK_ColorTRANSPARENT;
    look.disabled_opacity = 1;
    return look;
  }

  const ControlLook look_;
  model::AppIcon icon_ = model::AppIcon::kAutomatic;
  gfx::ImageSkia thumbnail_;
  std::unique_ptr<ControlHighlight> highlight_;
};

BEGIN_METADATA(SettingsView, AppIconButton)
END_METADATA

SettingsView::Callbacks::Callbacks() = default;
SettingsView::Callbacks::Callbacks(Callbacks&&) = default;
SettingsView::Callbacks& SettingsView::Callbacks::operator=(Callbacks&&) =
    default;
SettingsView::Callbacks::~Callbacks() = default;

SettingsView::Shows::Shows() = default;
SettingsView::Shows::Shows(const Shows&) = default;
SettingsView::Shows& SettingsView::Shows::operator=(const Shows&) = default;
SettingsView::Shows::~Shows() = default;

SettingsView::SettingsView(Callbacks callbacks, AppIconImages& images)
    : callbacks_(std::move(callbacks)), images_(images) {
  SetID(kViewIdSettings);
  // It takes the keys while it shows, with no ring of its own: the page
  // under it, hidden, has none.
  SetFocusBehavior(FocusBehavior::ALWAYS);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Settings");

  // The panel's corners clip what scrolls, the overlay scroll bar too, as
  // the Swift's panel clipped its own: as the page's are (ADR 0057).
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kPanelLayerRadius));
  layer()->SetIsFastRoundedCorner(true);

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // The panel's card shows through: nothing here fills past its corners,
  // and nothing marks an edge the content runs past, as the Swift's didn't.
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetDrawOverflowIndicator(false);
  scroll_view_->SetUseContentsPreferredSize(true);
  contents_ = scroll_view_->SetContents(std::make_unique<Contents>());

  Card* appearance = contents_->AddGroup(u"Appearance");
  Row* theme = appearance->AddRow(
      std::make_unique<Row>(u"Theme", u"Choose how Pane looks."));
  theme_ = theme->AddControl(std::make_unique<MenuButton>(
      u"Theme",
      std::vector<MenuButton::Option>{{u"System", Icon::kMonitor},
                                      {u"Light", Icon::kSun},
                                      {u"Dark", Icon::kMoon}},
      base::BindRepeating(
          [](SettingsView* view, size_t index) {
            view->callbacks_.change.Run(base::BindRepeating(
                [](model::Theme theme, model::AppSettings& settings) {
                  settings.theme = theme;
                },
                kThemes[index]));
          },
          base::Unretained(this))));
  theme_->SetID(kViewIdSettingsTheme);
  TimeOpening(theme_);
  // ADR 0089: the chosen icon; a press opens every sky.
  Row* app_icon = appearance->AddRow(std::make_unique<Row>(
      u"App icon",
      u"Automatic follows the theme: Day when light, Night when dark."));
  app_icon_ =
      app_icon->AddControl(std::make_unique<AppIconButton>(base::BindRepeating(
          [](SettingsView* view) { view->callbacks_.choose_app_icon.Run(); },
          base::Unretained(this))));

  // A preset menu, setting `field` of the settings.
  const auto preset_menu = [this](
                               Row* row, std::u16string label,
                               base::span<const Preset> presets,
                               std::optional<int> model::AppSettings::* field,
                               int id) {
    MenuButton* menu = row->AddControl(std::make_unique<MenuButton>(
        std::move(label), OptionsOf(presets),
        base::BindRepeating(
            [](SettingsView* view, base::span<const Preset> presets,
               std::optional<int> model::AppSettings::* field, size_t index) {
              view->callbacks_.change.Run(base::BindRepeating(
                  [](std::optional<int> model::AppSettings::* field,
                     std::optional<int> value,
                     model::AppSettings& settings) { settings.*field = value; },
                  field, presets[index].value));
            },
            base::Unretained(this), presets, field)));
    menu->SetID(id);
    TimeOpening(menu);
    return menu;
  };

  Card* memory = contents_->AddGroup(u"Memory & disk");
  tab_sleep_ = preset_menu(
      memory->AddRow(std::make_unique<Row>(
          u"Sleep background tabs",
          u"A tab in the background sleeps after this long, to free memory.")),
      u"Sleep background tabs", kTabSleep,
      &model::AppSettings::tab_sleep_after_minutes, kViewIdSettingsTabSleep);
  profile_unload_ =
      preset_menu(memory->AddRow(std::make_unique<Row>(
                      u"Unload hidden profiles",
                      u"A profile out of view unloads after this long.")),
                  u"Unload hidden profiles", kProfileUnload,
                  &model::AppSettings::profile_unload_after_minutes,
                  kViewIdSettingsProfileUnload);
  cache_budget_ = preset_menu(
      memory->AddRow(std::make_unique<Row>(
          u"Trim cache",
          u"A profile that unloads trims its cache past this size.")),
      u"Trim cache", kCacheBudget, &model::AppSettings::cache_budget_mb,
      kViewIdSettingsCacheBudget);

  // The PIN's buttons open the Lock's PIN screen (L1 draws it).
  const auto pin_button = [this](Row* row, const std::u16string& title,
                                 model::PinEntry::Mode mode, int id) {
    TextButton* button = row->AddControl(std::make_unique<TextButton>(
        base::BindRepeating(
            [](SettingsView* view, model::PinEntry::Mode mode) {
              view->callbacks_.pin.Run(mode);
            },
            base::Unretained(this), mode),
        title, ButtonVariant::kOutline, button_size::kSmall));
    button->SetID(id);
    return button;
  };
  Card* security = contents_->AddGroup(u"Security");
  pin_ =
      security->AddRow(std::make_unique<Row>(u"PIN", u"Lock Pane with a PIN."));
  set_up_pin_ = pin_button(pin_, u"Set up PIN", model::PinEntry::Mode::kSetup,
                           kViewIdSettingsSetUpPin);
  change_pin_ = pin_button(pin_, u"Change PIN", model::PinEntry::Mode::kChange,
                           kViewIdSettingsChangePin);
  remove_pin_ = pin_button(pin_, u"Remove PIN", model::PinEntry::Mode::kRemove,
                           kViewIdSettingsRemovePin);
  // Paper 06.4.
  lock_after_row_ = security->AddRow(std::make_unique<Row>(
      u"Lock after",
      u"Pane locks after this long without use, and when the Mac sleeps or "
      u"locks."));
  lock_after_ = preset_menu(lock_after_row_, u"Lock after", kLockAfter,
                            &model::AppSettings::lock_after_minutes,
                            kViewIdSettingsLockAfter);
  ShowPinRows(false);

  Card* updates = contents_->AddGroup(u"Updates");
  update_row_ = updates->AddRow(
      std::make_unique<Row>(u"Updates", SettingsUpdate::kChecksOnItsOwn));
  update_row_->SetID(kViewIdSettingsUpdates);
  update_button_ = update_row_->AddControl(std::make_unique<TextButton>(
      base::BindRepeating(
          [](SettingsView* view) {
            (view->shows_.update.downloads ? view->callbacks_.download_update
                                           : view->callbacks_.check_updates)
                .Run();
          },
          base::Unretained(this)),
      u"Check for updates", ButtonVariant::kOutline, button_size::kSmall));
  update_button_->SetID(kViewIdSettingsCheckUpdates);
  version_row_ =
      updates->AddRow(std::make_unique<Row>(u"Version", std::u16string()));
  version_row_->ExposeTitle();
  ExposeToScreenReaders(*version_row_->AddControl(
      MakeLabel(std::u16string(), kVersion, kPaneColorMutedForeground)));

  images_observation_.Observe(&images);
  SetShows(shows_);
}

SettingsView::~SettingsView() = default;

void SettingsView::TimeOpening(MenuButton* menu) {
  // From the press to the system's menu about to show: the work before the
  // menu's own window draws, which runs with the app's tasks held.
  MenuButton::Opening opening;
  opening.asked = base::BindRepeating(
      [](SettingsView* view) {
        view->opening_menu_ = BeginInteractionInterval(Interaction::kOpenMenu);
      },
      base::Unretained(this));
  opening.shows = base::BindRepeating(
      [](SettingsView* view) {
        if (const std::optional<uint64_t> interval =
                std::exchange(view->opening_menu_, std::nullopt)) {
          EndInteractionInterval(Interaction::kOpenMenu, *interval);
        }
      },
      base::Unretained(this));
  menu->SetOpening(std::move(opening));
}

void SettingsView::ShowPinRows(bool has_pin) {
  pin_->SetDescription(
      has_pin ? u"Pane asks for it when it opens, and when it locks."
              : u"Lock Pane with a PIN.",
      /*crossfades=*/false);
  set_up_pin_->SetVisible(!has_pin);
  change_pin_->SetVisible(has_pin);
  remove_pin_->SetVisible(has_pin);
  lock_after_row_->SetVisible(has_pin);
  pin_->InvalidateLayout();
  contents_->InvalidateLayout();
}

void SettingsView::SetShows(const Shows& shows) {
  const bool first = !std::exchange(shown_once_, true);
  if (!first && shows == shows_) {
    return;
  }
  const Shows before = shows_;
  shows_ = shows;
  const model::AppSettings& settings = shows.settings;

  theme_->SetSelected(static_cast<size_t>(
      std::ranges::find(kThemes, settings.theme) - kThemes.begin()));
  OnAppIconThumbnailsChanged();
  tab_sleep_->SetSelected(IndexOf(kTabSleep, settings.tab_sleep_after_minutes));
  profile_unload_->SetSelected(
      IndexOf(kProfileUnload, settings.profile_unload_after_minutes));
  cache_budget_->SetSelected(IndexOf(kCacheBudget, settings.cache_budget_mb));
  lock_after_->SetSelected(IndexOf(kLockAfter, settings.lock_after_minutes));
  if (first || shows.has_pin != before.has_pin) {
    ShowPinRows(shows.has_pin);
  }

  if (first || shows.update != before.update) {
    update_row_->SetDescription(shows.update.status, /*crossfades=*/true);
    update_row_->SetID(shows.update.up_to_date ? kViewIdSettingsUpToDate
                                               : kViewIdSettingsUpdates);
    update_button_->SetTitle(shows.update.button);
    update_button_->SetEnabled(shows.update.button_enabled);
    update_button_->SetID(shows.update.downloads ? kViewIdSettingsDownloadUpdate
                                                 : kViewIdSettingsCheckUpdates);
    update_row_->InvalidateLayout();
  }

  if (first || shows.version != before.version) {
    auto* version =
        static_cast<views::Label*>(version_row_->controls().front());
    version->SetText(shows.version);
    version_row_->InvalidateLayout();
  }
  contents_->InvalidateLayout();
}

void SettingsView::OnAppIconThumbnailsChanged() {
  app_icon_->SetIcon(
      shows_.settings.app_icon,
      images_->Thumbnail(shows_.settings.app_icon, AppIconImages::kRowSize));
}

std::vector<std::pair<std::string, const views::View*>>
SettingsView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named;
  const views::View* views[] = {this,
                                theme_.get(),
                                app_icon_.get(),
                                tab_sleep_.get(),
                                profile_unload_.get(),
                                cache_budget_.get(),
                                set_up_pin_.get(),
                                change_pin_.get(),
                                remove_pin_.get(),
                                lock_after_.get(),
                                update_row_.get(),
                                update_button_.get()};
  for (const views::View* view : views) {
    if (view->IsDrawn()) {
      named.emplace_back(std::string(ViewIdName(view->GetID())), view);
    }
  }
  return named;
}

MenuButton* SettingsView::FindMenu(std::string_view name) {
  for (MenuButton* menu :
       {theme_.get(), tab_sleep_.get(), profile_unload_.get(),
        cache_budget_.get(), lock_after_.get()}) {
    if (ViewIdName(menu->GetID()) == name) {
      return menu;
    }
  }
  return nullptr;
}

void SettingsView::Layout(PassKey) {
  scroll_view_->SetBoundsRect(GetLocalBounds());
}

BEGIN_METADATA(SettingsView)
END_METADATA

}  // namespace pane
