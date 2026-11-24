// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_DIALOG_H_
#define PANE_UI_DESIGN_DIALOG_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/design/icons.h"
#include "pane/ui/design/motion.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/focus/focus_search.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"

namespace views {
class Label;
}

namespace pane {

// A dialog's title, and what it says under it (the Swift's DialogHeading,
// Paper 03.5 Overlays): the title at 18 semibold on a 24-point line, 6 over
// the message at 14 on 20-point lines, wrapping, in `message_color`: the
// foreground at 70%, or Quit Pane?'s a step stronger
// (kPaneColorDialogMessage). The title's −0.18 tracking goes without: Views
// has none (Paper 10.6).
class DialogHeading : public views::View {
  METADATA_HEADER(DialogHeading, views::View)

 public:
  DialogHeading(const std::u16string& title,
                const std::u16string& message,
                ui::ColorVariant message_color = kPaneColorHeadingMessage);
  DialogHeading(const DialogHeading&) = delete;
  DialogHeading& operator=(const DialogHeading&) = delete;
  ~DialogHeading() override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  int HeightForWidth(int width) const;

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> message_ = nullptr;
};

// A dialog's card on its shadow (Quit Pane?'s, the App icon picker's, a
// page's dialog): its content on a fill, an edge, and a shadow, painted in
// one layer sized to how far the shadow reaches, so only that much rasters,
// once, as it comes (ADR 0123). It comes growing from 0.97 about its middle
// as it fades in, and goes the same way (Motion.panel), on the compositor's
// own animation, so the card and its shadow raster once, at their end's
// scale. It is placed to the fraction of a point, as Paper's layout places
// it: its layer carries what whole points leave. A press on the card stays with
// it; one on its shadow goes to what lies under it.
class DialogCard : public views::View, public views::ViewTargeterDelegate {
  METADATA_HEADER(DialogCard, views::View)

 public:
  struct Look {
    Look();
    Look(const Look&);
    Look& operator=(const Look&);
    ~Look();

    // The fill, rounded on the scale: a dialog's 29.
    float radius = radius::k2xl;
    ui::ColorVariant fill;
    // The edge, `edge_width` wide: a ring outside the fill, its corner the
    // fill's grown by as much (a card's), or a line just inside the fill's
    // own edge, as Paper's borders are (an alert's hairline).
    ui::ColorVariant edge;
    float edge_width = 1;
    bool edge_outside = true;
    // The content's inset from the fill's edge.
    gfx::Insets padding;
    // The shadow's color, how far down it falls, and its Gaussian's
    // deviation (half CSS's blur, as Paper 02.5 gives it). A translucent
    // fill casts it as much lighter.
    ui::ColorVariant shadow;
    gfx::Vector2dF shadow_offset;
    float shadow_sigma = 0;
  };

  DialogCard(const Look& look, std::unique_ptr<views::View> content);
  DialogCard(const DialogCard&) = delete;
  DialogCard& operator=(const DialogCard&) = delete;
  ~DialogCard() override;

  views::View* content() { return content_; }
  // The card's size, its edge included, for a fill `width` wide: as tall as
  // its content is at the width its padding leaves.
  gfx::SizeF SizeFor(int width) const;
  // Places the card, its edge included, at `place` in its parent, to the
  // fraction of a point; whether it moved.
  bool Place(const gfx::RectF& place);
  // The card, its edge included, in its parent.
  gfx::Rect CardBounds() const;
  // At rest, shown or gone: its opacity, and the card's size.
  void Settle(bool shown);
  // On its way there from where it stands; `done` once it arrives, unless
  // another move takes over.
  void Move(bool shown, base::OnceClosure done = base::OnceClosure());

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

 private:
  class Fill;

  // How far past the card, its edge included, the shadow reaches: three of
  // its Gaussian's deviations, moved down by its offset.
  gfx::Insets Reach() const;
  // Where the layer stands shown, or gone: the card's place past whole
  // points, grown to its size or 0.97 of it about its middle.
  gfx::Transform TransformOf(bool shown) const;

  const Look look_;
  raw_ptr<Fill> fill_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  // The card's place past its whole points, on the screen's pixels.
  gfx::Vector2dF shift_;
};

// Keeps the keys' traversal inside `root`, a dialog over the window: Tab
// and Shift+Tab go round its controls, the last on to the first, as a
// dialog's own window keeps them, rather than on to the window's controls
// under it (Views' pane traversal, as Chrome's AccessiblePaneView keeps a
// toolbar's). Its view answers it as its pane's (GetPaneFocusTraversable).
class FocusTrap : public views::FocusTraversable {
 public:
  explicit FocusTrap(views::View* root);
  FocusTrap(const FocusTrap&) = delete;
  FocusTrap& operator=(const FocusTrap&) = delete;
  ~FocusTrap() override;

  // views::FocusTraversable:
  views::FocusSearch* GetFocusSearch() override;
  views::FocusTraversable* GetFocusTraversableParent() override;
  views::View* GetFocusTraversableParentView() override;

 private:
  views::FocusSearch search_;
};

// A dialog's icon tile (Quit Pane?'s, Windows' system alert's; Paper 04.5,
// 11.13 A): 40 points, rounded 18, the icon's 22 in its middle, stroked
// 1.75 in the foreground; under a contrast theme, its edge where its fill
// told it apart (11.10). Assistive technologies skip it: it only decorates.
class DialogTile : public views::View {
  METADATA_HEADER(DialogTile, views::View)

 public:
  static constexpr int kSize = 40;

  explicit DialogTile(Icon icon);
  DialogTile(const DialogTile&) = delete;
  DialogTile& operator=(const DialogTile&) = delete;
  ~DialogTile() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  const Icon icon_;
};

// A dialog over the window, as Quit Pane? and the App icon picker are drawn
// (Paper 03.5, 04.5, 06.2): the modal scrim over the whole window, black at
// 14%, 45% on dark, and the dialog's card in its middle (DialogCard), `width`
// points wide, its content 24 in, rounded 29, on a 1-point ring outside it
// and its shadow (0 24px 64px, black 16%, 50% on dark). The scrim and the
// card fade apart, each on a layer of its own (ADR 0123). A click on the
// scrim and Esc dismiss it; Return accepts it.
// Which other keys it takes is its `keys`.
class ModalDialog : public views::View {
  METADATA_HEADER(ModalDialog, views::View)

 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    base::RepeatingClosure dismiss;
    base::RepeatingClosure accept;
  };

  // Esc and Return alone, the window's other keys going on, and its content
  // having the keys' focus to move in (the App icon picker's, Paper 06.2); or
  // every key, none reaching what is under it or the
  // window's commands (Quit Pane?'s, as the PIN screen's): only the menu
  // bar's Quit goes on, which the window lets through (TakesEveryKey); or a
  // dialog's own keys (Windows' system alert's and About Pane's, Paper 11.13
  // A, 11.15 A): every key as Quit Pane?'s, but Tab, which moves round its
  // controls (MoveFocus), and Space and Enter, which press the one with the
  // ring where the system's buttons take them.
  enum class Keys { kEscapeAndReturn, kEvery, kDialog };

  ModalDialog(int width,
              std::unique_ptr<views::View> content,
              Callbacks callbacks,
              Keys keys = Keys::kEscapeAndReturn);
  ModalDialog(const ModalDialog&) = delete;
  ModalDialog& operator=(const ModalDialog&) = delete;
  ~ModalDialog() override;

  // Its card's look: its content 24 in, rounded 29, on a 1-point ring
  // outside it and its shadow, 0 24px 64px (a page's dialog on Windows takes
  // it too, Paper 04.15.1).
  static DialogCard::Look CardLook();

  views::View* content() { return card_->content(); }
  // The card, ring included, in this view.
  gfx::Rect CardBounds() const { return card_->CardBounds(); }
  // Shows, fading in as the card grows to its size, and takes the keys. It
  // is made hidden, ahead of its first showing, and kept.
  void AnimateIn();
  // Fades out as the card shrinks, taking no events on the way, then hides;
  // `gone` once it has, unless it comes in again meanwhile, or at once if it
  // wasn't showing.
  void AnimateOut(base::OnceClosure gone = base::OnceClosure());
  // Shown and not on its way out.
  bool showing() const;
  // Shown, and taking every key.
  bool takes_every_key() const {
    return (keys_ == Keys::kEvery || keys_ == Keys::kDialog) && showing();
  }
  // Shown, and moving the keys round its own controls by Tab.
  bool moves_focus_within() const {
    return keys_ == Keys::kDialog && showing();
  }
  // The next of its content's controls, or the one before, round them; the
  // first, or the last, while none of them has the keys.
  void MoveFocus(bool reverse);

  // views::View: with its own keys, Tab from a control of its goes round
  // them, as a dialog's window keeps it (Views' pane traversal), rather than
  // on to the window's controls under it.
  views::FocusTraversable* GetPaneFocusTraversable() override;

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;

 private:
  // Lays out the scrim and the card, in this view's middle as Paper centers
  // it; whether the card moved.
  bool Place();
  // The dialog at rest where it is heading.
  void Settle();
  // Heads in, or out, from where it stands.
  void Move(bool shown);
  // It has arrived; gone, it hides.
  void Moved();

  const int width_;
  Callbacks callbacks_;
  const Keys keys_;
  raw_ptr<views::View> scrim_ = nullptr;
  raw_ptr<DialogCard> card_ = nullptr;
  bool closing_ = false;
  // Where it is heading, and whether it is on its way.
  bool shown_ = false;
  bool moving_ = false;
  // What waits for it to have gone.
  base::OnceClosure gone_;
  // Its content's controls, which Tab goes round with its own keys.
  std::unique_ptr<FocusTrap> focus_trap_;
  base::WeakPtrFactory<ModalDialog> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_DIALOG_H_
