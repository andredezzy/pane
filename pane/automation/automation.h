// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_AUTOMATION_H_
#define PANE_AUTOMATION_AUTOMATION_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "base/values.h"
#include "pane/model/pin_entry.h"

namespace pane {

class UiApp;

// A test-only command channel (ADR 0018), on when PANE_AUTOMATION_DIR is set
// in a scenario's launch, which runs on the mock keychain (StartViewsLaunch):
// the scenario runner drops JSON commands into `<dir>/in/<n>.json` and reads
// the result, with a snapshot of what shows, from `<dir>/out/<n>.json`. The
// files are watched (kqueue on the Mac), not polled. Its commands call what
// the UI calls, so scenarios exercise the app's paths.
class Automation {
 public:
  Automation(const base::FilePath& directory, UiApp* app);
  Automation(const Automation&) = delete;
  Automation& operator=(const Automation&) = delete;
  ~Automation();

 private:
  class Files;
  using Reply = base::OnceCallback<void(base::Value result, std::string error)>;

  void Run(std::string name, base::DictValue command);
  void Dispatch(const std::string& command_name,
                const base::DictValue& command,
                Reply reply);

  // Each profile's state a command read, for the replies' profiles: its HTTP
  // cache's bytes, and its cookies without their values.
  void AddProfileReads(base::DictValue& state) const;

  // Opens the PIN screen in `mode` as a user does (showPinScreen), a step at
  // a time: Settings shown by the sidebar's Settings where it isn't, the
  // mode's button scrolled into view, then pressed; then waits for the
  // screen, 5 s from `started` at most.
  void OpenPinScreen(model::PinEntry::Mode mode,
                     base::TimeTicks started,
                     int step,
                     Reply reply);

  raw_ptr<UiApp> app_;
  base::SequenceBound<Files> files_;
  std::map<std::string, int64_t> cache_sizes_;
  std::map<std::string, base::ListValue> cookies_;
  base::WeakPtrFactory<Automation> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_AUTOMATION_AUTOMATION_H_
