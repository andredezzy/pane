// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TOOLBAR_ADDRESS_FIELD_H_
#define PANE_UI_TOOLBAR_ADDRESS_FIELD_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "pane/ui/design/field.h"
#include "pane/ui/design/motion.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/textfield/textfield.h"

namespace views {
class Label;
}

namespace pane {

namespace model {
class ToolbarProgress;
}

class ToolbarProgressBar;

// The toolbar's address (the Swift's AddressField, ADR 0060): a
// views::Textfield, so IME, undo, Cut/Copy/Paste, Services, and
// accessibility are Chromium's, 32 points tall, rounded 9, with no fill until
// the pointer is over it (the foreground at 3%, Motion.hover). At rest it
// shows the page as Aside does, the domain (without www.), a divider, and the
// title, at 12, its own text hidden under them and taking the click. Being
// edited, it shows the URL in a 1-point ring inside its edge, all of it
// selected as it takes the keys: a click (once the caret is placed, as the
// Electron app's input.select()), Tab, ⌘L, or the window coming back. The
// load bar runs along its bottom edge.
class AddressField : public views::Textfield {
  METADATA_HEADER(AddressField, views::Textfield)

 public:
  static constexpr int kHeight = 32;

  // `progress`, the app's, outlives the field.
  explicit AddressField(model::ToolbarProgress* progress);
  AddressField(const AddressField&) = delete;
  AddressField& operator=(const AddressField&) = delete;
  ~AddressField() override;

  // Shows `address` (the Swift's showAddress), all of it selected while the
  // field is being edited.
  void ShowAddress(const std::u16string& address);
  // The text differs from what was shown last: the user's, typed over it.
  bool IsTypedOver() const;
  // The page's title, for the field at rest.
  void SetTitle(const std::u16string& title);
  // Whether the domain and title show over the field, for the channel.
  bool label_shown() const { return label_shown_; }

  // views::Textfield:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnFocus() override;
  void OnBlur() override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnPaintBorder(gfx::Canvas* canvas) override;
  void Layout(PassKey) override;

 private:
  class Label;

  // The domain and title over the field while it isn't being edited and its
  // text is a web address; the field's own text otherwise. `editing` says
  // whether it has the keys, which a change of focus says before the focus
  // manager does.
  void UpdateLabel(bool editing);

  std::u16string shown_;
  std::u16string title_;
  bool label_shown_ = false;
  raw_ptr<Label> label_ = nullptr;
  raw_ptr<ToolbarProgressBar> bar_ = nullptr;
  // How lit the field is under the pointer, 0 to 1.
  ValueMotion hover_;
  // A press is giving the field the focus: it selects all once its caret is
  // placed, and until the pointer moves past the drag threshold.
  bool pressing_ = false;
  bool selected_by_press_ = false;
  gfx::Point press_point_;
  // Its selection dims while the window isn't the key one, as the Swift's
  // NSTextField's did.
  SelectionFollowsWindow selection_{this};
};

}  // namespace pane

#endif  // PANE_UI_TOOLBAR_ADDRESS_FIELD_H_
