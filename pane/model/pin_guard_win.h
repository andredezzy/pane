// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PIN_GUARD_WIN_H_
#define PANE_MODEL_PIN_GUARD_WIN_H_

#include "pane/model/pin_guard.h"

namespace pane::model {

// What a status of the Microsoft Platform Crypto Provider means for the PIN's
// guard on Windows (pin_guard_win.cc), as probed in the test VM (ADR 0113):
// only a key lost reads as gone, the chip's lockout as a lockout, and
// anything else as the chip refusing for now, or as no chip where TPM Base
// Services see no TPM 2.0 (`has_chip` false). `status` is a SECURITY_STATUS.

// At a key's open: NTE_BAD_KEYSET for a key deleted and for every key of a
// chip cleared since.
PinGuard::Refusal RefusalAtOpen(long status, bool has_chip);

// At the decrypt that gives the share back: NTE_PERM for a wrong PIN, and
// TPM_20_E_INTEGRITY for a key whose file was damaged.
PinGuard::Refusal RefusalAtDecrypt(long status, bool has_chip);

}  // namespace pane::model

#endif  // PANE_MODEL_PIN_GUARD_WIN_H_
