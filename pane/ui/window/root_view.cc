// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/window/root_view.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "pane/ui/after_frame.h"
#include "pane/ui/design/accessible_layers.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/system_words.h"
#include "pane/ui/design/tab_order.h"
#include "pane/ui/design/theme.h"
#include "pane/ui/dialogs/about_dialog.h"
#include "pane/ui/dialogs/extension_prompt_view.h"
#include "pane/ui/dialogs/page_dialog.h"
#include "pane/ui/dialogs/system_alert_dialog.h"
#include "pane/ui/extensions/extension_actions_view.h"
#include "pane/ui/extensions/extensions_ui.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/page/devtools.h"
#include "pane/ui/page/exclusive_access_hint.h"
#include "pane/ui/page/page_web_view.h"
#include "pane/ui/panels/app_icon_picker.h"
#include "pane/ui/panels/settings_view.h"
#include "pane/ui/view_ids.h"
#include "pane/ui/window/empty_state_view.h"
#include "pane/ui/window/focus_groups.h"
#include "pane/ui/window/panel_frame.h"
#include "pane/ui/window/quit_dialog.h"
#include "pane/ui/window/sad_tab_view.h"
#include "pane/ui/window/window_controls.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_solid_color.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace pane {

namespace {

// The window's least size, as the Electron app's (ADR 0036).
constexpr gfx::Size kMinimumSize(900, 600);

// The content panel: flush with the sidebar, 8 from the window's other edges
// (m-2 ml-0), rounded at the panel's 18 (PanelFrame draws it).
constexpr int kPanelInset = 8;

// The profile sheet's panel: 8 from the window's top, right, and bottom
// (Paper 05.4); its ring outside it, its shadow 0 8px 40px, black at 50% in
// both appearances (the Swift's surfaceBackground).
constexpr int kSheetInset = 8;
constexpr float kSheetShadowSigma = 20;
constexpr float kSheetShadowOffset = 8;
constexpr SkColor kSheetShadow = SkColorSetA(SK_ColorBLACK, 0x80);

// Over the page while a page's dialog shows: the page takes no press, as
// the Swift's clear shape over it took them, nor a scroll.
class PageShield : public views::View {
  METADATA_HEADER(PageShield, views::View)

 public:
  PageShield() { GetViewAccessibility().SetIsIgnored(true); }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override { return true; }
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override { return true; }
};

BEGIN_METADATA(PageShield)
END_METADATA

// The extensions menu's Esc, and Esc and Return for a page's dialog, ahead of
// the focused view (a field of its own, the address, the sidebar's), as the
// Swift's key monitor took them: on the Mac, Views hands a key to the focused
// view before the FocusManager's accelerators, and the address takes Esc and
// Return itself.
class PageDialogKeys : public ui::EventHandler {
 public:
  explicit PageDialogKeys(RootView* root) : root_(root) {}

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override {
    if (root_->OverlayKey(*event)) {
      event->SetHandled();
    }
  }

 private:
  const raw_ptr<RootView> root_;
};

// Under find in page's bar, or the extensions menu, while it shows, over the
// rest of the window: a press anywhere else closes it, and reaches nothing
// under it, nor does a scroll (the Swift's FindOverlay, and the clear shape
// under its extensions menu, each over the window and taking the tap).
class FindShield : public views::View {
  METADATA_HEADER(FindShield, views::View)

 public:
  explicit FindShield(base::RepeatingClosure close) : close_(std::move(close)) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    close_.Run();
    return true;
  }
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override { return true; }

 private:
  base::RepeatingClosure close_;
};

BEGIN_METADATA(FindShield)
END_METADATA

// A page's dialog: its top at three eighths of the window, raised to leave
// 16 under it, and never closer than 8 to the window's top (the Swift's
// AlertPlacement).
constexpr int kPageDialogMargin = 16;
constexpr int kPageDialogTop = 8;
// The extensions menu: 4 under the puzzle, and 8 from the window's leading
// edge at least.
constexpr int kMenuGap = 4;
constexpr int kMenuMargin = 8;

}  // namespace

// The profile sheet over the window (the Swift's, ADR 0120): its scrim over
// the whole window, a press on which closes it, and its panel at the
// window's right on its ring and shadow, one DialogCard. It comes as the
// scrim fades in and the panel slides in from the window's right as it fades
// in (Motion.sheet, SwiftUI's move from the trailing edge by the width of
// what it moves, the window's), and goes the same way; each on the
// compositor's own animation. While it shows, Esc cancels and Return
// creates, whatever in it has the keys.
class ProfileSheetOverlay : public views::View {
  METADATA_HEADER(ProfileSheetOverlay, views::View)

 public:
  explicit ProfileSheetOverlay(ProfileSheet::Callbacks callbacks) {
    SetVisible(false);
    scrim_ = AddChildView(std::make_unique<views::View>());
    scrim_->SetPaintToLayer(::ui::LAYER_SOLID_COLOR);
    scrim_->layer()->SetFillsBoundsOpaquely(false);
    scrim_->layer()->SetOpacity(0);
    scrim_->GetViewAccessibility().SetIsIgnored(true);
    DialogCard::Look look;
    look.radius = radius::kPanel;
    look.fill = kPaneColorCard;
    look.edge = kPaneColorBorder;
    look.shadow = kSheetShadow;
    look.shadow_offset = gfx::Vector2dF(0, kSheetShadowOffset);
    look.shadow_sigma = kSheetShadowSigma;
    auto sheet = std::make_unique<ProfileSheet>(std::move(callbacks));
    sheet_ = sheet.get();
    card_ = AddChildView(std::make_unique<DialogCard>(look, std::move(sheet)));
  }

  ProfileSheet* sheet() { return sheet_; }
  bool showing() const { return GetVisible() && !closing_; }
  gfx::Rect CardBounds() const { return card_->CardBounds(); }

  // `rested` once the slide in ends, at once if the sheet is still; dropped
  // when it goes instead.
  void WhenRests(base::OnceClosure rested) {
    if (!showing()) {
      return;
    }
    if (!moving_) {
      std::move(rested).Run();
      return;
    }
    rested_.push_back(std::move(rested));
  }

  void AnimateIn() {
    closing_ = false;
    SetVisible(true);
    SetCanProcessEventsWithinSubtree(true);
    ResetAccelerators();
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
    Place();
    // From the window's right, faded, unless it is on its way out.
    if (!moving_) {
      scrim_->layer()->SetOpacity(0);
      card_->layer()->SetOpacity(0);
      card_->layer()->SetTransform(Away());
    }
    Move(true);
  }

  void AnimateOut() {
    closing_ = true;
    ResetAccelerators();
    SetCanProcessEventsWithinSubtree(false);
    Move(false);
  }

  // views::View:
  void Layout(PassKey) override {
    // A window resized under it: there at once.
    if (Place() && !moving_ && showing()) {
      card_->layer()->SetTransform(gfx::Transform());
    }
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    scrim_->layer()->AsSolidColor()->SetColor(SkColor4f::FromColor(
        GetColorProvider()->GetColor(kPaneColorSheetScrim)));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // A press beside the panel, on the scrim, closes it.
    if (showing()) {
      sheet_->Cancel();
    }
    return true;
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (!showing()) {
      return false;
    }
    if (accelerator.key_code() == ui::VKEY_ESCAPE) {
      sheet_->Cancel();
    } else {
      sheet_->Submit();
    }
    return true;
  }

 private:
  // The panel's place, its ring included; whether it moved.
  bool Place() {
    scrim_->SetBoundsRect(GetLocalBounds());
    return card_->Place(gfx::RectF(
        width() - ProfileSheet::kWidth - kSheetInset - 1, kSheetInset - 1,
        ProfileSheet::kWidth + 2, std::max(height() - 2 * kSheetInset, 0) + 2));
  }

  // Off past the window's right, by the window's width.
  gfx::Transform Away() const {
    return gfx::Transform::MakeTranslation(width(), 0);
  }

  void Move(bool shown) {
    moving_ = true;
    CompositorMotion::Start(scrim_->layer(), motion::Sheet(),
                            {.opacity = shown ? 1.0f : 0.0f});
    CompositorMotion::Start(card_->layer(), motion::Sheet(),
                            {.opacity = shown ? 1.0f : 0.0f,
                             .transform = shown ? gfx::Transform() : Away()},
                            base::BindOnce(&ProfileSheetOverlay::Moved,
                                           weak_factory_.GetWeakPtr(), shown));
  }

  void Moved(bool shown) {
    moving_ = false;
    if (!shown && closing_) {
      SetVisible(false);
    }
    std::vector<base::OnceClosure> rested = std::exchange(rested_, {});
    if (showing()) {
      for (base::OnceClosure& each : rested) {
        std::move(each).Run();
      }
    }
  }

  raw_ptr<views::View> scrim_ = nullptr;
  raw_ptr<DialogCard> card_ = nullptr;
  raw_ptr<ProfileSheet> sheet_ = nullptr;
  bool closing_ = false;
  bool moving_ = false;
  // What waits for the slide in to end.
  std::vector<base::OnceClosure> rested_;
  base::WeakPtrFactory<ProfileSheetOverlay> weak_factory_{this};
};

BEGIN_METADATA(ProfileSheetOverlay)
END_METADATA

RootView::Callbacks::Callbacks() = default;
RootView::Callbacks::Callbacks(Callbacks&&) = default;
RootView::Callbacks& RootView::Callbacks::operator=(Callbacks&&) = default;
RootView::Callbacks::~Callbacks() = default;

RootView::Shows::Shows() = default;
RootView::Shows::Shows(const Shows&) = default;
RootView::Shows& RootView::Shows::operator=(const Shows&) = default;
RootView::Shows::~Shows() = default;

// static
RootView* RootView::Of(views::Widget* widget) {
  views::WidgetDelegate* delegate =
      widget ? widget->widget_delegate() : nullptr;
  return delegate ? views::AsViewClass<RootView>(delegate->GetContentsView())
                  : nullptr;
}

RootView::RootView(Callbacks callbacks,
                   model::ToolbarProgress* progress,
                   AppIconImages* images)
    : callbacks_(std::move(callbacks)), images_(images) {
  // The window's own layer holds this color and nothing else, so it has no
  // tiles: what draws over it draws on layers of its own, each as small as
  // what it draws (ADR 0073).
  SetBackground(views::CreateSolidBackground(kPaneColorWindowBackground));

  // The panel's card, on its shadow and in its ring, under everything.
  frame_ = AddChildView(std::make_unique<PanelFrame>());

  // The sidebar's column, and its edge over it and the panel's, under every
  // overlay that comes after.
  sidebar_ = AddChildView(
      std::make_unique<SidebarView>(std::move(callbacks_.sidebar)));

  // The toolbar's row, at the panel's top while a tab shows, over the card;
  // its extensions are the app's.
  callbacks_.toolbar.extensions = callbacks_.extensions;
  toolbar_ = AddChildView(
      std::make_unique<ToolbarView>(std::move(callbacks_.toolbar), progress));
  toolbar_->SetPaintToLayer();
  toolbar_->layer()->SetFillsBoundsOpaquely(false);
  // Their buttons take no Tab on Windows, as Chrome's toolbar's: F6,
  // Alt+Shift+T, and the arrows reach them (tab_order.h, focus_groups.h).
  KeepButtonsOutOfTab(*sidebar_);
  KeepButtonsOutOfTab(*toolbar_);
  focus_groups_ = FocusGroups::Make(*this);

  // DevTools docked to the page, under it in its card (ADR 0024).
  devtools_ = AddChildView(std::make_unique<PageWebView>(nullptr));
  devtools_->SetID(kViewIdDevTools);
  devtools_->GetViewAccessibility().SetName(u"Developer tools");
  devtools_->SetVisible(false);
  // The page in its card, and what shows there with no tab selected.
  page_ = AddChildView(std::make_unique<PageWebView>(nullptr));
  page_->SetID(kViewIdContent);
  page_->GetViewAccessibility().SetName(u"Web content");
  empty_ = AddChildView(std::make_unique<EmptyStateView>());
  sad_tab_ = AddChildView(
      std::make_unique<SadTabView>(std::move(callbacks_.reload_crashed)));
  // Over the card's middle, a layer the compositor fills.
  for (views::View* place : {static_cast<views::View*>(empty_.get()),
                             static_cast<views::View*>(sad_tab_.get())}) {
    place->SetPaintToLayer();
    place->layer()->SetFillsBoundsOpaquely(false);
  }
  // Settings, in the panel in the page's place: made now, so showing it
  // makes nothing.
  settings_ = AddChildView(
      std::make_unique<SettingsView>(std::move(callbacks_.settings), *images));
  // The window's own controls where Pane draws them (Windows' caption
  // buttons): in the panel's top-right corner, over the toolbar's end and
  // what shows with no toolbar, under the overlays but the PIN screen, whose
  // panel they sit on while it shows (Paper 11.1's B, 07.x, 06.4.1).
  if (std::unique_ptr<WindowControls> controls = WindowControls::Make()) {
    window_controls_ = AddChildView(std::move(controls));
    toolbar_->SetEndReserve(window_controls_->RowWidth());
  }
  // Over the page's place while a page's dialog shows, under the sidebar's
  // edge.
  page_shield_ = AddChildView(std::make_unique<PageShield>());
  page_shield_->SetVisible(false);
  // The page's dim, where the system draws one: a color the compositor
  // draws, over the page alone.
  if (ThisPageDialogStyle().dims_page) {
    page_dim_ = AddChildView(std::make_unique<views::View>());
    page_dim_->SetPaintToLayer(ui::LAYER_SOLID_COLOR);
    page_dim_->layer()->SetFillsBoundsOpaquely(false);
    page_dim_->layer()->SetOpacity(0);
    page_dim_->SetCanProcessEventsWithinSubtree(false);
    page_dim_->GetViewAccessibility().SetIsIgnored(true);
    page_dim_->SetVisible(false);
  }
  page_dialog_keys_ = std::make_unique<PageDialogKeys>(this);
  AddPreTargetHandler(page_dialog_keys_.get());
  resize_ = AddChildView(
      std::make_unique<ResizeHandle>(std::move(callbacks_.resize)));
  // Find in page's bar over the sidebar's edge, and its shield, under a
  // page's dialogs and what shows over the whole window; made now, so ⌘F
  // makes nothing.
  find_shield_ =
      AddChildView(std::make_unique<FindShield>(callbacks_.find_bar.close));
  find_shield_->SetVisible(false);
  auto find_bar = std::make_unique<FindBarView>(std::move(callbacks_.find_bar));
  find_bar_ = find_bar.get();
  find_card_ = AddChildView(std::make_unique<DialogCard>(
      FindBarView::CardLook(), std::move(find_bar)));
  find_card_->SetVisible(false);
  // The extensions menu and the more menu, over a page's dialog and under
  // the switcher; made now, so the puzzle's first press makes nothing.
  extensions_shield_ = AddChildView(std::make_unique<FindShield>(
      base::BindRepeating(&ExtensionsUi::CloseMenu,
                          base::Unretained(callbacks_.extensions.get()))));
  extensions_shield_->SetVisible(false);
  auto menu = std::make_unique<ExtensionsMenuView>(*callbacks_.extensions);
  extensions_menu_ = menu.get();
  extensions_card_ = AddChildView(std::make_unique<DialogCard>(
      ExtensionsMenuView::CardLook(), std::move(menu)));
  extensions_card_->SetVisible(false);
  auto more = std::make_unique<ExtensionMoreMenuView>(*callbacks_.extensions);
  more_menu_ = more.get();
  more_card_ = AddChildView(std::make_unique<DialogCard>(
      ExtensionsMenuView::CardLook(), std::move(more)));
  more_card_->SetVisible(false);
  // Over the rest, under the profile sheet; made now, so the first frame
  // that shows it makes nothing.
  switcher_ = AddChildView(std::make_unique<TabSwitcherView>());
  switcher_->SetVisible(false);
  // Quit Pane?, over everything else; made now, so the first ⌘Q makes
  // nothing. It takes every key while it shows.
  ModalDialog::Callbacks quit;
  quit.dismiss = callbacks_.cancel_quit;
  quit.accept = callbacks_.quit;
  // Every key on the Mac, Return accepting from anywhere, as macOS's default
  // button takes it; where the system's buttons take Return (Windows; Views'
  // PlatformStyle), a dialog's own keys, as Delete profile's alert has them:
  // Tab round Cancel and Exit, and Space or Enter pressing the one with the
  // ring (ADR 0126). With every key, Enter on a focused Cancel exited (W8c).
  quit_dialog_ = AddChildView(std::make_unique<ModalDialog>(
      QuitDialog::kWidth,
      std::make_unique<QuitDialog>(callbacks_.cancel_quit, callbacks_.quit),
      std::move(quit),
      views::PlatformStyle::kReturnClicksFocusedControl
          ? ModalDialog::Keys::kDialog
          : ModalDialog::Keys::kEvery));
  quit_dialog_->SetID(kViewIdQuit);
  // Read as a modal dialog named by its title, as Delete profile's alert and
  // About Pane are: unnamed, Narrator said "dialog" alone (W8c).
  quit_dialog_->GetViewAccessibility().SetName(
      std::u16string(Words().quit_question));
  // The PIN screen, over everything else but the hint; made now, so a Lock
  // makes nothing.
  lock_ = AddChildView(
      std::make_unique<LockScreenView>(std::move(callbacks_.lock)));
  // The full screen's Esc hint, over everything.
  hint_ = AddChildView(std::make_unique<ExclusiveAccessHint>());
  ShowPlaces();
}

RootView::~RootView() {
  RemovePreTargetHandler(page_dialog_keys_.get());
}

void RootView::SetSidebar(const SidebarContent& content) {
  sidebar_->SetContent(content);
}

void RootView::SetShows(const Shows& shows) {
  const bool content_changed = shows.content != shows_.content;
  const bool laid_out_again =
      content_changed ||
      std::round(shows.sidebar_width) != std::round(shows_.sidebar_width);
  // The page's place had the keys: the sad tab takes them in it, and the
  // page back from it.
  const bool place_focused = page_->HasFocus() || sad_tab_->HasFocus();
  shows_ = shows;
  toolbar_->SetShows(shows.toolbar);
  settings_->SetShows(shows.settings);
  ShowAppIconPicker(shows.app_icon_picker);
  if (app_icon_picker_) {
    app_icon_picker_->SetSelection(shows.settings.settings.app_icon);
  }
  // The picker, made once Settings' first frame is out, so its first
  // showing makes nothing.
  if (shows.content == Content::kSettings && !app_icon_dialog_ && GetWidget()) {
    AfterFrame(GetWidget()->GetCompositor(),
               base::BindOnce(&RootView::PrepareAppIconPicker,
                              weak_factory_.GetWeakPtr()));
  }
  sidebar_->SetCanCollapseAll(shows.can_collapse_all);
  resize_->SetWidth(shows.sidebar_width);
  if (laid_out_again) {
    ShowPlaces();
  }
  if (content_changed && place_focused &&
      (shows.content == Content::kPage || shows.content == Content::kSadTab)) {
    FocusPage();
  }
  ShowLock(shows.lock);
}

void RootView::ReturnKeys(views::View* before) {
  if (lock_->takes_every_key()) {
    return;
  }
  if (before && before->IsDrawn() && before->IsFocusable()) {
    before->RequestFocus();
  } else {
    FocusPage();
  }
}

void RootView::ShowLock(const LockScreenView::Shows& shows) {
  // What the window shows before its first frame is out is there in it, the
  // PIN screen at launch included; after, the screen rises and sinks.
  if (!first_frame_requested_ && GetWidget() && GetWidget()->GetCompositor()) {
    first_frame_requested_ = true;
    AfterFrame(GetWidget()->GetCompositor(),
               base::BindOnce(
                   [](base::WeakPtr<RootView> root) {
                     if (root) {
                       root->first_frame_out_ = true;
                     }
                   },
                   weak_factory_.GetWeakPtr()));
  }
  const bool took_keys = lock_->takes_every_key();
  lock_->SetShows(shows, /*animate=*/first_frame_out_);
  const bool takes_keys = lock_->takes_every_key();
  PlaceWindowControls();
  if (takes_keys && !took_keys) {
    views::FocusManager* focus = GetFocusManager();
    views::View* focused = focus ? focus->GetFocusedView() : nullptr;
    focus_before_lock_.SetView(lock_->Contains(focused) ? nullptr : focused);
    RaiseTopOverlays();
    lock_->TakeFocus();
  } else if (took_keys && !takes_keys) {
    // The keys go back to what had them, else to the page, or Settings.
    views::View* before = focus_before_lock_.view();
    focus_before_lock_.SetView(nullptr);
    if (before && before->IsDrawn() && before->IsFocusable()) {
      before->RequestFocus();
    } else if (settings_->GetVisible()) {
      FocusSettings();
    } else {
      FocusPage();
    }
    // With nothing to take them, no page showing yet, they leave the PIN's
    // field as it goes, as a launch with no PIN gives no view the keys: they
    // stayed in it, hidden, where a screen reader still read it (W8d).
    views::FocusManager* focus = GetFocusManager();
    if (focus && lock_->Contains(focus->GetFocusedView())) {
      focus->ClearFocus();
    }
  }
}

void RootView::RaiseTopOverlays() {
  ReorderChildView(lock_, children().size());
  PlaceWindowControls();
  ReorderChildView(hint_, children().size());
  UpdateScreenReaderLayers();
}

void RootView::PlaceWindowControls() {
  if (!window_controls_) {
    return;
  }
  if (lock_->showing()) {
    ReorderChildView(window_controls_, GetIndexOf(lock_).value() + 1);
  } else {
    ReorderChildView(window_controls_, GetIndexOf(settings_).value() + 1);
  }
}

int RootView::NonClientHitTest(const gfx::Point& point) {
  if (window_controls_ && window_controls_->GetVisible()) {
    gfx::Point in_controls = point;
    views::View::ConvertPointToTarget(this, window_controls_, &in_controls);
    if (const int component = window_controls_->ComponentAt(in_controls);
        component != HTNOWHERE) {
      return component;
    }
  }
  return IsDragArea(point) ? HTCAPTION : HTNOWHERE;
}

void RootView::ShakePin() {
  lock_->Shake();
}

void RootView::SetSidebarWidth(double width) {
  if (std::round(width) == std::round(shows_.sidebar_width)) {
    return;
  }
  shows_.sidebar_width = width;
  resize_->SetWidth(width);
  InvalidateLayout();
  SchedulePaint();
}

// What shows goes or comes at once, not at the next layout: a new tab's
// address field takes the keys in the same call that makes the toolbar show.
void RootView::ShowPlaces() {
  // The sidebar steps aside while a page fills the window, as the toolbar
  // does, which shows over a tab's page on the browser page.
  sidebar_->SetVisible(!page_fullscreen_);
  resize_->SetVisible(!page_fullscreen_);
  const bool tab =
      shows_.content == Content::kPage || shows_.content == Content::kSadTab;
  toolbar_->SetVisible(tab && !page_fullscreen_);
  empty_->SetVisible(shows_.content == Content::kEmpty && !page_fullscreen_);
  // The sad tab takes the page's place, on the panel's card: the page that
  // died leaves it.
  sad_tab_->SetVisible(shows_.content == Content::kSadTab && !page_fullscreen_);
  // Settings takes the panel, and the page leaves it, hidden, so it draws
  // nothing under Settings, which never masks it (ADR 0073).
  settings_->SetVisible(shows_.content == Content::kSettings &&
                        !page_fullscreen_);
  page_->SetVisible(shows_.content == Content::kPage);
  devtools_->SetVisible(shows_.content == Content::kPage &&
                        devtools_->web_contents());
  ShowFindPlaces();
  InvalidateLayout();
  SchedulePaint();
}

void RootView::SetPageFullscreen(bool fullscreen) {
  if (fullscreen == page_fullscreen_) {
    return;
  }
  page_fullscreen_ = fullscreen;
  RoundPage();
  ShowPlaces();
}

void RootView::ShowPage(content::WebContents* contents) {
  if (page_->web_contents() == contents) {
    return;
  }
  // The page that had the keys goes, none in its place yet (a new tab's
  // comes after it): Views hands them to the next Tab stop as the view
  // stops taking them (WebView::LostMainFrame), a button on Windows, whose
  // ring would show until the page comes and takes them. None holds them
  // meanwhile.
  const bool had_keys = !contents && page_->HasFocus();
  page_->SetWebContents(contents);
  if (views::FocusManager* focus = GetFocusManager(); had_keys && focus) {
    focus->ClearFocus();
  }
  // Another page's DevTools go, and this one's come.
  UpdateDevTools();
  RoundPage();
}

void RootView::UpdateDevTools() {
  content::WebContents* devtools =
      DockedDevToolsFor(page_->web_contents(), gfx::Rect()).devtools;
  if (devtools_->web_contents() != devtools) {
    // They had the keys: the page takes them as they go.
    const bool had_focus =
        devtools_->HasFocus() ||
        (GetFocusManager() &&
         devtools_->Contains(GetFocusManager()->GetFocusedView()));
    devtools_->SetWebContents(devtools);
    if (!devtools && had_focus) {
      FocusPage();
    }
  }
  devtools_->SetVisible(devtools && shows_.content == Content::kPage);
  RoundPage();
  InvalidateLayout();
  SchedulePaint();
}

// The corners go on the page's own layer, which comes and goes with it
// (NativeViewHostMac has none to round with no page attached), and none while
// the page fills the window: the panel's, as a circle that follows the
// squircle within a fifth of a pixel (ADR 0057), so no mask composites the
// page each frame. All four, as the Swift's page layer rounds them, its top
// two under the toolbar too: the user's call over Paper's square ones, and
// 04.7 now draws them so. With DevTools docked, they fill the card and take
// its four, and the page those of the card's it meets, as the Swift's
// rounded host clipped both.
void RootView::RoundPage() {
  // Only as they change: each layout reaches here.
  const auto round = [](views::WebView* view,
                        const gfx::RoundedCornersF& corners) {
    if (view->holder()->GetNativeViewCornerRadii() != corners) {
      view->holder()->SetNativeViewCornerRadii(corners);
    }
  };
  const float radius = page_fullscreen_ ? 0 : kPanelLayerRadius;
  if (devtools_->web_contents()) {
    round(devtools_, gfx::RoundedCornersF(radius));
  }
  if (!page_->web_contents()) {
    return;
  }
  if (!devtools_->web_contents()) {
    round(page_, gfx::RoundedCornersF(radius));
    return;
  }
  const gfx::Rect place = devtools_->bounds();
  const gfx::Rect page = page_->bounds();
  const bool top = page.y() == place.y();
  const bool bottom = page.bottom() == place.bottom();
  const bool left = page.x() == place.x();
  const bool right = page.right() == place.right();
  round(page_, gfx::RoundedCornersF(
                   top && left ? radius : 0, top && right ? radius : 0,
                   bottom && right ? radius : 0, bottom && left ? radius : 0));
}

content::WebContents* RootView::page() const {
  return page_->web_contents();
}

std::u16string RootView::address() const {
  return toolbar_->address();
}

std::string RootView::focus() const {
  const views::FocusManager* manager =
      GetWidget() ? GetWidget()->GetFocusManager() : nullptr;
  const views::View* focused = manager ? manager->GetFocusedView() : nullptr;
  if (!focused) {
    return "none";
  }
  const std::string_view name = NearestViewIdName(focused);
  return std::string(name.empty() ? focused->GetClassName() : name);
}

void RootView::FocusPage() {
  if (sad_tab_->GetVisible()) {
    sad_tab_->RequestFocus();
  } else if (page_->web_contents()) {
    page_->RequestFocus();
  }
}

void RootView::FocusSettings() {
  if (settings_->GetVisible()) {
    settings_->RequestFocus();
  }
}

void RootView::FocusAddress() {
  toolbar_->FocusAddress();
}

void RootView::PrepareAppIconPicker() {
  if (app_icon_dialog_) {
    return;
  }
  auto picker = std::make_unique<AppIconPicker>(
      *images_, shows_.settings.settings.app_icon, callbacks_.choose_app_icon,
      callbacks_.close_app_icon_picker);
  app_icon_picker_ = picker.get();
  ModalDialog::Callbacks dialog;
  dialog.dismiss = callbacks_.close_app_icon_picker;
  dialog.accept = callbacks_.close_app_icon_picker;
  app_icon_dialog_ = AddChildView(std::make_unique<ModalDialog>(
      AppIconPicker::kWidth, std::move(picker), std::move(dialog)));
  app_icon_dialog_->SetID(kViewIdAppIconPicker);
  // Named by its title, as Quit Pane? is.
  app_icon_dialog_->GetViewAccessibility().SetName(AppIconPicker::kTitle);
  app_icon_dialog_->SetBoundsRect(GetLocalBounds());
  RaiseTopOverlays();
}

AppIconPicker* RootView::app_icon_picker() {
  return app_icon_dialog_ && app_icon_dialog_->showing()
             ? app_icon_picker_.get()
             : nullptr;
}

void RootView::ShowAppIconPicker(bool show) {
  if (!show) {
    // It fades as it goes, taking nothing meanwhile; Settings has the keys
    // again.
    if (app_icon_dialog_ && app_icon_dialog_->showing()) {
      app_icon_dialog_->AnimateOut();
      if (!lock_->takes_every_key()) {
        FocusSettings();
      }
    }
    return;
  }
  PrepareAppIconPicker();
  if (app_icon_dialog_->showing()) {
    return;
  }
  app_icon_picker_->SetSelection(shows_.settings.settings.app_icon);
  // Over whatever came after it, under the PIN screen.
  ReorderChildView(app_icon_dialog_, children().size());
  RaiseTopOverlays();
  app_icon_dialog_->SetBoundsRect(GetLocalBounds());
  app_icon_dialog_->AnimateIn();
}

void RootView::ShowQuitDialog(bool show) {
  if (!show) {
    if (!quit_dialog_->showing()) {
      return;
    }
    // It fades as it goes, taking nothing meanwhile; the keys go back to
    // what had them, or to the page.
    quit_dialog_->AnimateOut();
    views::View* before = focus_before_quit_.view();
    focus_before_quit_.SetView(nullptr);
    ReturnKeys(before);
    return;
  }
  if (quit_dialog_->showing()) {
    return;
  }
  views::FocusManager* focus = GetFocusManager();
  focus_before_quit_.SetView(focus ? focus->GetFocusedView() : nullptr);
  // Over whatever came after it: the App icon picker, the switcher; under the
  // PIN screen.
  ReorderChildView(quit_dialog_, children().size());
  RaiseTopOverlays();
  quit_dialog_->SetBoundsRect(GetLocalBounds());
  quit_dialog_->AnimateIn();
}

bool RootView::quit_dialog_showing() const {
  return quit_dialog_->showing();
}

bool RootView::TakesEveryKey() const {
  return quit_dialog_->takes_every_key() ||
         (alert_dialog_ && alert_dialog_->takes_every_key()) ||
         (about_dialog_ && about_dialog_->takes_every_key()) ||
         lock_->takes_every_key();
}

void RootView::ShowAlert(const SystemAlert& alert,
                         base::OnceCallback<void(bool)> done) {
  AnswerAlert(false);
  views::FocusManager* focus = GetFocusManager();
  focus_before_alert_.SetView(focus ? focus->GetFocusedView() : nullptr);
  // Esc, Cancel, and a click outside it cancel; Return presses no button of
  // its own, the action never being the default.
  ModalDialog::Callbacks callbacks;
  callbacks.dismiss = base::BindRepeating(&RootView::AnswerAlert,
                                          weak_factory_.GetWeakPtr(), false);
  auto content = std::make_unique<SystemAlertDialog>(
      alert, callbacks.dismiss,
      base::BindRepeating(&RootView::AnswerAlert, weak_factory_.GetWeakPtr(),
                          true));
  alert_dialog_ = AddChildView(std::make_unique<ModalDialog>(
      SystemAlertDialog::kWidth, std::move(content), std::move(callbacks),
      ModalDialog::Keys::kDialog));
  alert_dialog_->SetID(kViewIdAlert);
  // Read as a modal dialog named by its title, as Quit Pane? is.
  alert_dialog_->GetViewAccessibility().SetName(alert.title);
  alert_done_ = std::move(done);
  // Over whatever came after it, under the PIN screen.
  RaiseTopOverlays();
  alert_dialog_->SetBoundsRect(GetLocalBounds());
  alert_dialog_->AnimateIn();
}

void RootView::AnswerAlert(bool chosen) {
  if (!alert_dialog_) {
    return;
  }
  ModalDialog* dialog = std::exchange(alert_dialog_, nullptr);
  dialog->AnimateOut(base::BindOnce(&RootView::DialogGone,
                                    weak_factory_.GetWeakPtr(), dialog));
  views::View* before = focus_before_alert_.view();
  focus_before_alert_.SetView(nullptr);
  ReturnKeys(before);
  UpdateScreenReaderLayers();
  if (alert_done_) {
    std::move(alert_done_).Run(chosen);
  }
}

void RootView::DialogGone(ModalDialog* dialog) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<RootView> root, ModalDialog* dialog) {
                       if (!root) {
                         return;
                       }
                       for (views::View* child : root->children()) {
                         if (child == dialog) {
                           root->RemoveChildViewT(dialog);
                           return;
                         }
                       }
                     },
                     weak_factory_.GetWeakPtr(), dialog));
}

void RootView::ShowAbout(const gfx::ImageSkia& icon,
                         const std::u16string& version) {
  if (!about_dialog_) {
    ModalDialog::Callbacks callbacks;
    callbacks.dismiss =
        base::BindRepeating(&RootView::HideAbout, weak_factory_.GetWeakPtr());
    callbacks.accept = callbacks.dismiss;
    auto content = std::make_unique<AboutDialog>(version, callbacks.dismiss);
    about_content_ = content.get();
    about_dialog_ = AddChildView(std::make_unique<ModalDialog>(
        AboutDialog::kWidth, std::move(content), std::move(callbacks),
        ModalDialog::Keys::kDialog));
    about_dialog_->SetID(kViewIdAbout);
    about_dialog_->GetViewAccessibility().SetName(u"About Pane");
  }
  about_content_->SetIcon(icon);
  if (about_dialog_->showing()) {
    return;
  }
  views::FocusManager* focus = GetFocusManager();
  focus_before_about_.SetView(focus ? focus->GetFocusedView() : nullptr);
  // Over whatever came after it, under the PIN screen.
  ReorderChildView(about_dialog_, children().size());
  RaiseTopOverlays();
  about_dialog_->SetBoundsRect(GetLocalBounds());
  about_dialog_->AnimateIn();
}

void RootView::HideAbout() {
  if (!about_dialog_ || !about_dialog_->showing()) {
    return;
  }
  about_dialog_->AnimateOut();
  views::View* before = focus_before_about_.view();
  focus_before_about_.SetView(nullptr);
  ReturnKeys(before);
}

SystemAlertDialog* RootView::alert() {
  return alert_dialog_ && alert_dialog_->showing()
             ? static_cast<SystemAlertDialog*>(alert_dialog_->content())
             : nullptr;
}

AboutDialog* RootView::about() {
  return about_dialog_ && about_dialog_->showing() ? about_content_.get()
                                                   : nullptr;
}

void RootView::DismissDialogs() {
  AnswerAlert(false);
  HideAbout();
}

views::Button* RootView::FocusedButtonIn(views::View* view) {
  views::FocusManager* focus = GetFocusManager();
  views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  return focused && view->Contains(focused)
             ? views::AsViewClass<views::Button>(focused)
             : nullptr;
}

void RootView::ShowPageDialog(std::unique_ptr<PageDialog> dialog) {
  HidePageDialog();
  PageDialog* content = dialog.get();
  // Under the extensions menu, the switcher, and what shows over the whole
  // window after them.
  page_dialog_ =
      AddChildViewAt(std::make_unique<DialogCard>(ThisPageDialogStyle().card,
                                                  std::move(dialog)),
                     GetIndexOf(extensions_shield_).value());
  page_shield_->SetVisible(true);
  DimPage(true);
  PlacePageDialogs();
  page_dialog_->Settle(false);
  page_dialog_entering_ = true;
  page_dialog_->Move(
      true, base::BindOnce(&RootView::PageDialogEntered,
                           weak_factory_.GetWeakPtr(), page_dialog_.get()));
  // What had the keys gets them back once the dialogs are answered.
  if (!focus_before_page_dialog_.view()) {
    views::FocusManager* focus = GetFocusManager();
    views::View* focused = focus ? focus->GetFocusedView() : nullptr;
    if (focused && !Contains(focused)) {
      focused = nullptr;
    }
    focus_before_page_dialog_.SetView(focused);
  }
  content->TakeFocus();
}

void RootView::HidePageDialog() {
  if (!page_dialog_) {
    return;
  }
  DialogCard* card = std::exchange(page_dialog_, nullptr);
  page_dialog_entering_ = false;
  static_cast<PageDialog*>(card->content())->Retire();
  card->SetCanProcessEventsWithinSubtree(false);
  page_dialogs_leaving_.push_back(card);
  page_shield_->SetVisible(false);
  DimPage(false);
  // It fades as it shrinks, then leaves the window.
  card->Move(false, base::BindOnce(&RootView::PageDialogLeft,
                                   weak_factory_.GetWeakPtr(), card));
  // The keys go back to what had them, or to the page.
  views::FocusManager* focus = GetFocusManager();
  const views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  if (!focused || card->Contains(focused)) {
    views::View* before = focus_before_page_dialog_.view();
    if (before && before->IsDrawn() && before->IsFocusable()) {
      before->RequestFocus();
    } else {
      FocusPage();
    }
  }
  focus_before_page_dialog_.SetView(nullptr);
}

PageDialog* RootView::page_dialog() {
  return page_dialog_ ? static_cast<PageDialog*>(page_dialog_->content())
                      : nullptr;
}

bool RootView::OverlayKey(const ui::KeyEvent& event) {
  const ui::KeyboardCode code = event.key_code();
  if (event.type() != ui::EventType::kKeyPressed ||
      (code != ui::VKEY_ESCAPE && code != ui::VKEY_RETURN) ||
      (event.flags() &
       (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN))) {
    return false;
  }
  // What shows over these has its keys first: the switcher, a dialog over
  // the whole window, and the PIN screen.
  if (switcher_->GetVisible() || quit_dialog_->showing() ||
      (app_icon_dialog_ && app_icon_dialog_->showing()) ||
      (alert_dialog_ && alert_dialog_->showing()) ||
      (about_dialog_ && about_dialog_->showing()) || lock_->takes_every_key()) {
    return false;
  }
  // An extension's prompt, over the menu and a page's dialog: Return
  // accepts, Esc cancels; where the system's buttons take Return, the one
  // with the ring presses as it (Windows; Views' PlatformStyle), as a page's
  // dialog's does (ADR 0126): Enter on a focused Cancel added the extension
  // (W8c).
  if (extension_prompt_) {
    if (code == ui::VKEY_RETURN &&
        views::PlatformStyle::kReturnClicksFocusedControl &&
        FocusedButtonIn(extension_prompt_)) {
      return false;
    }
    callbacks_.extensions->AnswerPrompt(code == ui::VKEY_RETURN);
    return true;
  }
  // The extensions menu, over a page's dialog: Esc closes its more menu,
  // then it; the rest goes on.
  if (extensions_card_->GetVisible()) {
    return code == ui::VKEY_ESCAPE && callbacks_.extensions->Escape();
  }
  PageDialog* dialog = page_dialog();
  if (!dialog) {
    return false;
  }
  // Where the system's buttons take Return, the one with the ring presses as
  // it (Windows; Views' PlatformStyle).
  if (code == ui::VKEY_RETURN &&
      views::PlatformStyle::kReturnClicksFocusedControl &&
      FocusedButtonIn(dialog)) {
    return false;
  }
  dialog->PressKey(code == ui::VKEY_ESCAPE);
  return true;
}

void RootView::DimPage(bool dimmed) {
  if (!page_dim_) {
    return;
  }
  if (dimmed) {
    page_dim_->SetVisible(true);
  }
  // On the card's curve, from where it stands; gone, it hides, unless
  // another dialog came meanwhile.
  CompositorMotion::Start(
      page_dim_->layer(), motion::Panel(), {.opacity = dimmed ? 1.0f : 0.0f},
      base::BindOnce(
          [](base::WeakPtr<RootView> root) {
            if (root && root->page_dim_ && !root->page_dialog_) {
              root->page_dim_->SetVisible(false);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void RootView::PlacePageDialogs() {
  const PageDialogStyle& style = ThisPageDialogStyle();
  const int line = std::round(height() * 3 / 8.0);
  // The page's middle, where the system centres its dialog on the page it
  // dims (Windows, Paper 11.18); else the window's.
  const float middle =
      style.centred_on_page
          ? page_->bounds().x() + page_->bounds().width() / 2.0f
          : width() / 2.0f;
  std::vector<DialogCard*> cards(page_dialogs_leaving_.begin(),
                                 page_dialogs_leaving_.end());
  if (page_dialog_) {
    cards.push_back(page_dialog_);
  }
  for (DialogCard* card : cards) {
    auto* dialog = static_cast<PageDialog*>(card->content());
    dialog->SetMostHeight(height() - line - kPageDialogMargin -
                          style.card.padding.height());
    const gfx::SizeF size = card->SizeFor(style.width);
    // The fill's top on the line, a ring outside it above it (Paper 04.15.1).
    const float ring = style.card.edge_outside ? style.card.edge_width : 0;
    const float top = std::max<float>(
        kPageDialogTop, std::min<float>(line - ring, height() - size.height() -
                                                         kPageDialogMargin));
    const bool moved = card->Place(gfx::RectF(middle - size.width() / 2, top,
                                              size.width(), size.height()));
    // The window changed under the one showing: there at once, or on to
    // the new place if it is on its way in.
    if (moved && card == page_dialog_) {
      if (page_dialog_entering_) {
        card->Move(true, base::BindOnce(&RootView::PageDialogEntered,
                                        weak_factory_.GetWeakPtr(), card));
      } else {
        card->Settle(true);
      }
    }
  }
}

void RootView::PageDialogEntered(DialogCard* card) {
  if (page_dialog_ == card) {
    page_dialog_entering_ = false;
  }
}

void RootView::PageDialogLeft(DialogCard* card) {
  if (std::erase(page_dialogs_leaving_, card)) {
    std::unique_ptr<DialogCard> gone = RemoveChildViewT(card);
  }
}

void RootView::PrepareProfileSheet() {
  if (profile_sheet_) {
    return;
  }
  // Over the switcher, under Quit Pane?, the App icon picker, and the hint.
  profile_sheet_ = AddChildViewAt(std::make_unique<ProfileSheetOverlay>(
                                      std::move(callbacks_.profile_sheet)),
                                  GetIndexOf(switcher_).value() + 1);
  profile_sheet_->SetBoundsRect(GetLocalBounds());
}

void RootView::ShowProfileSheet(const model::BrowserProfile* profile) {
  PrepareProfileSheet();
  if (GetWidget()) {
    LogInteraction(Interaction::kOpenProfileSheet,
                   GetWidget()->GetCompositor());
  }
  // What had the keys has them back once it goes.
  if (!profile_sheet_->showing()) {
    views::FocusManager* focus = GetFocusManager();
    views::View* focused = focus ? focus->GetFocusedView() : nullptr;
    focus_before_sheet_.SetView(
        focused && !profile_sheet_->Contains(focused) ? focused : nullptr);
  }
  profile_sheet_->sheet()->Begin(profile);
  profile_sheet_->SetBoundsRect(GetLocalBounds());
  profile_sheet_->AnimateIn();
  profile_sheet_->sheet()->FocusName();
}

void RootView::HideProfileSheet() {
  if (!profile_sheet_ || !profile_sheet_->showing()) {
    return;
  }
  profile_sheet_->AnimateOut();
  views::View* before = focus_before_sheet_.view();
  focus_before_sheet_.SetView(nullptr);
  ReturnKeys(before);
}

ProfileSheet* RootView::profile_sheet() {
  return profile_sheet_ && profile_sheet_->showing() ? profile_sheet_->sheet()
                                                     : nullptr;
}

void RootView::WhenProfileSheetRests(base::OnceClosure rested) {
  if (profile_sheet_) {
    profile_sheet_->WhenRests(std::move(rested));
  }
}

gfx::Rect RootView::ProfileSheetBounds() const {
  return profile_sheet_ ? profile_sheet_->CardBounds() +
                              profile_sheet_->bounds().OffsetFromOrigin()
                        : gfx::Rect();
}

void RootView::ShowSwitcher(std::vector<TabSwitcherView::Group> groups,
                            int selected) {
  switcher_->SetGroups(std::move(groups), selected);
  switcher_->SetBoundsRect(GetLocalBounds());
  switcher_->SetVisible(true);
}

void RootView::SelectInSwitcher(int selected) {
  switcher_->SetSelected(selected);
}

void RootView::HideSwitcher() {
  switcher_->SetVisible(false);
}

gfx::Rect RootView::PageBounds() const {
  return page_->bounds();
}

gfx::RoundedCornersF RootView::PageCorners() const {
  return page_->web_contents() ? page_->holder()->GetNativeViewCornerRadii()
                               : gfx::RoundedCornersF();
}

void RootView::ShowFind(const FindBarView::State& state) {
  find_bar_->SetState(state);
  ShowFindPlaces();
}

// While the browser page shows: Settings, which takes the page's place,
// hides it, as the Swift drew it only over the browser page.
void RootView::ShowFindPlaces() {
  const bool shown =
      find_bar_->state().visible && shows_.content != Content::kSettings;
  if (find_card_->GetVisible() == shown) {
    return;
  }
  const bool had_focus = find_bar_->field()->HasFocus();
  find_shield_->SetVisible(shown);
  find_card_->SetVisible(shown);
  if (shown) {
    find_card_->Settle(true);
    InvalidateLayout();
  } else if (had_focus) {
    FocusPage();
  }
}

void RootView::FocusFind() {
  if (find_card_->GetVisible()) {
    find_bar_->FocusField();
  }
}

bool RootView::FindHasFocus() const {
  return find_bar_->field()->HasFocus();
}

FindBarView* RootView::find_bar() {
  return find_card_->GetVisible() ? find_bar_.get() : nullptr;
}

gfx::Rect RootView::FindBarBounds() const {
  return find_card_->CardBounds();
}

void RootView::ShowExtensionsMenu(const ExtensionsMenuView::Shows& shows) {
  extensions_menu_->SetShows(shows);
  const InstalledExtension* more = nullptr;
  for (const InstalledExtension& extension : shows.extensions) {
    if (shows.more == extension.id) {
      more = &extension;
    }
  }
  if (more) {
    more_menu_->SetExtension(*more);
  }
  const bool opening = !extensions_card_->GetVisible();
  if (opening) {
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
    // Said as it opens: the keys stay where they were, and Narrator said
    // nothing of it (W8c).
    extensions_menu_->GetViewAccessibility().AnnouncePolitely(
        u"Extensions menu");
  }
  extensions_shield_->SetVisible(true);
  extensions_card_->SetVisible(true);
  more_card_->SetVisible(more != nullptr);
  PlaceExtensionsMenu();
  extensions_card_->Settle(true);
  more_card_->Settle(more != nullptr);
}

void RootView::HideExtensionsMenu() {
  if (!extensions_card_->GetVisible()) {
    return;
  }
  RemoveAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  extensions_shield_->SetVisible(false);
  extensions_card_->SetVisible(false);
  more_card_->SetVisible(false);
}

bool RootView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  return OverlayKey(ui::KeyEvent(ui::EventType::kKeyPressed,
                                 accelerator.key_code(),
                                 accelerator.modifiers()));
}

void RootView::ChildVisibilityChanged(views::View* child) {
  views::View::ChildVisibilityChanged(child);
  UpdateScreenReaderLayers();
}

void RootView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  views::View::ViewHierarchyChanged(details);
  if (details.parent == this) {
    UpdateScreenReaderLayers();
  }
  // A row or a button come to the toolbar or the sidebar takes no Tab on
  // Windows either (tab_order.h).
  if (details.is_add && toolbar_ && sidebar_ &&
      (toolbar_->Contains(details.child) ||
       sidebar_->Contains(details.child))) {
    KeepButtonsOutOfTab(*details.child);
  }
}

void RootView::RemovedFromWidget() {
  if (focus_groups_) {
    focus_groups_->Detach();
  }
}

bool RootView::MoveFocusGroup(const ui::Accelerator& key) {
  return focus_groups_ && !TakesEveryKey() && !page_fullscreen_ &&
         focus_groups_->Move(key);
}

bool RootView::InContent(const views::View* view) const {
  for (const views::View* place :
       {static_cast<const views::View*>(page_.get()),
        static_cast<const views::View*>(devtools_.get()),
        static_cast<const views::View*>(sad_tab_.get()),
        static_cast<const views::View*>(settings_.get()),
        static_cast<const views::View*>(empty_.get())}) {
    if (place->Contains(view)) {
      return true;
    }
  }
  return false;
}

bool RootView::FocusContent() {
  views::View* place = nullptr;
  if (settings_->GetVisible()) {
    FocusSettings();
    place = settings_;
  } else if (sad_tab_->GetVisible() ||
             (page_->GetVisible() && page_->web_contents())) {
    FocusPage();
    place = sad_tab_->GetVisible() ? static_cast<views::View*>(sad_tab_)
                                   : static_cast<views::View*>(page_);
  }
  const views::FocusManager* focus = GetFocusManager();
  return place && focus && place->Contains(focus->GetFocusedView());
}

void RootView::UpdateScreenReaderLayers() {
  UpdateAccessibleLayers(*this, [this](const views::View& child) {
    return &child == lock_.get() || &child == quit_dialog_.get() ||
           &child == app_icon_dialog_.get() || &child == profile_sheet_.get() ||
           &child == extension_prompt_.get() || &child == page_dialog_.get() ||
           &child == alert_dialog_.get() || &child == about_dialog_.get();
  });
}

ExtensionsMenuView* RootView::extensions_menu() {
  return extensions_card_->GetVisible() ? extensions_menu_.get() : nullptr;
}

void RootView::ShowExtensionPrompt(const ExtensionPrompt& prompt,
                                   const gfx::ImageSkia& app_icon) {
  HideExtensionPrompt();
  // Over the menus and a page's dialog, under the switcher and what shows
  // over the whole window after it.
  extension_prompt_ = AddChildViewAt(
      std::make_unique<ExtensionPromptOverlay>(
          prompt, app_icon,
          base::BindRepeating(&ExtensionsUi::AnswerPrompt,
                              base::Unretained(callbacks_.extensions.get()))),
      GetIndexOf(switcher_).value());
  PlaceExtensionPrompt();
  UpdateScreenReaderLayers();
  // It takes the keys; what had them gets them back once it is answered.
  views::FocusManager* focus = GetFocusManager();
  views::View* focused = focus ? focus->GetFocusedView() : nullptr;
  focus_before_prompt_.SetView(focused && Contains(focused) ? focused
                                                            : nullptr);
  extension_prompt_->RequestFocus();
}

void RootView::HideExtensionPrompt() {
  if (!extension_prompt_) {
    return;
  }
  ExtensionPromptOverlay* overlay = std::exchange(extension_prompt_, nullptr);
  // Gone at once, as the Swift's went; it leaves the window once the press
  // that answered it is through.
  overlay->SetVisible(false);
  overlay->SetCanProcessEventsWithinSubtree(false);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&RootView::ExtensionPromptLeft,
                                weak_factory_.GetWeakPtr(), overlay));
  views::View* before = focus_before_prompt_.view();
  focus_before_prompt_.SetView(nullptr);
  if ((!before || !before->IsDrawn() || !before->IsFocusable()) &&
      page_dialog() && !lock_->takes_every_key()) {
    page_dialog()->TakeFocus();
  } else {
    ReturnKeys(before);
  }
}

void RootView::ExtensionPromptLeft(ExtensionPromptOverlay* overlay) {
  for (views::View* child : children()) {
    if (child == overlay) {
      RemoveChildViewT(overlay);
      return;
    }
  }
}

ExtensionPromptView* RootView::extension_prompt() {
  return extension_prompt_ ? extension_prompt_->prompt() : nullptr;
}

gfx::Rect RootView::ExtensionPromptBounds() const {
  return extension_prompt_ ? extension_prompt_->CardBounds() +
                                 extension_prompt_->bounds().OffsetFromOrigin()
                           : gfx::Rect();
}

// The removal's popup hangs from its extension's pinned button, else the
// puzzle, while the toolbar shows; else it, as adding does, sits in the
// window's middle (the Swift's extensionsOverlays).
void RootView::PlaceExtensionPrompt() {
  if (!extension_prompt_) {
    return;
  }
  extension_prompt_->SetBoundsRect(GetLocalBounds());
  std::optional<gfx::Rect> anchor;
  if (const ExtensionPrompt* prompt = callbacks_.extensions->prompt();
      prompt && prompt->kind == ExtensionPrompt::Kind::kRemove &&
      toolbar_->GetVisible()) {
    ExtensionActionsView* actions = toolbar_->extension_actions();
    const views::View* button = actions->ActionButtonFor(prompt->extension_id);
    if (!button || !button->IsDrawn()) {
      button = actions->puzzle();
    }
    if (button && button->IsDrawn()) {
      anchor = views::View::ConvertRectToTarget(button, this,
                                                button->GetLocalBounds());
    }
  }
  extension_prompt_->SetAnchor(anchor);
}

ExtensionMoreMenuView* RootView::extension_more_menu() {
  return more_card_->GetVisible() ? more_menu_.get() : nullptr;
}

// The menu's trailing edge on the puzzle's, 4 under it, kept 8 from the
// window's leading edge; the more menu beside it, over its edge by 10, level
// with its extension's row (the Swift's extensionsOverlays).
void RootView::PlaceExtensionsMenu() {
  extensions_shield_->SetBoundsRect(GetLocalBounds());
  if (!extensions_card_->GetVisible()) {
    return;
  }
  const views::View* puzzle = toolbar_->extension_actions()->puzzle();
  const gfx::Rect anchor =
      views::View::ConvertRectToTarget(puzzle, this, puzzle->GetLocalBounds());
  const gfx::SizeF size = extensions_card_->SizeFor(ExtensionsMenuView::kWidth);
  const float menu_x =
      std::max<float>(kMenuMargin, anchor.right() - size.width());
  const float menu_y = anchor.bottom() + kMenuGap;
  extensions_card_->Place(
      gfx::RectF(menu_x, menu_y, size.width(), size.height()));
  if (!more_card_->GetVisible()) {
    return;
  }
  const std::optional<gfx::Rect> row =
      extensions_menu_->RowBounds(more_menu_->extension_id());
  const gfx::SizeF more = more_card_->SizeFor(ExtensionMoreMenuView::kWidth);
  const float more_x =
      std::max<float>(kMenuMargin, menu_x - ExtensionMoreMenuView::kWidth +
                                       ExtensionMoreMenuView::kOverlap);
  const float more_y = menu_y + (row ? row->y() : 0);
  more_card_->Place(gfx::RectF(more_x, more_y, more.width(), more.height()));
}

void RootView::ShowExclusiveAccessHint(const std::u16string& hint) {
  hint_->SetHint(hint);
  if (!hint.empty()) {
    // Over whatever came after it.
    ReorderChildView(hint_, children().size());
    UpdateScreenReaderLayers();
    InvalidateLayout();
  }
}

std::u16string RootView::exclusive_access_hint() const {
  return hint_->hint();
}

SkPath RootView::PanelPath() const {
  return SquirclePath(gfx::RectF(panel_bounds_),
                      page_fullscreen_ ? 0 : radius::kPanel);
}

void RootView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // The page's dim in the modal scrim's color, black in both, darker on dark.
  if (page_dim_) {
    page_dim_->layer()->AsSolidColor()->SetColor(SkColor4f::FromColor(
        GetColorProvider()->GetColor(kPaneColorModalScrim)));
  }
  // Every view in the theme's colors, those on layers of their own too; a
  // contrast theme's colors come in more than one change, its switch before
  // its colors, and each one paints again.
  PaintAgain(*this);
}

bool RootView::IsDragArea(const gfx::Point& point) {
  // The PIN screen's panel moves the window around its controls, and so do
  // the gutters around it, as the Swift's WindowDragArea under it did.
  if (lock_->showing()) {
    gfx::Point in_lock = point;
    views::View::ConvertPointToTarget(this, lock_, &in_lock);
    return lock_->IsDragArea(in_lock);
  }
  if (page_fullscreen_) {
    return false;
  }
  views::View* hit = GetEventHandlerForPoint(point);
  // The header's background and its title; the window's own around the
  // panel, which takes its presses as the panel's card did.
  return hit == sidebar_->header() ||
         (hit == this && !PanelPath().contains(point.x(), point.y()));
}

gfx::Size RootView::GetMinimumSize() const {
  return kMinimumSize;
}

void RootView::Layout(PassKey) {
  const int sidebar_width = static_cast<int>(std::round(shows_.sidebar_width));

  sidebar_->SetBounds(0, 0, sidebar_width, height());
  resize_->SetBounds(sidebar_width - ResizeHandle::kOverSidebar, 0,
                     ResizeHandle::kWidth, height());

  panel_bounds_ =
      page_fullscreen_
          ? GetLocalBounds()
          : gfx::Rect(sidebar_width, kPanelInset,
                      std::max(width() - sidebar_width - kPanelInset, 0),
                      std::max(height() - 2 * kPanelInset, 0));
  frame_->SetBoundsRect(GetLocalBounds());
  frame_->SetPanel(panel_bounds_, page_fullscreen_);

  toolbar_->SetBounds(panel_bounds_.x(), panel_bounds_.y(),
                      panel_bounds_.width(), ToolbarView::kHeight);

  const int top = toolbar_->GetVisible() ? ToolbarView::kHeight : 0;
  // The page's place, which docked DevTools share (ADR 0024).
  const gfx::Rect place(panel_bounds_.x(), panel_bounds_.y() + top,
                        panel_bounds_.width(),
                        std::max(panel_bounds_.height() - top, 0));
  const DockedDevTools docked = DockedDevToolsFor(page_->web_contents(), place);
  devtools_->SetBoundsRect(docked.devtools ? docked.devtools_bounds : place);
  page_->SetBoundsRect(docked.page_bounds);
  RoundPage();

  empty_->SetBoundsRect(panel_bounds_);
  settings_->SetBoundsRect(panel_bounds_);
  // Where the page is, under the toolbar.
  sad_tab_->SetBoundsRect(page_->bounds());
  page_shield_->SetBoundsRect(page_->bounds());
  if (page_dim_) {
    page_dim_->SetBoundsRect(page_->bounds());
    // The page's own corners, as its layer rounds them (RoundPage).
    if (const gfx::RoundedCornersF corners = PageCorners();
        page_dim_->layer()->rounded_corner_radii() != corners) {
      page_dim_->layer()->SetRoundedCornerRadius(corners);
    }
  }
  switcher_->SetBoundsRect(GetLocalBounds());
  PlacePageDialogs();
  // The find bar 64 down the window and 24 in from its right.
  find_shield_->SetBoundsRect(GetLocalBounds());
  if (find_card_->GetVisible() &&
      find_card_->Place(gfx::RectF(
          width() - FindBarView::kRightInset - FindBarView::kWidth,
          FindBarView::kTop, FindBarView::kWidth, FindBarView::kHeight))) {
    find_card_->Settle(true);
  }
  PlaceExtensionsMenu();
  PlaceExtensionPrompt();
  // The hint 24 down, in the window's middle.
  const gfx::Size hint = hint_->GetPreferredSize();
  hint_->SetBounds((width() - hint.width()) / 2, ExclusiveAccessHint::kTop,
                   hint.width(), hint.height());

  if (profile_sheet_) {
    profile_sheet_->SetBoundsRect(GetLocalBounds());
  }
  if (app_icon_dialog_) {
    app_icon_dialog_->SetBoundsRect(GetLocalBounds());
  }
  quit_dialog_->SetBoundsRect(GetLocalBounds());
  for (views::View* child : children()) {
    if (views::IsViewClass<ModalDialog>(child) && child != quit_dialog_ &&
        child != app_icon_dialog_) {
      child->SetBoundsRect(GetLocalBounds());
    }
  }
  lock_->SetBoundsRect(GetLocalBounds());
  LayOutWindowControls();
}

void RootView::LayOutWindowControls() {
  if (!window_controls_) {
    return;
  }
  // None while a page or the window fills the screen, as Windows' own
  // caption goes then.
  views::Widget* widget = GetWidget();
  const bool shown = !page_fullscreen_ && !(widget && widget->IsFullscreen());
  window_controls_->SetVisible(shown);
  if (!shown) {
    return;
  }
  // The panel's top-right corner, which the PIN screen's panel shares; in a
  // maximized window the frame's gutter above and beside Close answers as
  // Close, so the controls reach the window's top and right edges there.
  const gfx::Rect panel(panel_bounds_.x(), kPanelInset, panel_bounds_.width(),
                        std::max(height() - 2 * kPanelInset, 0));
  const bool maximized = widget && widget->IsMaximized();
  const int row = window_controls_->RowWidth();
  gfx::Rect bounds(panel.right() - row, panel.y(), row,
                   window_controls_->RowHeight());
  if (maximized) {
    bounds = gfx::Rect(panel.right() - row, 0, width() - (panel.right() - row),
                       panel.y() + window_controls_->RowHeight());
  }
  window_controls_->SetBoundsRect(bounds);
  gfx::Rect panel_in_controls = panel;
  panel_in_controls.Offset(-bounds.x(), -bounds.y());
  window_controls_->SetPanel(panel_in_controls);
}

BEGIN_METADATA(RootView)
END_METADATA

}  // namespace pane
