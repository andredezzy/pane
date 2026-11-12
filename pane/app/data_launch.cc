// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/app/data_launch.h"

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "pane/model/test_hooks.h"

namespace pane {

base::FilePath DataDirectory() {
  if (const std::optional<std::string> named =
          model::TestHook("PANE_DATA_DIR")) {
    return base::FilePath::FromUTF8Unsafe(*named);
  }
  return DefaultDataDirectory();
}

}  // namespace pane
