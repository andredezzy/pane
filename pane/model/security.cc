// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/security.h"

#include <utility>

#include "base/strings/string_util.h"
#include "third_party/re2/src/re2/re2.h"

namespace pane::model {

namespace {

// A field Swift decodes if present: absent and null are both nothing, and
// another type fails the whole state.
template <typename T>
bool Field(const base::DictValue& dict,
           std::string_view key,
           std::optional<T>& out,
           std::optional<T> (*read)(const base::Value&)) {
  const base::Value* value = dict.Find(key);
  if (!value || value->is_none()) {
    return true;
  }
  out = read(*value);
  return out.has_value();
}

std::optional<int> ReadInt(const base::Value& value) {
  return value.GetIfInt();
}

std::optional<Pin> ReadPin(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::optional<int> length = dict->FindInt("length");
  if (!length) {
    return std::nullopt;
  }
  Pin pin{.length = *length};
  if (const base::Value* hash = dict->Find("hash"); hash && !hash->is_none()) {
    if (!hash->is_string()) {
      return std::nullopt;
    }
    pin.hash = hash->GetString();
  }
  return pin;
}

std::optional<SealedKey> ReadKey(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  return dict ? SealedKey::FromValue(*dict) : std::nullopt;
}

}  // namespace

SecurityState::SecurityState() = default;

SecurityState::SecurityState(std::optional<Pin> pin,
                             int failed_attempts,
                             std::optional<SealedKey> key)
    : pin(std::move(pin)),
      failed_attempts(failed_attempts),
      key(std::move(key)) {}

SecurityState::SecurityState(const SecurityState&) = default;
SecurityState& SecurityState::operator=(const SecurityState&) = default;
SecurityState::~SecurityState() = default;

// static
std::optional<SecurityState> SecurityState::FromValue(
    const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  SecurityState state;
  std::optional<int> failed_attempts;
  if (!Field(*dict, "pin", state.pin, &ReadPin) ||
      !Field(*dict, "failedAttempts", failed_attempts, &ReadInt) ||
      !Field(*dict, "key", state.key, &ReadKey)) {
    return std::nullopt;
  }
  state.failed_attempts = failed_attempts.value_or(0);
  return state;
}

base::Value SecurityState::ToValue() const {
  base::DictValue dict;
  if (pin) {
    base::DictValue pin_value;
    // Absent without a hash, as Swift's synthesized coding leaves it.
    if (pin->hash) {
      pin_value.Set("hash", *pin->hash);
    }
    pin_value.Set("length", pin->length);
    dict.Set("pin", std::move(pin_value));
  } else {
    dict.Set("pin", base::Value());
  }
  dict.Set("failedAttempts", failed_attempts);
  if (key) {
    dict.Set("key", key->ToValue());
  }
  return base::Value(std::move(dict));
}

// static
std::optional<SecurityState> SecurityState::Recover(std::string_view text) {
  // 0.13.1 and before wrote the hash through JSONEncoder, which escapes each
  // "/" of bcrypt's alphabet as "\/".
  std::string unescaped(text);
  base::ReplaceSubstringsAfterOffset(&unescaped, 0, "\\/", "/");

  static const re2::LazyRE2 kHash = {R"((\$2[aby]\$\d\d\$[./A-Za-z0-9]{53}))"};
  static const re2::LazyRE2 kLength = {R"("length"\s*:\s*(\d{1,2}))"};
  static const re2::LazyRE2 kFailedAttempts = {
      R"("failedAttempts"\s*:\s*(\d{1,2}))"};

  std::string hash;
  int length = 0;
  if (!re2::RE2::PartialMatch(unescaped, *kHash, &hash) ||
      !re2::RE2::PartialMatch(unescaped, *kLength, &length) ||
      length < kMinimumPinLength) {
    return std::nullopt;
  }
  int failed_attempts = 0;
  re2::RE2::PartialMatch(unescaped, *kFailedAttempts, &failed_attempts);
  return SecurityState(Pin{.hash = std::move(hash), .length = length},
                       failed_attempts, std::nullopt);
}

}  // namespace pane::model
