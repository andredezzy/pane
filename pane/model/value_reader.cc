// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/value_reader.h"

namespace pane::model {

namespace {

std::optional<std::string> StringOf(const base::Value* value) {
  const std::string* string = value ? value->GetIfString() : nullptr;
  return string ? std::make_optional(*string) : std::nullopt;
}

}  // namespace

ValueReader::ValueReader(const base::Value* value)
    : dict_(value ? value->GetIfDict() : nullptr), ok_(dict_ != nullptr) {}

ValueReader::~ValueReader() = default;

const base::Value* ValueReader::Present(std::string_view key) const {
  const base::Value* value = dict_ ? dict_->Find(key) : nullptr;
  return value && !value->is_none() ? value : nullptr;
}

const base::Value* ValueReader::Required(std::string_view key) {
  const base::Value* value = Present(key);
  ok_ = ok_ && value;
  return value;
}

std::string ValueReader::String(std::string_view key) {
  return Check(StringOf(Required(key))).value_or(std::string());
}

std::optional<std::string> ValueReader::OptionalString(std::string_view key) {
  const base::Value* value = Present(key);
  return value ? Check(StringOf(value)) : std::nullopt;
}

int ValueReader::Int(std::string_view key) {
  const base::Value* value = Required(key);
  return Check(value ? value->GetIfInt() : std::nullopt).value_or(0);
}

std::optional<int> ValueReader::OptionalInt(std::string_view key) {
  const base::Value* value = Present(key);
  return value ? Check(value->GetIfInt()) : std::nullopt;
}

double ValueReader::Double(std::string_view key) {
  const base::Value* value = Required(key);
  return Check(value ? value->GetIfDouble() : std::nullopt).value_or(0);
}

std::optional<bool> ValueReader::OptionalBool(std::string_view key) {
  const base::Value* value = Present(key);
  return value ? Check(value->GetIfBool()) : std::nullopt;
}

std::vector<std::string> ValueReader::Strings(std::string_view key) {
  std::vector<std::string> strings;
  const base::Value* value = Required(key);
  const base::ListValue* list = value ? value->GetIfList() : nullptr;
  ok_ = ok_ && list;
  if (list) {
    for (const base::Value& item : *list) {
      strings.push_back(Check(StringOf(&item)).value_or(std::string()));
    }
  }
  return strings;
}

}  // namespace pane::model
