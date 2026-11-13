// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/data_place.h"

namespace pane::model {

FileMigrator::FileMigrator() = default;
FileMigrator::FileMigrator(const FileMigrator&) = default;
FileMigrator& FileMigrator::operator=(const FileMigrator&) = default;
FileMigrator::~FileMigrator() = default;

ProfileWork::ProfileWork() = default;
ProfileWork::ProfileWork(ProfileWork&&) = default;
ProfileWork& ProfileWork::operator=(ProfileWork&&) = default;
ProfileWork::~ProfileWork() = default;

std::optional<Key> ProfileWork::KeyWhileUnlocked() const {
  if (locked->data.IsSet()) {
    return std::nullopt;
  }
  return engine_key;
}

}  // namespace pane::model
