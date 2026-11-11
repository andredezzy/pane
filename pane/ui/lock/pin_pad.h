// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_LOCK_PIN_PAD_H_
#define PANE_UI_LOCK_PIN_PAD_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "pane/ui/design/motion.h"
#include "ui/views/view.h"

namespace pane {

// The PIN's dots (the Swift's PinDots, the Electron app's numpad.tsx): circles
// 13 points across and 14 apart, filled for each digit entered and ringed
// after (1.5 points, the foreground at 25%). A dot fills as its ring fades,
// over 0.15 s easing in and out (Motion's Dot), and as a new PIN's dots grow
// or shrink in number the others slide to their new places over the same
// time, a new one there at once, as SwiftUI drew them. A wrong PIN shakes the
// row (ShakeOffset), unless motion is reduced, on a layer of its own, so the
// shake repaints nothing. The row sits in the middle of the view, to the
// fraction of a point.
class PinDots : public views::View {
  METADATA_HEADER(PinDots, views::View)

 public:
  static constexpr int kSize = 13;
  static constexpr int kGap = 14;

  PinDots();
  PinDots(const PinDots&) = delete;
  PinDots& operator=(const PinDots&) = delete;
  ~PinDots() override;

  // `count` dots, `filled` of them filled: there at once, or moving there in
  // a window that draws.
  void Set(int count, int filled, bool animate);
  // A wrong PIN.
  void Shake();

  int count() const { return static_cast<int>(dots_.size()); }
  int filled() const { return filled_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  struct Dot;

  ::ui::Compositor* compositor();
  void Shook(double share);

  std::vector<std::unique_ptr<Dot>> dots_;
  int filled_ = 0;
  // How far through its 0.4 s the shake is.
  ValueMotion shake_;
};

// The numpad (the Swift's Numpad): 1 to 9, then 0 and Delete, in keys 64
// points across, 14 apart. A digit is a ring with its number, 26 points and
// thin; Delete, the Electron app's glyph. Each lights under the pointer and
// fills while pressed, fading over 0.15 s both ways (Motion.hover), as the
// Swift's keys did; neither takes the keyboard, which the PIN screen holds.
class Numpad : public views::View {
  METADATA_HEADER(Numpad, views::View)

 public:
  static constexpr int kKey = 64;
  static constexpr int kGap = 14;
  static constexpr int kWidth = 3 * kKey + 2 * kGap;
  static constexpr int kHeight = 4 * kKey + 3 * kGap;

  Numpad(base::RepeatingCallback<void(char16_t)> digit,
         base::RepeatingClosure backspace);
  Numpad(const Numpad&) = delete;
  Numpad& operator=(const Numpad&) = delete;
  ~Numpad() override;

  // Its keys by the Swift's names ("pane.lockScreen.digit.4",
  // "pane.lockScreen.backspace").
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  class DigitKey;
  class DeleteKey;

  std::vector<raw_ptr<DigitKey>> digits_;
  raw_ptr<DeleteKey> delete_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_LOCK_PIN_PAD_H_
