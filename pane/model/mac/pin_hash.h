// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_PIN_HASH_H_
#define PANE_MODEL_MAC_PIN_HASH_H_

#include <string_view>

namespace pane::model {

// A PIN set before ADR 0113, checked once against its bcrypt hash at the first
// unlock, when the PIN seals the data key and the hash goes (Pin::hash). Read
// through OpenBSD's bcrypt, which reads the Electron app's bcryptjs hashes,
// $2a$ and $2b$ alike. Pane makes no such hash any more.
bool VerifyPinHash(std::string_view pin, std::string_view hash);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_PIN_HASH_H_
