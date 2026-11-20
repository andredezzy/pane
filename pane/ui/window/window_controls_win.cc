// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/window_controls.h"

#include <windows.h>

#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/win/windows_version.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/windows_icon_painter.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace pane {

namespace {

// Chrome's (WindowFrameUtil): 45 wide and 1 between, the toolbar's 42 tall
// (Paper 11.1's B).
constexpr int kButtonWidth = 45;
constexpr int kSpacing = 1;
constexpr int kButtonHeight = 42;
constexpr int kRowWidth = 3 * kButtonWidth + 2 * kSpacing;

// Chrome's states (WindowsCaptionButton::OnPaintBackground, PaintSymbol): a
// hover fills the cell with the glyph's own color at 0x1A, a press at 0x33;
// Close fills with Chrome's red under the pointer, its X turning white, and
// at 0x98 of it pressed; an inactive window's glyphs drop to 0x66, 40%, but
// under the pointer (Paper 11.2).
constexpr SkAlpha kHovered = 0x1A;
constexpr SkAlpha kPressed = 0x33;
constexpr SkColor kCloseRed = SkColorSetRGB(0xE8, 0x11, 0x23);
constexpr SkAlpha kClosePressed = 0x98;
constexpr SkAlpha kInactiveGlyph = 0x66;

enum class Kind { kMinimize, kMaximize, kClose };

class CaptionButton : public views::Button {
  METADATA_HEADER(CaptionButton, views::Button)

 public:
  CaptionButton(Kind kind, PressedCallback pressed)
      : views::Button(std::move(pressed)),
        kind_(kind),
        hover_(0,
               base::BindRepeating(&CaptionButton::HoverStepped,
                                   base::Unretained(this))) {
    // Not in the Tab order, as neither Chrome's nor Windows' own are: Alt+
    // Space opens the window menu, and Windows' keys do what they do.
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    UpdateName();
    SetID(kind == Kind::kMinimize   ? kViewIdWindowMinimize
          : kind == Kind::kMaximize ? kViewIdWindowMaximize
                                    : kViewIdWindowClose);
  }

  Kind kind() const { return kind_; }

  // The cell the button shows in its bounds, which reach past it into the
  // frame's gutter for Close in a maximized window; and the content panel,
  // in its coordinates, whose corner rounds the fill.
  // Its glyph's cell and the panel, which its fill keeps to: it paints
  // again only when they move, as the row lays out at every layout of the
  // window's.
  void SetCell(const gfx::Rect& cell, const gfx::Rect& panel) {
    if (cell == cell_ && panel == panel_) {
      return;
    }
    cell_ = cell;
    panel_ = panel;
    SchedulePaint();
  }

  void SetMaximized(bool maximized) {
    if (maximized_ == maximized) {
      return;
    }
    maximized_ = maximized;
    UpdateName();
    SchedulePaint();
  }

  // views::Button:
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    SchedulePaint();
  }

  // Windows' Snap Layouts flyout takes the pointer's events as it shows over
  // Maximize, the pointer still on the button: it stays lit, as Windows' own
  // does (Paper 11.3), until the pointer is off it.
  void OnMouseExited(const ui::MouseEvent& event) override {
    if (kind_ == Kind::kMaximize && PointerOver()) {
      exit_check_.Start(FROM_HERE, base::Milliseconds(100),
                        base::BindRepeating(&CaptionButton::CheckPointer,
                                            base::Unretained(this)));
      return;
    }
    views::Button::OnMouseExited(event);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const bool lit = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    // A hover fades in and out on Pane's hover (motion::Hover), as Chrome's
    // button hover; a press shows at once, as does a window going inactive.
    hover_.MoveTo(lit ? 1 : 0, motion::Hover(),
                  GetWidget() ? GetWidget()->GetCompositor() : nullptr);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    SkColor fill = SK_ColorTRANSPARENT;
    if (Forced()) {
      // A contrast theme's: Highlight under the pointer and pressed, as
      // Windows' own, with no red for Close (Paper 11.10).
      fill = GetState() == STATE_PRESSED
                 ? colors->GetColor(ui::kColorNativeHighlight)
                 : SkColorSetA(colors->GetColor(ui::kColorNativeHighlight),
                               gfx::Tween::IntValueBetween(hover_.value(),
                                                           SK_AlphaTRANSPARENT,
                                                           SK_AlphaOPAQUE));
    } else if (kind_ == Kind::kClose) {
      fill =
          GetState() == STATE_PRESSED
              ? SkColorSetA(kCloseRed, kClosePressed)
              : SkColorSetA(kCloseRed, gfx::Tween::IntValueBetween(
                                           hover_.value(), SK_AlphaTRANSPARENT,
                                           SK_AlphaOPAQUE));
    } else {
      const SkColor base = GlyphColor(*colors);
      fill = GetState() == STATE_PRESSED
                 ? SkColorSetA(base, kPressed)
                 : SkColorSetA(base, gfx::Tween::IntValueBetween(
                                         hover_.value(), SK_AlphaTRANSPARENT,
                                         kHovered));
    }
    if (SkColorGetA(fill) == SK_AlphaTRANSPARENT) {
      return;
    }
    gfx::ScopedCanvas scoped(canvas);
    // Close's red, in the panel's corner, is rounded by it.
    canvas->ClipPath(SquirclePath(gfx::RectF(panel_), radius::kPanel),
                     /*do_anti_alias=*/true);
    canvas->FillRect(cell_, fill);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const bool lit = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    SkColor symbol = GlyphColor(*colors);
    if (Forced()) {
      symbol = gfx::Tween::ColorValueBetween(
          hover_.value(), colors->GetColor(ui::kColorNativeWindowText),
          colors->GetColor(ui::kColorNativeHighlightText));
    } else if (!GetWidget()->ShouldPaintAsActive() && !lit) {
      symbol = SkColorSetA(symbol, kInactiveGlyph);
    } else if (kind_ == Kind::kClose) {
      symbol =
          gfx::Tween::ColorValueBetween(hover_.value(), symbol, SK_ColorWHITE);
    }

    // As Chrome paints its glyphs: in pixels, 10 by 10 in the cell's middle,
    // a stroke of a pixel per point, no antialiasing.
    gfx::ScopedCanvas scoped(canvas);
    const float scale = canvas->UndoDeviceScaleFactor();
    const int symbol_size = base::ClampRound(10 * scale);
    gfx::RectF cell(cell_);
    cell.Scale(scale);
    gfx::Rect symbol_rect = gfx::ToEnclosingRect(cell);
    symbol_rect.ClampToCenteredSize(gfx::Size(symbol_size, symbol_size));
    cc::PaintFlags flags;
    flags.setAntiAlias(false);
    flags.setColor(symbol);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    const int stroke = base::ClampRound(scale);
    flags.setStrokeWidth(stroke);
    switch (kind_) {
      case Kind::kMinimize:
        painter_->PaintMinimizeIcon(canvas, symbol_rect, flags);
        return;
      case Kind::kMaximize:
        if (maximized_) {
          painter_->PaintRestoreIcon(canvas, symbol_rect, flags);
        } else {
          painter_->PaintMaximizeIcon(canvas, symbol_rect, flags);
        }
        return;
      case Kind::kClose:
        // Chrome's halo of transparent pixels round the X, brighter when
        // it is white.
        flags.setStrokeWidth(stroke +
                             stroke * (symbol == SK_ColorWHITE ? 0.1f : 0.05f));
        painter_->PaintCloseIcon(canvas, symbol_rect, flags);
        return;
    }
  }

 private:
  // Black on Pane's light panel, white on its dark one, by the luma Chrome
  // reads under its own (kColorCaptionButtonOnToolbar).
  static SkColor GlyphColor(const ui::ColorProvider& colors) {
    return color_utils::IsDark(colors.GetColor(kPaneColorCard)) ? SK_ColorWHITE
                                                                : SK_ColorBLACK;
  }

  void HoverStepped(double value) { SchedulePaint(); }

  bool PointerOver() const {
    return GetBoundsInScreen().Contains(
        display::Screen::Get()->GetCursorScreenPoint());
  }

  // The flyout gone, or the pointer off the button: the hover goes.
  void CheckPointer() {
    if (PointerOver()) {
      return;
    }
    exit_check_.Stop();
    if (GetState() == STATE_HOVERED) {
      SetState(STATE_NORMAL);
    }
  }

  // Under one of Windows' contrast themes.
  bool Forced() const {
    return GetNativeTheme()->forced_colors() !=
           ui::ColorProviderKey::ForcedColors::kNone;
  }

  // Each reads its name, as Chrome names them, and Narrator reaches them as
  // the title bar's buttons (checked with UI Automation, commit 490757e).
  void UpdateName() {
    switch (kind_) {
      case Kind::kMinimize:
        GetViewAccessibility().SetName(u"Minimize");
        return;
      case Kind::kMaximize:
        GetViewAccessibility().SetName(maximized_ ? u"Restore" : u"Maximize");
        return;
      case Kind::kClose:
        GetViewAccessibility().SetName(u"Close");
        return;
    }
  }

  const Kind kind_;
  // Windows 11's glyphs, Windows 10's before it, as Chrome picks them.
  const std::unique_ptr<Windows10IconPainter> painter_ =
      base::win::GetVersion() >= base::win::Version::WIN11
          ? std::make_unique<Windows11IconPainter>()
          : std::make_unique<Windows10IconPainter>();
  gfx::Rect cell_;
  gfx::Rect panel_;
  bool maximized_ = false;
  ValueMotion hover_;
  base::RepeatingTimer exit_check_;
};

BEGIN_METADATA(CaptionButton)
END_METADATA

class CaptionButtons : public WindowControls, public views::WidgetObserver {
  METADATA_HEADER(CaptionButtons, WindowControls)

 public:
  CaptionButtons() {
    SetID(kViewIdWindowControls);
    // Over the panel's card and the toolbar, each a layer of its own.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Window controls");
    for (Kind kind : {Kind::kMinimize, Kind::kMaximize, Kind::kClose}) {
      buttons_[static_cast<size_t>(kind)] =
          AddChildView(std::make_unique<CaptionButton>(
              kind, base::BindRepeating(&CaptionButtons::Pressed,
                                        base::Unretained(this), kind)));
    }
  }

  // WindowControls:
  int RowWidth() const override { return kRowWidth; }
  int RowHeight() const override { return kButtonHeight; }

  // The window lays out its controls with the panel at each of its own
  // layouts: one invalidated each time, unchanged, took the next frame's
  // layout of the whole window, at every frame of every animation.
  void SetPanel(const gfx::Rect& panel) override {
    if (panel == panel_) {
      return;
    }
    panel_ = panel;
    InvalidateLayout();
  }

  int ComponentAt(const gfx::Point& point) override {
    for (CaptionButton* button : buttons_) {
      if (button->GetVisible() && button->bounds().Contains(point)) {
        switch (button->kind()) {
          case Kind::kMinimize:
            return HTMINBUTTON;
          case Kind::kMaximize:
            return HTMAXBUTTON;
          case Kind::kClose:
            return HTCLOSE;
        }
      }
    }
    return HTNOWHERE;
  }

  // views::View:
  void Layout(PassKey) override {
    // The row's top right at the panel's; Close reaches the view's own top
    // and right, the frame's gutter there when the window is maximized.
    const int row_x = panel_.right() - kRowWidth;
    const int row_y = panel_.y();
    for (CaptionButton* button : buttons_) {
      const int index = static_cast<int>(button->kind());
      const gfx::Rect cell(row_x + index * (kButtonWidth + kSpacing), row_y,
                           kButtonWidth, kButtonHeight);
      gfx::Rect bounds = cell;
      if (button->kind() == Kind::kClose) {
        bounds = gfx::Rect(cell.x(), 0, width() - cell.x(), cell.bottom());
      }
      button->SetBoundsRect(bounds);
      gfx::Rect cell_in_button = cell;
      cell_in_button.Offset(-bounds.x(), -bounds.y());
      gfx::Rect panel_in_button = panel_;
      panel_in_button.Offset(-bounds.x(), -bounds.y());
      button->SetCell(cell_in_button, panel_in_button);
    }
  }

  void AddedToWidget() override {
    widget_observation_.Observe(GetWidget());
    paint_as_active_ =
        GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
            &CaptionButtons::SchedulePaintAll, base::Unretained(this)));
    ShowState();
  }

  void RemovedFromWidget() override {
    paint_as_active_ = {};
    widget_observation_.Reset();
  }

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override {
    ShowState();
  }

  void OnWidgetDestroying(views::Widget* widget) override {
    widget_observation_.Reset();
  }

 private:
  void Pressed(Kind kind) {
    views::Widget* widget = GetWidget();
    if (!widget) {
      return;
    }
    switch (kind) {
      case Kind::kMinimize:
        widget->Minimize();
        return;
      case Kind::kMaximize:
        if (widget->IsMaximized()) {
          widget->Restore();
        } else {
          widget->Maximize();
        }
        return;
      // The window's close, which quits once the pages agree, without Exit
      // Pane? (ADR 0053).
      case Kind::kClose:
        widget->Close();
        return;
    }
  }

  // Restore in Maximize's place while the window is maximized.
  void ShowState() {
    if (views::Widget* widget = GetWidget()) {
      buttons_[static_cast<size_t>(Kind::kMaximize)]->SetMaximized(
          widget->IsMaximized());
    }
  }

  void SchedulePaintAll() {
    for (CaptionButton* button : buttons_) {
      button->SchedulePaint();
    }
  }

  std::array<raw_ptr<CaptionButton>, 3> buttons_;
  gfx::Rect panel_;
  base::CallbackListSubscription paint_as_active_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

BEGIN_METADATA(CaptionButtons)
END_METADATA

}  // namespace

// static
std::unique_ptr<WindowControls> WindowControls::Make() {
  return std::make_unique<CaptionButtons>();
}

}  // namespace pane
