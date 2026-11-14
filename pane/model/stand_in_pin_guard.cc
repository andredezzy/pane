// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/stand_in_pin_guard.h"

#include <utility>

#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/random.h"

namespace pane::model {

StandInPinGuard::StandInPinGuard() = default;
StandInPinGuard::~StandInPinGuard() = default;

void StandInPinGuard::Clear() {
  base::AutoLock lock(lock_);
  keys_.clear();
}

void StandInPinGuard::SetNoChip(bool no_chip) {
  base::AutoLock lock(lock_);
  no_chip_ = no_chip;
}

void StandInPinGuard::SetGuardRefusal(std::optional<Refusal> refusal) {
  base::AutoLock lock(lock_);
  guard_refusal_ = refusal;
}

int StandInPinGuard::wrong_pins() const {
  base::AutoLock lock(lock_);
  return wrong_pins_;
}

std::vector<std::string> StandInPinGuard::keys() const {
  base::AutoLock lock(lock_);
  std::vector<std::string> names;
  for (const auto& [name, held] : keys_) {
    names.push_back(name);
  }
  return names;
}

void StandInPinGuard::SetOnForget(base::RepeatingClosure on_forget) {
  base::AutoLock lock(lock_);
  on_forget_ = std::move(on_forget);
}

base::expected<Guarded, PinGuard::Refusal> StandInPinGuard::Guard(
    std::string_view pin) {
  base::AutoLock lock(lock_);
  if (no_chip_) {
    return base::unexpected(Refusal::kNoChip);
  }
  if (wrong_pins_ >= kLockoutAt) {
    return base::unexpected(Refusal::kLockedOut);
  }
  if (guard_refusal_) {
    return base::unexpected(*guard_refusal_);
  }
  Guarded guarded;
  guarded.share = crypto::RandBytesAsArray<kKeySize>();
  guarded.kept = {
      .key = "Pane data key stand-in " + base::NumberToString(++made_),
      // What a chip's sealed share would be: opaque, and nothing without it.
      .share = base::Base64Encode(crypto::RandBytesAsArray<256>())};
  keys_[guarded.kept.key] =
      Held{.pin = std::string(pin), .share = guarded.share};
  return guarded;
}

base::expected<Key, PinGuard::Refusal> StandInPinGuard::Unseal(
    const GuardedShare& kept,
    std::string_view pin) {
  base::AutoLock lock(lock_);
  if (no_chip_) {
    return base::unexpected(Refusal::kNoChip);
  }
  const auto held = keys_.find(kept.key);
  if (held == keys_.end()) {
    return base::unexpected(Refusal::kGone);
  }
  if (wrong_pins_ >= kLockoutAt) {
    return base::unexpected(Refusal::kLockedOut);
  }
  if (held->second.pin != pin) {
    ++wrong_pins_;
    return base::unexpected(Refusal::kWrongPin);
  }
  return held->second.share;
}

void StandInPinGuard::Forget(std::optional<std::string_view> kept) {
  base::RepeatingClosure on_forget;
  {
    base::AutoLock lock(lock_);
    on_forget = on_forget_;
  }
  if (on_forget) {
    on_forget.Run();
  }
  base::AutoLock lock(lock_);
  std::erase_if(
      keys_, [&](const auto& entry) { return !kept || entry.first != *kept; });
}

}  // namespace pane::model
