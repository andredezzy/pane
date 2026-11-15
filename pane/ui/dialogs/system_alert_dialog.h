// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_SYSTEM_ALERT_DIALOG_H_
#define PANE_UI_DIALOGS_SYSTEM_ALERT_DIALOG_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/dialogs/system_alert.h"
#include "ui/views/view.h"

namespace pane {

class DialogHeading;
class DialogTile;
class KeyedButton;

// The system's alert where Pane draws it (Windows, Paper 11.13 A: Delete
// profile's), as Exit Pane? is drawn: the trash's 40-point tile, the title
// and what the action does, then Cancel (Esc) and the destructive action,
// which no key presses, never the default, as on the Mac; 16 apart, the
// buttons 8 further down, at the right, 8 apart. Tab moves between the two,
// and Space or Enter presses the one with the ring.
class SystemAlertDialog : public views::View {
  METADATA_HEADER(SystemAlertDialog, views::View)

 public:
  // The dialog's width, its content 24 in.
  static constexpr int kWidth = 480;

  SystemAlertDialog(const SystemAlert& alert,
                    base::RepeatingClosure cancel,
                    base::RepeatingClosure act);
  SystemAlertDialog(const SystemAlertDialog&) = delete;
  SystemAlertDialog& operator=(const SystemAlertDialog&) = delete;
  ~SystemAlertDialog() override;

  const SystemAlert& alert() const { return alert_; }
  views::View* cancel_button();
  views::View* action_button();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  const SystemAlert alert_;
  raw_ptr<DialogTile> tile_ = nullptr;
  raw_ptr<DialogHeading> heading_ = nullptr;
  raw_ptr<KeyedButton> cancel_ = nullptr;
  raw_ptr<KeyedButton> action_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_SYSTEM_ALERT_DIALOG_H_
