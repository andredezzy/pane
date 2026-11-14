// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_LOCK_LOCK_SCREEN_VIEW_H_
#define PANE_UI_LOCK_LOCK_SCREEN_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/model/pin_entry.h"
#include "pane/ui/lock/lock_update_status.h"
#include "ui/views/view.h"

namespace pane {

class PanelFrame;

// The PIN screen (the Swift's LockScreenView, the Electron app's lock-screen
// page and numpad.tsx; Paper 07.1 to 07.9, ADR 0042): a panel over the whole
// window, 8 points in from its edges, on the content panel's card, ring, and
// shadow, which moves the window around its controls. A back arrow at its
// left while the screen can be left; in its middle, 28 apart, the step's
// title, the dots, the numpad, and the hints, the error, and the attempts
// left; or, while it runs, the data's one-time move into its volume (Paper
// 07.5, ADR 0113), the two crossfading; at its bottom, while locked, the
// update on offer (DESIGN.md › Updates). The dots and the numpad show at 40%
// while no key can be read (07.6 to 07.8).
//
// It rises from below the window as it comes (Motion.lockIn) and sinks back
// as it goes (Motion.lockOut), on the compositor's own animation, over the
// window as the window already is: emptied before it rises, and in place
// again before it sinks (ADR 0116). In a window's first frame, at launch, it
// is already in place. While a PIN shows it takes every key, so none reaches
// a page or the window's commands but Quit (ADR 0019): a digit types, Delete
// takes one back, Esc goes back, Return continues; and VoiceOver stays in it.
class LockScreenView : public views::View {
  METADATA_HEADER(LockScreenView, views::View)

 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // A key typed or pressed: a digit, Delete, Continue, and Esc or the
    // back arrow.
    base::RepeatingCallback<void(char16_t)> digit;
    base::RepeatingClosure backspace;
    base::RepeatingClosure continue_with_pin;
    base::RepeatingClosure back;
    // The update's action beside its status.
    base::RepeatingClosure update;
  };

  // What the screen shows.
  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    // The PIN screen, while it shows.
    std::optional<model::PinEntry> entry;
    // Wrong PINs at the lock in a row that count toward the panic wipe, on
    // disk: the attempts left show under the dots. None on a seal the TPM
    // guards, which never wipes: no attempts show there (Paper 07.1.1).
    std::optional<int> attempts_toward_wipe;
    // How far the data's one-time move has come, while it moves.
    std::optional<double> move_share;
    // The update on offer, at the lock.
    std::optional<LockUpdateStatus::Shows> update;

    // Whether the screen shows at all.
    bool shown() const { return entry.has_value() || move_share.has_value(); }
  };

  explicit LockScreenView(Callbacks callbacks);
  LockScreenView(const LockScreenView&) = delete;
  LockScreenView& operator=(const LockScreenView&) = delete;
  ~LockScreenView() override;

  // Shows what `shows` holds: rising as it comes and sinking as it goes when
  // `animate`, or there at once (a window's first frame). Gone, it keeps
  // what it last showed as it sinks.
  void SetShows(const Shows& shows, bool animate);
  // Shown and not on its way out.
  bool showing() const { return GetVisible() && !leaving_; }
  // A PIN shows, taking every key.
  bool takes_every_key() const;
  // A wrong PIN: the dots shake.
  void Shake();
  // The panel takes the keys.
  void TakeFocus();
  // Whether a press at `point`, in this view, moves the window: anywhere on
  // the panel and around it but its controls.
  bool IsDragArea(const gfx::Point& point);
  // Its controls and parts by the Swift's names, while it shows.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  void Layout(PassKey) override;
  void OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                  float new_device_scale_factor) override;

 private:
  class Panel;
  class PinContent;
  class MoveContent;

  // The panel, at rest shown or gone.
  void Settle(bool shown);
  // The panel rising or sinking; `done` once it is there.
  void Slide(bool shown);
  void Slid(bool shown);
  // The PIN or the data's move, crossfading when `animate`.
  void ShowContent(bool animate);

  Callbacks callbacks_;
  Shows shows_;
  bool leaving_ = false;
  raw_ptr<PanelFrame> frame_ = nullptr;
  raw_ptr<Panel> panel_ = nullptr;
  raw_ptr<views::View> back_ = nullptr;
  raw_ptr<PinContent> pin_ = nullptr;
  raw_ptr<MoveContent> move_ = nullptr;
  raw_ptr<LockUpdateStatus> update_ = nullptr;
  // What the panel shows now: the PIN, or the data's move.
  bool pin_shown_ = true;
  base::WeakPtrFactory<LockScreenView> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_LOCK_LOCK_SCREEN_VIEW_H_
