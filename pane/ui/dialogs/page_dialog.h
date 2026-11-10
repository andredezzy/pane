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
#include "ui/views/view.h"

namespace views {
class Button;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace pane {

// What a page's dialog asks (the Swift's PageDialog): its title, its
// message's lines, its fields, and its buttons, the default one first, as an
// NSAlert orders them.
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
};

// A page's dialog (Paper 04.15 to 04.23 and 04.29; the Swift's DialogView),
// as macOS draws the alert sheet it replaced, on the alert's card
// (AlertCardLook): the title in bold and the message at 13, on 16-point
// lines, the fields (rounded 7), then 28-point capsule buttons, the default
// one blue: two side by side, the default on the right; three stacked, the
// default on top. A message too long for the window scrolls. Return presses
// the default and Esc dismisses it, as Chrome's dialogs take them; the first
// field takes the keyboard as it shows, its text selected, or the dialog
// does, so no key reaches the page under it, and Tab goes round its fields
// (and its buttons, with Full Keyboard Access). VoiceOver reads it as a
// modal dialog by its title, its fields by their placeholders (ADR 0071).
class PageDialog : public views::View {
  METADATA_HEADER(PageDialog, views::View)

 public:
  // The fill's width, its edge inside it.
  static constexpr int kWidth = 260;

  // A button's index, or nullopt for Esc, Chrome's dismissal.
  using Answer = base::RepeatingCallback<void(std::optional<size_t> button)>;

  PageDialog(const PageDialogContent& content, Answer answer);
  PageDialog(const PageDialog&) = delete;
  PageDialog& operator=(const PageDialog&) = delete;
  ~PageDialog() override;

  // The alert's card: the panel at 95% with its hairline edge, rounded 29,
  // its content 22 in at the top and leading edge and 16 at the others, and
  // its shadow, black at 25%, 16 down, a Gaussian of 24.
  static DialogCard::Look AlertCardLook();

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
  // the two ahead of what has its keys, a field or the address included, as
  // the Swift's key monitor did (RootView).
  void PressKey(bool escape);
  // It answers no more: its presses and keys do nothing.
  void Retire();
  // How tall it may stand, its edges' padding included: past it, its
  // message scrolls, at most 320 points of it showing (the Swift's
  // ViewThatFits).
  void SetMostHeight(int height);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

 private:
  class Field;

  void Press(std::optional<size_t> button);
  // Tab and Shift-Tab: the next field, or button, round the dialog.
  void MoveFocus(bool reverse);
  int HeightForWidth(int width) const;
  int MessageHeight(int width) const;
  int FieldsHeight() const;
  int ButtonsHeight() const;

  const PageDialogContent content_;
  const Answer answer_;
  // How tall the content may stand, and the message's height while it
  // scrolls in it.
  int most_height_ = 0;
  std::optional<int> scrolled_message_height_;
  bool retired_ = false;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ScrollView> message_scroll_ = nullptr;
  raw_ptr<views::View> message_ = nullptr;
  std::vector<raw_ptr<views::Label>> message_lines_;
  std::vector<raw_ptr<Field>> fields_;
  std::vector<raw_ptr<views::Button>> buttons_;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_PAGE_DIALOG_H_
