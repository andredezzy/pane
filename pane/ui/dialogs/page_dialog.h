// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_PAGE_DIALOG_H_
#define PANE_UI_DIALOGS_PAGE_DIALOG_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/design/text.h"
#include "ui/color/color_variant.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace views {
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace pane {

class FadingScrollView;

// What a page's dialog asks (the Swift's PageDialog): its title, its
// message's lines, its fields, and its buttons, the default one first, as an
// NSAlert orders them, and the one Esc answers as.
struct PageDialogContent {
  struct Field {
    std::u16string placeholder;
    bool secure = false;
    std::u16string text;
  };

  PageDialogContent();
  PageDialogContent(const PageDialogContent&);
  PageDialogContent& operator=(const PageDialogContent&);
  PageDialogContent(PageDialogContent&&);
  PageDialogContent& operator=(PageDialogContent&&);
  ~PageDialogContent();

  std::u16string title;
  std::vector<std::u16string> message;
  std::vector<Field> fields;
  std::vector<std::u16string> buttons;
  // The button Esc answers as, its key shown on it where the system's
  // dialogs show keys (Windows, Paper 11.18): Cancel's. None where Esc gives
  // no answer of a button's (a permission's, which the site may ask again)
  // or the default's (an alert's OK, an unresponsive page's Wait).
  std::optional<size_t> escape_button;
};

// How a page's dialog looks on each system (page_dialog_mac.cc,
// page_dialog_win.cc): the Mac's alert sheet, as macOS draws the one it
// replaced (Paper 04.15 to 04.23); Windows', the card of Pane's own
// questions over the page's dim, its buttons keyed (04.15.1 to 04.23.1,
// 04.29.1, 11.18, 11.18.1).
struct PageDialogStyle {
  PageDialogStyle();
  PageDialogStyle(const PageDialogStyle&);
  PageDialogStyle& operator=(const PageDialogStyle&);
  ~PageDialogStyle();

  // The card's fill's width, and its look.
  int width = 0;
  DialogCard::Look card;
  // The title, and the message's lines, in their colors.
  TextStyle title;
  ui::ColorVariant title_color;
  TextStyle message;
  ui::ColorVariant message_color;
  // From the title to the message; between the message's lines; between the
  // heading, the fields, and the buttons; the buttons' row further down;
  // between two buttons, and two fields; a field's height.
  int title_spacing = 0;
  int line_spacing = 0;
  int spacing = 0;
  int buttons_top = 0;
  int button_spacing = 0;
  int field_spacing = 0;
  int field_height = 0;
  // Two buttons share the row, each half of it, the default on the right
  // (the Mac's alert); else each stands at its own width, at the row's end.
  // Three stand stacked either way, the default on top.
  bool buttons_share_row = true;
  // A message too long to show whole scrolls, fading at the edge it runs
  // past this far, or ending hard there (0: the Mac's alert).
  int message_fade = 0;
  // The page under it dims as it comes, and it stands centred on the page
  // rather than on the window.
  bool dims_page = false;
  bool centred_on_page = false;
};

// This system's.
const PageDialogStyle& ThisPageDialogStyle();

// This system's button for a page's dialog: the default (Return's), the one
// Esc answers as, or another.
enum class PageDialogButtonRole { kDefault, kEscape, kOther };
std::unique_ptr<views::Button> MakePageDialogButton(
    views::Button::PressedCallback pressed,
    const std::u16string& title,
    PageDialogButtonRole role);

// This system's field for a page's dialog, which reads by its placeholder
// (ADR 0071), as `field` asks; Tab and Shift-Tab in it go to `tab`, round
// the dialog.
std::unique_ptr<views::Textfield> MakePageDialogField(
    const PageDialogContent::Field& field,
    base::RepeatingCallback<void(bool reverse)> tab);

// A page's dialog (Paper 04.15 to 04.23 and 04.29; the Swift's DialogView),
// in this system's style (PageDialogStyle): on the Mac as macOS draws the
// alert sheet it replaced, the title in bold and the message at 13, the
// fields rounded 7, then 28-point capsule buttons, the default one blue; on
// Windows on the card of Pane's own questions, the title at 18 and the
// message at 14, Pane's fields, then keyed buttons, the default filled with
// Enter. Two buttons stand side by side, the default on the right; three
// stacked, the default on top. A message too long for the window scrolls,
// 320 points of it at most showing. Return presses the default and Esc
// answers as Chrome's dialogs take it; the first field takes the keyboard as
// it shows, its text selected, or the dialog does, so no key reaches the page
// under it, and Tab goes round its fields and buttons (the Mac's with Full
// Keyboard Access; RootView hands it Tab ahead of the window's traversal).
// VoiceOver reads it as a modal dialog by its title, its fields by their
// placeholders (ADR 0071).
class PageDialog : public views::View {
  METADATA_HEADER(PageDialog, views::View)

 public:
  // A button's index, or nullopt for Esc, Chrome's dismissal.
  using Answer = base::RepeatingCallback<void(std::optional<size_t> button)>;

  PageDialog(const PageDialogContent& content, Answer answer);
  PageDialog(const PageDialog&) = delete;
  PageDialog& operator=(const PageDialog&) = delete;
  ~PageDialog() override;

  const PageDialogContent& content() const { return content_; }
  // What the fields hold now: what was typed, or set through accessibility
  // (VoiceOver, Voice Control), as the alert's own fields counted it.
  std::vector<std::u16string> FieldTexts() const;
  // Sets the fields' text, in order, as a value set through accessibility
  // does: the channel's, for a scenario.
  void SetFieldTexts(const std::vector<std::u16string>& texts);
  views::Textfield* field(size_t index);
  size_t field_count() const { return fields_.size(); }
  views::Button* button(size_t index) { return buttons_[index]; }
  size_t button_count() const { return buttons_.size(); }
  // Its views by the names the channel reports them by: "pane.dialog" (the
  // card's content), "pane.dialog.field.<n>", "pane.dialog.button.<n>".
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // The first field takes the keyboard, its text selected, or the dialog
  // does.
  void TakeFocus();
  // Esc dismisses it, as Chrome's dialogs' Cancel, Wait, and a permission's
  // dismissal; Return presses the default, the first. The window hands it
  // the two ahead of what has its keys, a field or the address included (ADR
  // 0079, RootView).
  void PressKey(bool escape);
  // It answers no more: its presses and keys do nothing.
  void Retire();
  // How tall it may stand, its edges' padding included: past it, its
  // message scrolls, at most 320 points of it showing.
  void SetMostHeight(int height);
  // Tab and Shift-Tab: the next field, or button, round the dialog.
  void MoveFocus(bool reverse);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  // Tab from a field or a button of its goes round them, not on to the
  // window's (Views' pane traversal; FocusTrap).
  views::FocusTraversable* GetPaneFocusTraversable() override;

 private:
  void Press(std::optional<size_t> button);
  int HeightForWidth(int width) const;
  int MessageHeight(int width) const;
  int FieldsHeight() const;
  int ButtonsHeight() const;

  const PageDialogContent content_;
  const PageDialogStyle style_;
  const Answer answer_;
  // How tall the content may stand, and the message's height while it
  // scrolls in it.
  int most_height_ = 0;
  std::optional<int> scrolled_message_height_;
  bool retired_ = false;
  raw_ptr<views::Label> title_ = nullptr;
  // What the message scrolls in, while it does: a scroll view, or one that
  // fades at its edges, around it.
  raw_ptr<views::View> message_scroller_ = nullptr;
  raw_ptr<views::View> message_ = nullptr;
  std::vector<raw_ptr<views::Label>> message_lines_;
  std::vector<raw_ptr<views::Textfield>> fields_;
  std::vector<raw_ptr<views::Button>> buttons_;
  std::unique_ptr<FocusTrap> focus_trap_;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_PAGE_DIALOG_H_
