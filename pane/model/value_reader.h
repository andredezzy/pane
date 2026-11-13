// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_VALUE_READER_H_
#define PANE_MODEL_VALUE_READER_H_

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/values.h"

namespace pane::model {

// Reads a JSON object as Swift's Decodable does: a required key absent or
// null fails, an optional one absent or null is nothing, and either of the
// wrong type fails. Any failure fails the whole value.
class ValueReader {
 public:
  explicit ValueReader(const base::Value* value);
  ValueReader(const ValueReader&) = delete;
  ValueReader& operator=(const ValueReader&) = delete;
  ~ValueReader();

  bool ok() const { return ok_; }

  const base::Value* Present(std::string_view key) const;
  const base::Value* Required(std::string_view key);

  std::string String(std::string_view key);
  std::optional<std::string> OptionalString(std::string_view key);
  int Int(std::string_view key);
  std::optional<int> OptionalInt(std::string_view key);
  double Double(std::string_view key);
  std::optional<bool> OptionalBool(std::string_view key);
  std::vector<std::string> Strings(std::string_view key);

  template <typename Enum, size_t N>
  std::optional<Enum> OptionalEnum(
      std::string_view key,
      const std::array<std::string_view, N>& names) {
    const base::Value* value = Present(key);
    if (!value) {
      return std::nullopt;
    }
    const std::string* name = value->GetIfString();
    const auto it = name ? std::ranges::find(names, *name) : names.end();
    ok_ = ok_ && it != names.end();
    return it == names.end() ? std::nullopt
                             : std::make_optional(static_cast<Enum>(
                                   std::distance(names.begin(), it)));
  }

  template <typename Enum, size_t N>
  Enum RequiredEnum(std::string_view key,
                    const std::array<std::string_view, N>& names) {
    ok_ = ok_ && Present(key);
    return OptionalEnum<Enum>(key, names).value_or(Enum());
  }

  // A value another reader takes, and fails with.
  template <typename T>
  T Take(std::optional<T> value) {
    ok_ = ok_ && value.has_value();
    return value ? std::move(*value) : T();
  }

 private:
  template <typename T>
  std::optional<T> Check(std::optional<T> value) {
    ok_ = ok_ && value.has_value();
    return value;
  }

  raw_ptr<const base::DictValue> dict_;
  bool ok_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_VALUE_READER_H_
