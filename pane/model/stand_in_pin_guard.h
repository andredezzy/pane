// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_STAND_IN_PIN_GUARD_H_
#define PANE_MODEL_STAND_IN_PIN_GUARD_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "pane/model/pin_guard.h"

namespace pane::model {

// A PIN's guard in memory, standing in for the TPM in the tests of the seal's
// logic: keys by name, each with its PIN and its share; wrong PINs counted,
// and at the 31st the chip's lockout, as Windows sets it, which refuses every
// PIN, the right one too; and on the test's word a chip cleared (every key
// gone) or none at all. Thread-safe: the lock calls it off its sequence.
class StandInPinGuard : public PinGuard {
 public:
  static constexpr int kLockoutAt = 31;

  StandInPinGuard();

  // Every key gone, as when the chip is cleared.
  void Clear();
  // From now, no chip: every call refuses with kUnavailable.
  void SetUnavailable(bool unavailable);

  // The wrong PINs counted so far.
  int wrong_pins() const;
  // The keys it holds, by name.
  std::vector<std::string> keys() const;

  // Runs at each Forget, before it forgets, on whichever sequence called it.
  void SetOnForget(base::RepeatingClosure on_forget);

  // PinGuard:
  base::expected<Guarded, Refusal> Guard(std::string_view pin) override;
  base::expected<Key, Refusal> Unseal(const GuardedShare& kept,
                                      std::string_view pin) override;
  void Forget(std::optional<std::string_view> kept) override;

 private:
  struct Held {
    std::string pin;
    Key share;
  };

  ~StandInPinGuard() override;

  mutable base::Lock lock_;
  std::map<std::string, Held, std::less<>> keys_ GUARDED_BY(lock_);
  int wrong_pins_ GUARDED_BY(lock_) = 0;
  bool unavailable_ GUARDED_BY(lock_) = false;
  int made_ GUARDED_BY(lock_) = 0;
  base::RepeatingClosure on_forget_ GUARDED_BY(lock_);
};

}  // namespace pane::model

#endif  // PANE_MODEL_STAND_IN_PIN_GUARD_H_
