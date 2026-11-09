// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/pin_hash.h"

#include <string>

#include "pane/model/mac/bcrypt/bcrypt.h"

namespace pane::model {

bool VerifyPinHash(std::string_view pin, std::string_view hash) {
  // As C strings, which end at a NUL, as Swift's handed them to the C.
  const std::string pin_text(pin);
  const std::string hash_text(hash);
  return bcrypt_checkpass(pin_text.c_str(), hash_text.c_str()) == 0;
}

}  // namespace pane::model
