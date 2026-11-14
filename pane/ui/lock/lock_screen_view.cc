// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/lock/lock_screen_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/model/security.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icon_button.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/placement.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/design/update_status.h"
#include "pane/ui/lock/pin_pad.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/panel_frame.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The panel, 8 in from the window's edges (LockScreenView's .padding(8)).
constexpr int kInset = 8;
// Its columns: 80 points either side of the middle one (grid-cols-[5rem_1fr_
// 5rem]), the back arrow in the middle of the left one, 20 points across.
constexpr int kSide = 80;
constexpr int kBackSize = 20;
// The middle's stack, 28 apart; the data's move, 14 apart, its bar at the
// numpad's width.
constexpr int kSpacing = 28;
constexpr int kMoveSpacing = 14;
// The update on offer, 24 from the panel's bottom.
constexpr int kUpdateBottom = 24;
// The dots and the numpad while no key can be read (Paper 07.6).
constexpr float kUnreadableOpacity = 0.4f;

// Every line here: 13 points, light, on a 20-point line (lineBox(20); the
// error's lineSpacing).
constexpr TextStyle kMessage{13, weight::kLight, 20};

// Continue: its title alone, 60%, 80% under the pointer or pressed, fading
// as the keys do (the Swift's HintButtonStyle).
ControlLook ContinueLook() {
  ControlLook look(ControlShape::Capsule());
  look.tint = kPaneColorPinHint;
  look.lit_tint = kPaneColorPinHintLit;
  look.press = SK_ColorTRANSPARENT;
  look.disabled_opacity = 1;
  return look;
}
constexpr ButtonSize kContinueSize{20, 0, 0, kMessage};

// The back arrow: 30%, 60% under the pointer, the press's fill under it, no
// hover fill (the Swift's .icon(size: 20, tint:, hoverTint:, fill: nil)).
ControlLook BackLook() {
  ControlLook look(ControlShape::Circle());
  look.tint = kPaneColorPinQuiet;
  look.lit_tint = kPaneColorPinHint;
  look.disabled_opacity = 0.4f;
  return look;
}

std::unique_ptr<views::Label> Message(const std::u16string& text,
                                      ui::ColorVariant color) {
  auto label = MakeLabel(text, kMessage, std::move(color));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  // Not a control: a press there moves the window, as the panel's.
  label->SetCanProcessEventsWithinSubtree(false);
  return label;
}

}  // namespace

// The panel's middle and its controls' parent: it holds the keys while a PIN
// shows, and VoiceOver in it (ADR 0071).
class LockScreenView::Panel : public views::View {
  METADATA_HEADER(Panel, views::View)

 public:
  explicit Panel(LockScreenView* screen) : screen_(screen) {
    SetID(kViewIdLockScreen);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    // VoiceOver stays on the PIN screen: nothing under it is reachable.
    GetViewAccessibility().SetIsModal(true);
  }

  // views::View:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return screen_->takes_every_key();
  }

  // It takes every key but Quit (the Swift's KeyRouting.pinScreen): Esc goes
  // back, Return continues, Delete takes a digit back, and what a key types
  // without ⌘, ⌃, or ⌥ is a digit, or nothing.
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (!screen_->takes_every_key()) {
      return false;
    }
    switch (event.key_code()) {
      case ui::VKEY_ESCAPE:
        screen_->callbacks_.back.Run();
        return true;
      case ui::VKEY_RETURN:
        screen_->callbacks_.continue_with_pin.Run();
        return true;
      case ui::VKEY_BACK:
        screen_->callbacks_.backspace.Run();
        return true;
      default:
        break;
    }
    if (!(event.flags() &
          (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN))) {
      if (const char16_t character = event.GetCharacter()) {
        screen_->callbacks_.digit.Run(character);
      }
    }
    return true;
  }

  bool OnKeyReleased(const ui::KeyEvent& event) override {
    return screen_->takes_every_key();
  }

 private:
  const raw_ptr<LockScreenView> screen_;
};

BEGIN_METADATA(LockScreenView, Panel)
END_METADATA

// The PIN's column (the Swift's pin(_:) and hints(_:)): the step's title, the
// dots, the numpad, then Enter at least 4 digits or Continue, the error, and
// the attempts left, 28 apart, each in the column's middle.
class LockScreenView::PinContent : public views::View {
  METADATA_HEADER(PinContent, views::View)

 public:
  explicit PinContent(Callbacks& callbacks) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    title_ = AddChildView(Message(std::u16string(), kPaneColorPinTitle));
    dots_ = AddChildView(std::make_unique<PinDots>());
    numpad_ = AddChildView(
        std::make_unique<Numpad>(callbacks.digit, callbacks.backspace));
    numpad_->SetPaintToLayer();
    numpad_->layer()->SetFillsBoundsOpaquely(false);
    hint_ =
        AddChildView(Message(u"Enter at least 4 digits", kPaneColorPinQuiet));
    continue_ = AddChildView(std::make_unique<TextButton>(
        callbacks.continue_with_pin, std::u16string(), ContinueLook(),
        kContinueSize));
    continue_->SetID(kViewIdLockScreenContinue);
    error_ = AddChildView(Message(std::u16string(), kPaneColorDestructive));
    error_->SetMultiLine(true);
    attempts_ = AddChildView(Message(std::u16string(), kPaneColorPinQuiet));
    attempts_->SetID(kViewIdLockScreenAttempts);
    // The panel holds the keys, so its text is read only exposed: the step's
    // title, the dots (PinDots), the hint, the error, and the attempts left,
    // as the Swift's PIN screen read them.
    for (views::Label* text :
         {title_.get(), hint_.get(), error_.get(), attempts_.get()}) {
      ExposeToScreenReaders(*text);
    }
  }

  PinDots* dots() { return dots_; }

  void Set(const model::PinEntry& entry,
           std::optional<int> attempts_toward_wipe,
           bool animate) {
    const std::optional<std::string_view> title = entry.Title();
    title_->SetVisible(title.has_value());
    if (title) {
      title_->SetText(base::UTF8ToUTF16(*title));
    }
    dots_->Set(entry.Dots(), static_cast<int>(entry.entered().size()), animate);
    const float opacity = entry.is_unreadable() ? kUnreadableOpacity : 1;
    dots_->layer()->SetOpacity(opacity);
    numpad_->layer()->SetOpacity(opacity);

    const bool entering = entry.step() == model::PinEntry::Step::kEnter;
    hint_->SetVisible(entering && !entry.CanContinue());
    continue_->SetVisible(entering && entry.CanContinue());
    continue_->SetTitle(base::StrCat(
        {u"Continue with ",
         base::NumberToString16(static_cast<int>(entry.entered().size())),
         u"-digit PIN"}));

    const std::u16string error_before = error_->GetVisible()
                                            ? std::u16string(error_->GetText())
                                            : std::u16string();
    error_->SetVisible(entry.error().has_value());
    error_->SetText(entry.error() ? base::UTF8ToUTF16(*entry.error())
                                  : std::u16string());

    // The attempts left before the panic wipe, from the first wrong PIN; none
    // where wrong PINs wipe nothing, on a seal the TPM guards (Paper 07.1.1).
    const int failed_attempts = attempts_toward_wipe.value_or(0);
    const int remaining =
        std::max(0, model::SecurityState::kMaxAttempts - failed_attempts);
    attempts_->SetVisible(entry.mode() == model::PinEntry::Mode::kUnlock &&
                          failed_attempts > 0);
    attempts_->SetText(remaining == 1
                           ? u"Last attempt. All data will be erased."
                           : base::StrCat({base::NumberToString16(remaining),
                                           u" attempts remaining"}));
    attempts_->SetEnabledColor(remaining == 1 ? kPaneColorDestructive
                                              : kPaneColorPinQuiet);

    // A wrong PIN is said as it shows, which the dots' shake alone tells the
    // eye: a new error, and the attempts left once one more has failed.
    std::vector<std::u16string_view> said;
    if (error_->GetVisible() && error_->GetText() != error_before) {
      said.push_back(error_->GetText());
    }
    if (attempts_->GetVisible() && failed_attempts > failed_attempts_) {
      said.push_back(attempts_->GetText());
    }
    failed_attempts_ = failed_attempts;
    if (!said.empty()) {
      GetViewAccessibility().AnnounceAlert(base::JoinString(said, u". "));
    }
    InvalidateLayout();
  }

  // As tall as what it shows, at `width`.
  int HeightFor(int width) const {
    int height = 0;
    bool first = true;
    for (const views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      height += (first ? 0 : kSpacing) + HeightOf(child, width);
      first = false;
    }
    return height;
  }

  std::vector<std::pair<std::string, const views::View*>> NamedViews() const {
    std::vector<std::pair<std::string, const views::View*>> named =
        numpad_->NamedViews();
    for (const views::View* view :
         {static_cast<const views::View*>(dots_.get()),
          static_cast<const views::View*>(continue_.get()),
          static_cast<const views::View*>(attempts_.get())}) {
      named.emplace_back(std::string(ViewIdName(view->GetID())), view);
    }
    return named;
  }

  // views::View:
  void Layout(PassKey) override {
    int y = 0;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      const int height = HeightOf(child, width());
      // The dots' row is the column's width, so it centers itself to the
      // fraction of a point, and shakes within it.
      const int child_width =
          child == dots_ || child == error_
              ? width()
              : std::min(child->GetPreferredSize().width(), width());
      child->SetBounds((width() - child_width) / 2, y, child_width, height);
      y += height + kSpacing;
    }
  }

 private:
  int HeightOf(const views::View* child, int width) const {
    if (child == error_) {
      return error_->GetHeightForWidth(width);
    }
    return child->GetPreferredSize().height();
  }

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<PinDots> dots_ = nullptr;
  raw_ptr<Numpad> numpad_ = nullptr;
  raw_ptr<views::Label> hint_ = nullptr;
  raw_ptr<TextButton> continue_ = nullptr;
  raw_ptr<views::Label> error_ = nullptr;
  raw_ptr<views::Label> attempts_ = nullptr;
  // The failed attempts last shown, so each new one is said once.
  int failed_attempts_ = 0;
};

BEGIN_METADATA(LockScreenView, PinContent)
END_METADATA

// The data's one-time move into its volume (ADR 0113, Paper 07.5): Encrypting
// your data, the update's bar at the numpad's width, and Once, after this
// update, 14 apart; read as one element, with how far it has come.
class LockScreenView::MoveContent : public views::View {
  METADATA_HEADER(MoveContent, views::View)

 public:
  static constexpr int kHeight =
      2 * kMessage.line_height + 2 * kMoveSpacing + UpdateBar::kHeight;

  MoveContent() {
    SetID(kViewIdLockScreenMove);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Encrypting your data");
    // Read, under the panel that holds the keys, as one element.
    ExposeToScreenReaders(*this);
    title_ = AddChildView(Message(u"Encrypting your data", kPaneColorPinTitle));
    bar_ = AddChildView(std::make_unique<UpdateBar>());
    note_ =
        AddChildView(Message(u"Once, after this update", kPaneColorPinQuiet));
    for (views::View* child : children()) {
      child->GetViewAccessibility().SetIsIgnored(true);
    }
  }

  void Set(double share, bool animate) {
    views::Widget* widget = GetWidget();
    bar_->SetShare(share, animate && widget && widget->IsVisible()
                              ? widget->GetCompositor()
                              : nullptr);
    GetViewAccessibility().SetValue(base::StrCat(
        {base::NumberToString16(static_cast<int>(std::lround(share * 100))),
         u" percent"}));
  }

  // views::View:
  void Layout(PassKey) override {
    title_->SetBounds(0, 0, width(), kMessage.line_height);
    bar_->SetBounds((width() - Numpad::kWidth) / 2,
                    kMessage.line_height + kMoveSpacing, Numpad::kWidth,
                    UpdateBar::kHeight);
    note_->SetBounds(
        0, kMessage.line_height + 2 * kMoveSpacing + UpdateBar::kHeight,
        width(), kMessage.line_height);
  }

 private:
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<UpdateBar> bar_ = nullptr;
  raw_ptr<views::Label> note_ = nullptr;
};

BEGIN_METADATA(LockScreenView, MoveContent)
END_METADATA

LockScreenView::Callbacks::Callbacks() = default;
LockScreenView::Callbacks::Callbacks(Callbacks&&) = default;
LockScreenView::Callbacks& LockScreenView::Callbacks::operator=(Callbacks&&) =
    default;
LockScreenView::Callbacks::~Callbacks() = default;

LockScreenView::Shows::Shows() = default;
LockScreenView::Shows::Shows(const Shows&) = default;
LockScreenView::Shows& LockScreenView::Shows::operator=(const Shows&) = default;
LockScreenView::Shows::~Shows() = default;

LockScreenView::LockScreenView(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  // A layer that draws nothing, which carries the panel as it rises and
  // sinks; what it holds draws on layers of its own.
  SetPaintToLayer(ui::LAYER_NOT_DRAWN);
  SetVisible(false);
  frame_ = AddChildView(std::make_unique<PanelFrame>(/*on_window=*/false));
  panel_ = AddChildView(std::make_unique<Panel>(this));
  auto back = std::make_unique<IconButton>(
      callbacks_.back, kBackSize,
      IconButton::Glyph{.icon = Icon::kArrowLeft, .size = kBackSize}, u"Back",
      BackLook());
  back->SetID(kViewIdLockScreenBack);
  back->SetPaintToLayer();
  back->layer()->SetFillsBoundsOpaquely(false);
  back_ = panel_->AddChildView(std::move(back));
  pin_ = panel_->AddChildView(std::make_unique<PinContent>(callbacks_));
  move_ = panel_->AddChildView(std::make_unique<MoveContent>());
  move_->SetVisible(false);
  update_ = panel_->AddChildView(
      std::make_unique<LockUpdateStatus>(callbacks_.update));
  update_->SetVisible(false);
}

LockScreenView::~LockScreenView() = default;

bool LockScreenView::takes_every_key() const {
  return showing() && shows_.entry.has_value();
}

void LockScreenView::SetShows(const Shows& shows, bool animate) {
  const bool was_showing = showing();
  if (!shows.shown()) {
    // It sinks as it showed last.
    if (was_showing) {
      leaving_ = true;
      SetCanProcessEventsWithinSubtree(false);
      if (animate) {
        Slide(false);
      } else {
        Settle(false);
      }
    }
    return;
  }

  shows_ = shows;
  const views::Widget* widget = GetWidget();
  // What changes on a screen already up moves; a screen coming shows as it
  // is from its first frame.
  const bool moves = animate && was_showing && widget && widget->IsVisible();
  if (shows_.entry) {
    pin_->Set(*shows_.entry, shows_.attempts_toward_wipe, moves);
  }
  if (shows_.move_share) {
    move_->Set(*shows_.move_share, moves);
  }
  back_->SetVisible(shows_.entry && shows_.entry->dismissable());
  update_->SetVisible(shows_.update.has_value());
  if (shows_.update) {
    update_->Set(*shows_.update);
  }
  ShowContent(moves);
  InvalidateLayout();

  if (!was_showing) {
    leaving_ = false;
    SetCanProcessEventsWithinSubtree(true);
    // From below the window, unless it is on its way down there.
    const bool sinking = GetVisible();
    SetVisible(true);
    if (animate && widget && widget->IsVisible()) {
      if (!sinking) {
        layer()->SetTransform(gfx::Transform::MakeTranslation(0, height()));
      }
      Slide(true);
    } else {
      Settle(true);
    }
  }
}

void LockScreenView::ShowContent(bool animate) {
  const bool pin = shows_.entry.has_value();
  views::View* in = pin ? static_cast<views::View*>(pin_.get()) : move_.get();
  views::View* out = pin ? static_cast<views::View*>(move_.get()) : pin_.get();
  if (pin == pin_shown_ && in->GetVisible()) {
    return;
  }
  pin_shown_ = pin;
  if (!animate) {
    in->SetVisible(true);
    in->layer()->SetOpacity(1);
    out->SetVisible(false);
    return;
  }
  // The two crossfade, as SwiftUI's default transition did.
  if (!in->GetVisible()) {
    in->layer()->SetOpacity(0);
    in->SetVisible(true);
  }
  CompositorMotion::Start(in->layer(), motion::Smooth(), {.opacity = 1.0f});
  CompositorMotion::Start(
      out->layer(), motion::Smooth(), {.opacity = 0.0f},
      base::BindOnce(
          [](base::WeakPtr<LockScreenView> screen, bool pin) {
            if (screen && screen->pin_shown_ == pin) {
              (pin ? static_cast<views::View*>(screen->move_.get())
                   : screen->pin_.get())
                  ->SetVisible(false);
            }
          },
          weak_factory_.GetWeakPtr(), pin));
}

void LockScreenView::Settle(bool shown) {
  layer()->SetTransform(gfx::Transform());
  if (!shown) {
    SetVisible(false);
    leaving_ = false;
  }
}

void LockScreenView::Slide(bool shown) {
  CompositorMotion::Start(
      layer(), shown ? motion::LockIn() : motion::LockOut(),
      {.transform = shown ? gfx::Transform()
                          : gfx::Transform::MakeTranslation(0, height())},
      base::BindOnce(&LockScreenView::Slid, weak_factory_.GetWeakPtr(), shown));
}

void LockScreenView::Slid(bool shown) {
  if (!shown && leaving_) {
    Settle(false);
  }
}

void LockScreenView::Shake() {
  if (showing() && shows_.entry) {
    pin_->dots()->Shake();
  }
}

void LockScreenView::TakeFocus() {
  panel_->RequestFocus();
}

bool LockScreenView::IsDragArea(const gfx::Point& point) {
  for (const views::View* view = GetEventHandlerForPoint(point);
       view && view != this; view = view->parent()) {
    if (views::IsViewClass<views::Button>(view)) {
      return false;
    }
  }
  return true;
}

std::vector<std::pair<std::string, const views::View*>>
LockScreenView::NamedViews() const {
  std::vector<std::pair<std::string, const views::View*>> named;
  if (!showing()) {
    return named;
  }
  named.emplace_back(std::string(ViewIdName(kViewIdLockScreen)), panel_);
  if (shows_.entry) {
    for (auto& entry : pin_->NamedViews()) {
      named.push_back(std::move(entry));
    }
  }
  named.emplace_back(std::string(ViewIdName(kViewIdLockScreenBack)), back_);
  named.emplace_back(std::string(ViewIdName(kViewIdLockScreenMove)), move_);
  named.emplace_back(std::string(ViewIdName(kViewIdLockScreenUpdate)), update_);
  if (const views::View* action = update_->action()) {
    named.emplace_back(std::string(ViewIdName(action->GetID())), action);
  }
  return named;
}

void LockScreenView::Layout(PassKey) {
  gfx::Rect panel = GetLocalBounds();
  panel.Inset(kInset);
  frame_->SetBoundsRect(GetLocalBounds());
  frame_->SetPanel(panel, /*square=*/false);
  panel_->SetBoundsRect(panel);

  // In the panel: the back arrow in the middle of its left column, the
  // middle column's content in the middle of the panel, to the fraction of a
  // point, as SwiftUI centers it, and the update 24 from its bottom.
  const float scale = ScaleOf(GetWidget());
  back_->SetBounds((kSide - kBackSize) / 2, (panel.height() - kBackSize) / 2,
                   kBackSize, kBackSize);
  const int column = std::max(panel.width() - 2 * kSide, 0);
  const int pin_height = pin_->HeightFor(column);
  PlaceOnPixels(*pin_,
                gfx::RectF(kSide, (panel.height() - pin_height) / 2.0f, column,
                           pin_height),
                scale);
  PlaceOnPixels(
      *move_,
      gfx::RectF(kSide, (panel.height() - MoveContent::kHeight) / 2.0f, column,
                 MoveContent::kHeight),
      scale);
  const gfx::Size update = update_->GetPreferredSize();
  PlaceOnPixels(*update_,
                gfx::RectF((panel.width() - update.width()) / 2.0f,
                           panel.height() - kUpdateBottom - update.height(),
                           update.width(), update.height()),
                scale);
}

void LockScreenView::OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                                float new_device_scale_factor) {
  views::View::OnDeviceScaleFactorChanged(old_device_scale_factor,
                                          new_device_scale_factor);
  // Its places to the fraction of a point are the screen's pixels.
  InvalidateLayout();
}

BEGIN_METADATA(LockScreenView)
END_METADATA

}  // namespace pane
