// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/engine_trace.h"

#include <optional>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/time/time.h"

namespace pane {

namespace {

// Opened once, on first use, and written through on every line so the trace
// survives a crash right after the traced call. A test aid, not a hot path.
base::File* TraceFile() {
  static base::NoDestructor<base::File> file([] {
    std::optional<std::string> path =
        base::Environment::Create()->GetVar("PANE_ENGINE_TRACE");
    if (!path || path->empty()) {
      return base::File();
    }
    return base::File(base::FilePath(*path),
                      base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  }());
  return file->IsValid() ? file.get() : nullptr;
}

}  // namespace

void Trace(std::string_view kind, std::string_view name, base::DictValue args) {
  base::File* file = TraceFile();
  if (!file) {
    return;
  }

  base::DictValue line;
  line.Set("t", base::Time::Now().InMillisecondsFSinceUnixEpoch());
  line.Set("kind", kind);
  line.Set("name", name);
  line.Set("args", std::move(args));

  std::optional<std::string> json = base::WriteJson(line);
  if (!json) {
    return;
  }

  json->push_back('\n');
  file->WriteAtCurrentPosAndCheck(base::as_byte_span(*json));
}

}  // namespace pane
