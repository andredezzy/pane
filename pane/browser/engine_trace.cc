// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/engine_trace.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "pane/model/test_hooks.h"

namespace pane {

namespace {

// Opened once, on first use, and written through on every line so the trace
// survives a crash right after the traced call. A test aid, not a hot path:
// a scenario's, on the mock keychain alone (model::TestHook), since the trace
// names the pages.
base::File* TraceFile() {
  static base::NoDestructor<base::File> file([] {
    const std::optional<std::string> path =
        model::TestHook("PANE_ENGINE_TRACE");
    if (!path) {
      return base::File();
    }
    return base::File(base::FilePath::FromUTF8Unsafe(*path),
                      base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  }());
  return file->IsValid() ? file.get() : nullptr;
}

}  // namespace

bool IsTracing() {
  static const bool tracing = TraceFile() != nullptr;
  return tracing;
}

void Trace(std::string_view kind, std::string_view name, base::DictValue args) {
  base::File* file = IsTracing() ? TraceFile() : nullptr;
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

  // A line that cannot be written (a full disk) is lost: a test aid never
  // takes the browser down.
  json->push_back('\n');
  std::ignore = file->WriteAtCurrentPos(base::as_byte_span(*json));
}

}  // namespace pane
