// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/action_popup.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/extension_view.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "components/javascript_dialogs/app_modal_dialog_queue.h"
#include "components/web_modal/web_modal_utils.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/interaction_log.h"
#include "pane/ui/view_ids.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace pane {

namespace {

// The popover's corner, as macOS rounds an NSPopover's and Paper's 04.12
// draws it: circular, 14 (ADR 0057).
constexpr float kRadius = 14;
// The material's frame around the page, and the page's own corners in it.
constexpr int kFrame = 6;
constexpr float kPageRadius = 9;
// The arrow: Paper's square of 14 turned 45°, half of it out of the top
// edge, its tip on the button's bottom edge (10 tall and 20 across).
constexpr int kArrow = 10;
// Kept from the screen's edges, as AppKit keeps a popover.
constexpr int kScreenMargin = 8;

// The bubble's outline, its arrow `arrow_x` along its top edge.
SkPath Outline(const gfx::RectF& body, float arrow_x) {
  const float left = body.x();
  const float top = body.y();
  const float right = body.right();
  const float bottom = body.bottom();
  SkPathBuilder path;
  path.moveTo(left + kRadius, top);
  path.lineTo(arrow_x - kArrow, top);
  path.lineTo(arrow_x, top - kArrow);
  path.lineTo(arrow_x + kArrow, top);
  path.lineTo(right - kRadius, top);
  path.arcTo({right, top}, {right, top + kRadius}, kRadius);
  path.lineTo(right, bottom - kRadius);
  path.arcTo({right, bottom}, {right - kRadius, bottom}, kRadius);
  path.lineTo(left + kRadius, bottom);
  path.arcTo({left, bottom}, {left, bottom - kRadius}, kRadius);
  path.lineTo(left, top + kRadius);
  path.arcTo({left, top}, {left + kRadius, top}, kRadius);
  path.close();
  return path.detach();
}

}  // namespace

ActionPopup::Callbacks::Callbacks() = default;
ActionPopup::Callbacks::Callbacks(Callbacks&&) = default;
ActionPopup::Callbacks& ActionPopup::Callbacks::operator=(Callbacks&&) =
    default;
ActionPopup::Callbacks::~Callbacks() = default;

// The extension's page: Chrome's popup host shows in it, as in Chrome's
// ExtensionViewViews, sizing itself to its content within the popup's limits
// as each frame of its becomes the main one (views::WebView sets that up).
// Keys it leaves unhandled go to the window's commands, as a page's do.
class ActionPopup::Page : public views::WebView,
                          public extensions::ExtensionView {
  METADATA_HEADER(Page, views::WebView)

 public:
  Page(Profile* profile,
       extensions::ExtensionViewHost& host,
       ActionPopup& popup,
       const gfx::Size& max)
      : views::WebView(profile), host_(host), popup_(popup) {
    SetID(kViewIdActionPopupPage);
    host.set_view(this);
    // The page's renderer starts once it has a window to draw in, as
    // Chrome's ExtensionViewViews starts it.
    attached_ = AddWebContentsAttachedCallback(
        base::BindRepeating([](extensions::ExtensionViewHost* host,
                               views::WebView*) { host->CreateRendererSoon(); },
                            base::Unretained(&host)));
    SetWebContents(host.host_contents());
    EnableSizingFromWebContents(extensions::kExtensionPopupMinSize, max);
  }

  ~Page() override { host_->set_view(nullptr); }

  // extensions::ExtensionView:
  gfx::NativeView GetNativeView() override { return holder()->native_view(); }

  void ResizeDueToAutoResize(content::WebContents* web_contents,
                             const gfx::Size& new_size) override {
    PANE_TRACE("event", "ExtensionPopup.resize",
               base::DictValue()
                   .Set("width", new_size.width())
                   .Set("height", new_size.height()));
    views::WebView::ResizeDueToAutoResize(web_contents, new_size);
    popup_->PageResized();
  }

  void RenderFrameCreated(content::RenderFrameHost* frame) override {
    views::WebView::RenderFrameCreated(frame);
  }

  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    return GetWidget() && unhandled_keys_.HandleKeyboardEvent(
                              event, GetWidget()->GetFocusManager());
  }

  void OnLoaded() override { popup_->PageLoaded(); }

 private:
  const raw_ref<extensions::ExtensionViewHost> host_;
  const raw_ref<ActionPopup> popup_;
  base::CallbackListSubscription attached_;
  views::UnhandledKeyboardEventHandler unhandled_keys_;
};

BEGIN_METADATA(ActionPopup, Page)
END_METADATA

// The window's contents: the bubble painted as Paper 04.12 draws it, its
// arrow out of the top edge, and the page in its frame.
class ActionPopup::Bubble : public views::View {
  METADATA_HEADER(Bubble, views::View)

 public:
  explicit Bubble(std::unique_ptr<Page> page) {
    SetID(kViewIdActionPopup);
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    page_ = AddChildView(std::move(page));
  }

  // The window's size for a page of `page` points.
  static gfx::Size SizeFor(const gfx::Size& page) {
    return gfx::Size(page.width() + 2 * kFrame,
                     page.height() + 2 * kFrame + kArrow);
  }

  // Where the arrow's tip is, from the window's leading edge.
  void SetArrowX(float x) {
    if (x != arrow_x_) {
      arrow_x_ = x;
      SchedulePaint();
    }
  }

  gfx::Rect PageBounds() const {
    return gfx::Rect(kFrame, kArrow + kFrame, std::max(width() - 2 * kFrame, 0),
                     std::max(height() - kArrow - 2 * kFrame, 0));
  }

  // views::View:
  void Layout(PassKey) override { page_->SetBoundsRect(PageBounds()); }

  void AddedToWidget() override {
    // The page's own corners, as its layer rounds them (circles, as the
    // popover's own corner: no mask).
    page_->holder()->SetNativeViewCornerRadii(
        gfx::RoundedCornersF(kPageRadius));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    const gfx::RectF body(0, kArrow, width(), height() - kArrow);
    const SkPath outline = Outline(body, arrow_x_);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(colors->GetColor(kPaneColorActionPopup));
    canvas->DrawPath(outline, fill);
    // The page sits on white, as a page's canvas is in Chrome; a dark popup's
    // page then shows on it as it would in a tab.
    cc::PaintFlags canvas_fill;
    canvas_fill.setAntiAlias(true);
    canvas_fill.setColor(colors->GetColor(kPaneColorActionPopupCanvas));
    canvas->DrawRoundRect(gfx::RectF(PageBounds()), kPageRadius, canvas_fill);
    // The hairline, half a point just inside the edge: a point's line on the
    // edge, its outer half clipped away.
    canvas->Save();
    canvas->ClipPath(outline, /*do_anti_alias=*/true);
    cc::PaintFlags edge;
    edge.setAntiAlias(true);
    edge.setStyle(cc::PaintFlags::kStroke_Style);
    edge.setStrokeWidth(1);
    edge.setColor(colors->GetColor(kPaneColorActionPopupEdge));
    canvas->DrawPath(outline, edge);
    canvas->Restore();
  }

 private:
  raw_ptr<Page> page_ = nullptr;
  float arrow_x_ = 0;
};

BEGIN_METADATA(ActionPopup, Bubble)
END_METADATA

ActionPopup::ActionPopup(Browser& browser,
                         const extensions::Extension& extension,
                         const GURL& url,
                         views::Widget& parent,
                         const gfx::Rect& anchor,
                         bool activate,
                         std::optional<uint64_t> interval,
                         ShowPopupCallback shown,
                         Callbacks callbacks)
    : extension_id_(extension.id()),
      browser_(&browser),
      parent_(&parent),
      anchor_(anchor),
      activate_(activate),
      interval_(interval),
      shown_callback_(std::move(shown)),
      callbacks_(std::move(callbacks)),
      fade_(0,
            base::BindRepeating(
                [](ActionPopup* popup, double opacity) {
                  if (popup->widget_) {
                    popup->widget_->SetOpacity(static_cast<float>(opacity));
                  }
                },
                base::Unretained(this))) {
  PANE_TRACE("call", "ExtensionPopup.open",
             base::DictValue().Set("extension", extension.id()));
  // The room the screen leaves under the button, within Chrome's limits.
  const gfx::Rect work =
      display::Screen::Get()->GetDisplayMatching(anchor).work_area();
  max_page_ = extensions::kExtensionPopupMaxSize;
  max_page_.SetToMin(gfx::Size(
      work.width() - 2 * kScreenMargin - 2 * kFrame,
      work.bottom() - anchor.bottom() - kArrow - 2 * kFrame - kScreenMargin));
  max_page_.SetToMax(extensions::kExtensionPopupMinSize);

  host_ = extensions::ExtensionViewHostFactory::CreatePopupHost(extension, url,
                                                                &browser);
  host_->SetCloseHandler(
      base::BindOnce(&ActionPopup::HostClosed, weak_factory_.GetWeakPtr()));
  auto page =
      std::make_unique<Page>(browser.GetProfile(), *host_, *this, max_page_);
  page_ = page.get();
  auto bubble = std::make_unique<Bubble>(std::move(page));
  bubble_ = bubble.get();

  delegate_ = std::make_unique<views::WidgetDelegate>();
  delegate_->SetContentsView(std::move(bubble));
  delegate_->SetInitiallyFocusedView(page_);
  delegate_->SetAccessibleTitle(base::UTF8ToUTF16(extension.name()));
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_BUBBLE);
  params.delegate = delegate_.get();
  params.parent = parent.GetNativeView();
  params.name = kActionPopupWindowName;
  // Its own shape, and the window server's shadow of it, as a popover's.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.bounds =
      gfx::Rect(anchor.bottom_left(),
                Bubble::SizeFor(extensions::kExtensionPopupMinSize));
  widget_->Init(std::move(params));
  widget_observations_.AddObservation(widget_.get());
  widget_observations_.AddObservation(&parent);
  Place();
}

ActionPopup::~ActionPopup() {
  widget_observations_.RemoveAllObservations();
  if (shown_callback_) {
    std::move(shown_callback_).Run(nullptr);
  }
  // Its page first hears that it has no view, then the window goes, then
  // the page.
  page_ = nullptr;
  bubble_ = nullptr;
  widget_.reset();
  host_.reset();
}

gfx::Size ActionPopup::page_size() const {
  return page_ ? page_->GetPreferredSize() : gfx::Size();
}

void ActionPopup::FadeOut() {
  if (closing_) {
    return;
  }
  closing_ = true;
  PANE_TRACE("call", "ExtensionPopup.close",
             base::DictValue().Set("extension", extension_id_));
  // It takes no input on its way out.
  if (bubble_) {
    bubble_->SetCanProcessEventsWithinSubtree(false);
  }
  const auto gone = [](base::WeakPtr<ActionPopup> popup) {
    if (popup && popup->callbacks_.gone) {
      popup->callbacks_.gone.Run(popup.get());
    }
  };
  if (!shown_ || !widget_ || !widget_->IsVisible()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(gone, weak_factory_.GetWeakPtr()));
    return;
  }
  fade_.MoveTo(0, motion::Panel(), widget_->GetCompositor());
  // Under Reduce Motion, at once.
  if (!fade_.is_moving()) {
    widget_->Hide();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(gone, weak_factory_.GetWeakPtr()));
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(gone, weak_factory_.GetWeakPtr()),
      motion::Panel().total());
}

void ActionPopup::PageLoaded() {
  // Its Browser went first: it has nowhere to show.
  if (shown_ || closing_ || !widget_ || !host_) {
    return;
  }
  shown_ = true;
  Place();
  PANE_TRACE("callback", "ExtensionPopup.show",
             base::DictValue().Set("extension", extension_id_));
  // The window fades in from nothing, as AppKit's popover did.
  widget_->SetOpacity(0);
  if (activate_ && MayTakeFront()) {
    widget_->Show();
  } else {
    widget_->ShowInactive();
  }
  fade_.MoveTo(1, motion::Panel(), widget_->GetCompositor());
  if (!fade_.is_moving()) {
    widget_->SetOpacity(1);
  }
  if (interval_) {
    EndInteractionAtNextFrame(Interaction::kShowExtensionPopup, *interval_,
                              widget_->GetCompositor());
    interval_.reset();
  }
  if (shown_callback_) {
    std::move(shown_callback_).Run(host_.get());
  }
}

void ActionPopup::PageResized() {
  Place();
}

void ActionPopup::HostClosed(extensions::ExtensionHost* host) {
  ClosedItself(/*by_click=*/false);
}

void ActionPopup::ClosedItself(bool by_click) {
  PANE_TRACE("event", "ExtensionPopup.closedItself",
             base::DictValue().Set("byClick", by_click));
  // The host may be in the middle of its own close.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<ActionPopup> popup, bool by_click) {
            if (popup && !popup->closing_ && popup->callbacks_.closed) {
              popup->callbacks_.closed.Run(popup.get(), by_click);
            }
          },
          weak_factory_.GetWeakPtr(), by_click));
}

// Its middle under the anchor's, as NSPopover centers a popover on its
// positioning rect, kept on the screen with the arrow still at the anchor,
// away from the corners.
void ActionPopup::Place() {
  if (!widget_ || !page_) {
    return;
  }
  gfx::Size page = page_->GetPreferredSize();
  page.SetToMax(extensions::kExtensionPopupMinSize);
  page.SetToMin(max_page_);
  const gfx::Size size = Bubble::SizeFor(page);
  const gfx::Rect work =
      display::Screen::Get()->GetDisplayMatching(anchor_).work_area();
  const int tip = anchor_.CenterPoint().x();
  const int x =
      std::clamp(tip - size.width() / 2, work.x() + kScreenMargin,
                 std::max(work.x() + kScreenMargin,
                          work.right() - kScreenMargin - size.width()));
  const float arrow_x = std::clamp<float>(
      tip - x, kRadius + kArrow,
      std::max<float>(kRadius + kArrow, size.width() - kRadius - kArrow));
  bubble_->SetArrowX(arrow_x);
  widget_->SetBounds(gfx::Rect(gfx::Point(x, anchor_.bottom()), size));
}

bool ActionPopup::KeptOpen() const {
  content::WebContents* contents = host_ ? host_->host_contents() : nullptr;
  if (!contents) {
    return false;
  }
  return javascript_dialogs::AppModalDialogQueue::GetInstance()
             ->HasActiveDialog() ||
         web_modal::WebContentsHasActiveWebModal(contents) ||
         DevToolsWindow::GetInstanceForInspectedWebContents(contents);
}

// A click outside it: in Pane's window, which takes the keys from it, or in
// another app, which takes them from Pane (NSPopover's transient behavior).
void ActionPopup::OnWidgetActivationChanged(views::Widget* widget,
                                            bool active) {
  PANE_TRACE("event", "ExtensionPopup.activation",
             base::DictValue()
                 .Set("popup", widget == widget_.get())
                 .Set("active", active)
                 .Set("windowActive", parent_->IsActive()));
  if (!shown_ || closing_ || KeptOpen()) {
    return;
  }
  if ((widget == parent_ && active) ||
      (widget == widget_.get() && !active && !parent_->IsActive())) {
    ClosedItself(/*by_click=*/true);
  }
}

void ActionPopup::OnWidgetDestroying(views::Widget* widget) {
  widget_observations_.RemoveObservation(widget);
  if (widget == parent_) {
    ClosedItself(/*by_click=*/false);
  }
}

}  // namespace pane
