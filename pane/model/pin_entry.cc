// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/pin_entry.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "pane/model/security.h"

namespace pane::model {

PinEntry::PinEntry(Mode mode, int pin_length)
    : mode_(mode),
      pin_length_(pin_length),
      step_(mode == Mode::kSetup ? Step::kEnter : Step::kVerify) {}

PinEntry::PinEntry(const PinEntry&) = default;
PinEntry& PinEntry::operator=(const PinEntry&) = default;
PinEntry::~PinEntry() = default;

// static
PinEntry PinEntry::Unreadable(std::string message) {
  PinEntry entry(Mode::kUnlock, kMinimumPinLength);
  entry.error_ = std::move(message);
  entry.busy_ = true;
  entry.is_unreadable_ = true;
  return entry;
}

std::optional<std::string_view> PinEntry::Title() const {
  if (mode_ == Mode::kUnlock) {
    return std::nullopt;
  }
  switch (step_) {
    case Step::kVerify:
      return "Enter current PIN";
    case Step::kEnter:
      return "Enter new PIN";
    case Step::kConfirm:
      return "Confirm PIN";
  }
}

int PinEntry::Dots() const {
  switch (step_) {
    case Step::kVerify:
      return pin_length_;
    case Step::kEnter:
      return std::max(static_cast<int>(entered_.size()), kMinimumPinLength);
    case Step::kConfirm:
      return static_cast<int>(first_pin_.size());
  }
}

bool PinEntry::CanContinue() const {
  return step_ == Step::kEnter &&
         static_cast<int>(entered_.size()) >= kMinimumPinLength;
}

std::optional<int> PinEntry::SubmitLength() const {
  switch (step_) {
    case Step::kVerify:
      return pin_length_;
    case Step::kEnter:
      return std::nullopt;
    case Step::kConfirm:
      return static_cast<int>(first_pin_.size());
  }
}

PinEntry::Action PinEntry::Digit(char16_t digit) {
  if (busy_ || !base::IsAsciiDigit(digit)) {
    return {};
  }

  entered_.push_back(static_cast<char>(digit));
  error_.reset();

  if (static_cast<int>(entered_.size()) != SubmitLength()) {
    return {};
  }

  busy_ = true;

  switch (step_) {
    case Step::kVerify:
      return {.kind = Action::Kind::kVerify, .pin = entered_};
    case Step::kConfirm:
      if (entered_ == first_pin_) {
        return {.kind = Action::Kind::kSetPin, .pin = entered_};
      }
      error_ = "PINs don't match";
      return {.kind = Action::Kind::kShake};
    case Step::kEnter:
      return {};
  }
}

void PinEntry::Backspace() {
  if (busy_) {
    return;
  }
  if (!entered_.empty()) {
    entered_.pop_back();
  }
  error_.reset();
}

void PinEntry::ContinueWithPin() {
  if (!CanContinue()) {
    return;
  }
  first_pin_ = std::exchange(entered_, std::string());
  step_ = Step::kConfirm;
  error_.reset();
}

PinEntry::Action PinEntry::Verified(bool success, int remaining) {
  if (!success) {
    return {.kind = mode_ == Mode::kUnlock && remaining <= 0
                        ? Action::Kind::kWipe
                        : Action::Kind::kShake};
  }

  switch (mode_) {
    case Mode::kUnlock:
      return {.kind = Action::Kind::kUnlock};
    case Mode::kChange:
      entered_.clear();
      step_ = Step::kEnter;
      busy_ = false;
      error_.reset();
      return {};
    case Mode::kRemove:
      return {.kind = Action::Kind::kRemovePin, .pin = entered_};
    case Mode::kSetup:
      return {};
  }
}

void PinEntry::Shook() {
  entered_.clear();
  busy_ = false;
}

}  // namespace pane::model
