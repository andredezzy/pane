// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_EXTENSION_PROMPT_VIEW_H_
#define PANE_UI_DIALOGS_EXTENSION_PROMPT_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/dialog.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class Checkbox;

// Adding or removing an extension, as its prompt asks (ADR 0056; the
// Swift's ExtensionPrompt): adding, in Dia's dialog, with Chrome's title and
// what the extension can do (Chrome's other prompts, new permissions and
// re-enabling, ask there too, under their own titles); removing, in Aside's
// popup, with Report abuse. It is answered once.
struct ExtensionPrompt {
  // By the Swift's names, which the channel reports as a sheet's kind.
  enum class Kind { kInstall, kRemove };
  // Accepted or not, and Report abuse ticked (removing only).
  using Answer = base::OnceCallback<void(bool accepted, bool report_abuse)>;

  ExtensionPrompt();
  ExtensionPrompt(ExtensionPrompt&&);
  ExtensionPrompt& operator=(ExtensionPrompt&&);
  ~ExtensionPrompt();

  Kind kind = Kind::kInstall;
  std::string extension_id;
  std::u16string title;
  // The extension's icon; empty, its place shows the secondary fill.
  gfx::Image icon;
  // What it can do, under its heading ("It can:"), for adding.
  std::u16string permissions_heading;
  std::vector<std::u16string> permissions;
  std::u16string accept_title;
  std::u16string cancel_title;
  Answer answer;
};

// "extensionInstall", "extensionRemove".
std::string_view ExtensionPromptKindName(ExtensionPrompt::Kind kind);

// A prompt's card, as the Swift drew each, its content on DialogCard:
// - Adding, Dia's dialog (Paper 03.10, 04.10; ExtensionInstallDialog): 424
//   wide on its own surface, the card's fill rounded 29 with the
//   foreground's 10% just inside its edge and Dia's shadow (0 24px 64px),
//   its content 24 inside that edge: Pane's icon, the arrow, and the
//   extension's, at 32; the title at 20 semibold on 26-point lines, 14
//   under them; what it can do at 14 on 20-point lines, on a panel of the
//   foreground's 4% rounded 15, 12 under the title; then Cancel (Esc) and
//   the accept (↩) on the keyed buttons, 24 under it.
// - Removing, Aside's popup (03.10, 04.11; ExtensionPromptView): 320 wide on
//   the popovers' surface, rounded 29, its content 20 inside the edge: the
//   icon at 32 beside the title at 15 medium on 20-point lines, 20 apart;
//   Report abuse, the system's checkbox, 24 under them; then Cancel and
//   Remove, Aside's popup buttons, 32 under it.
// Return accepts and Esc cancels, from whatever has the keys (RootView's
// OverlayKey); VoiceOver reads a modal dialog by its title.
class ExtensionPromptView : public views::View {
  METADATA_HEADER(ExtensionPromptView, views::View)

 public:
  // Each card's width, its edge included.
  static constexpr int kInstallWidth = 424;
  static constexpr int kRemoveWidth = 320;

  // `pressed` hears a button's press: accept or not. `app_icon` is Pane's,
  // as the Dock shows it, for adding.
  ExtensionPromptView(const ExtensionPrompt& prompt,
                      const gfx::ImageSkia& app_icon,
                      base::RepeatingCallback<void(bool accepted)> pressed);
  ExtensionPromptView(const ExtensionPromptView&) = delete;
  ExtensionPromptView& operator=(const ExtensionPromptView&) = delete;
  ~ExtensionPromptView() override;

  static DialogCard::Look CardLook(ExtensionPrompt::Kind kind);
  static int WidthOf(ExtensionPrompt::Kind kind);

  ExtensionPrompt::Kind kind() const { return kind_; }
  // Report abuse, ticked (removing only).
  bool report_abuse() const;
  // Pane's icon, as the Dock shows it now, once the Theme changed it
  // (adding only).
  void SetAppIcon(const gfx::ImageSkia& app_icon);
  views::View* accept_button() { return accept_; }
  views::View* cancel_button() { return cancel_; }
  // Its parts, by the names the Swift's accessibility identifiers gave
  // them ("pane.extensionPopup.accept"), for the channel.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  class Icons;
  class Permissions;

  // The content's height at `width`, and its parts laid out there when
  // `place`.
  int Arrange(int width, bool place) const;
  int ArrangeInstall(int width, bool place) const;
  int ArrangeRemove(int width, bool place) const;
  // Both prompts' buttons from `y`, the accepting one at the trailing edge
  // and Cancel before it: the bottom they reach.
  int ArrangeButtons(int width, int y, bool place) const;

  const ExtensionPrompt::Kind kind_;
  raw_ptr<Icons> icons_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<Permissions> permissions_ = nullptr;
  raw_ptr<Checkbox> report_abuse_ = nullptr;
  raw_ptr<views::View> cancel_ = nullptr;
  raw_ptr<views::View> accept_ = nullptr;
};

// A prompt over the window, as the Swift's extensionsOverlays showed one,
// at once, with no transition: adding on the modal scrim, its card in the
// window's middle; removing on nothing, its card's trailing edge on its
// anchor's (the extension's pinned button, else the puzzle), 4 under it and
// kept 8 from the window's leading edge, or in the window's middle where no
// toolbar shows. A press outside the card cancels it. It holds the keys
// while it shows, as the Swift's popup took every key but a shortcut: Esc
// and Return come to it ahead of whatever had them (RootView's OverlayKey),
// a shortcut still runs, and the rest goes nowhere.
class ExtensionPromptOverlay : public views::View {
  METADATA_HEADER(ExtensionPromptOverlay, views::View)

 public:
  // `answer` hears the answer: a button, or a press outside (no).
  ExtensionPromptOverlay(const ExtensionPrompt& prompt,
                         const gfx::ImageSkia& app_icon,
                         base::RepeatingCallback<void(bool accepted)> answer);
  ExtensionPromptOverlay(const ExtensionPromptOverlay&) = delete;
  ExtensionPromptOverlay& operator=(const ExtensionPromptOverlay&) = delete;
  ~ExtensionPromptOverlay() override;

  ExtensionPromptView* prompt() { return prompt_; }
  // What the popup hangs from, in this view; none centers it.
  void SetAnchor(const std::optional<gfx::Rect>& anchor);
  // The card, its edge included, in this view.
  gfx::Rect CardBounds() const;

  // views::View:
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

 private:
  base::RepeatingCallback<void(bool)> answer_;
  std::optional<gfx::Rect> anchor_;
  // The scrim, a color the compositor draws (adding only).
  raw_ptr<views::View> scrim_ = nullptr;
  raw_ptr<DialogCard> card_ = nullptr;
  raw_ptr<ExtensionPromptView> prompt_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_EXTENSION_PROMPT_VIEW_H_
