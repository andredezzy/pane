// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe_run.h"

#include <crt_externs.h>
#include <libproc.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"

namespace pane::model {

std::vector<base::ProcessId> ChildProcesses() {
  const uint32_t parent = static_cast<uint32_t>(getpid());
  const int size = proc_listpids(PROC_PPID_ONLY, parent, nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::vector<pid_t> pids(static_cast<size_t>(size) / sizeof(pid_t) + 16);
  const int filled =
      proc_listpids(PROC_PPID_ONLY, parent, pids.data(),
                    static_cast<int>(pids.size() * sizeof(pid_t)));
  pids.resize(std::min(
      pids.size(), static_cast<size_t>(std::max(0, filled)) / sizeof(pid_t)));
  std::erase_if(pids, [](pid_t pid) { return pid <= 0; });
  return pids;
}

void KillProcesses(const std::vector<base::ProcessId>& processes) {
  for (base::ProcessId process : processes) {
    kill(process, SIGKILL);
  }
}

// As the Swift did: none of them answers a signal any more.
void WaitForProcesses(const std::vector<base::ProcessId>& processes,
                      base::TimeDelta timeout) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  while (
      std::ranges::any_of(
          processes, [](base::ProcessId pid) { return kill(pid, 0) == 0; }) &&
      base::TimeTicks::Now() < deadline) {
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }
}

// As the Swift's CommandLine.arguments.
base::CommandLine::StringVector StartedWith() {
  // SAFETY: the system gives `*_NSGetArgc()` arguments in `*_NSGetArgv()`.
  const base::span<char*> arguments = UNSAFE_BUFFERS(
      base::span(*_NSGetArgv(), static_cast<size_t>(*_NSGetArgc())));
  return base::CommandLine::StringVector(arguments.begin(), arguments.end());
}

bool Start(const base::CommandLine::StringVector& argv) {
  return base::LaunchProcess(argv, base::LaunchOptions()).IsValid();
}

void EndNow() {
  _exit(0);
}

}  // namespace pane::model
