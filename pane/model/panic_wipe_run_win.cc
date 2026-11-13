// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe_run.h"

#include <windows.h>

#include <algorithm>

#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/process/process_iterator.h"
#include "base/time/time.h"

namespace pane::model {

// The processes whose parent is this one, from the system's list (no job
// object holds them for Pane). Windows keeps a process's parent id after the
// parent has gone, and gives ids out again, so a child is one started after
// this one.
std::vector<base::ProcessId> ChildProcesses() {
  const base::ProcessId self = base::GetCurrentProcId();
  const base::Time started = base::Process::Current().CreationTime();
  std::vector<base::ProcessId> children;
  base::ProcessIterator processes(/*filter=*/nullptr);
  while (const base::ProcessEntry* entry = processes.NextProcessEntry()) {
    if (entry->parent_pid() != self || entry->pid() == self) {
      continue;
    }
    const base::Process child = base::Process::OpenWithAccess(
        entry->pid(), PROCESS_QUERY_LIMITED_INFORMATION);
    if (child.IsValid() && child.CreationTime() >= started) {
      children.push_back(entry->pid());
    }
  }
  return children;
}

void KillProcesses(const std::vector<base::ProcessId>& processes) {
  for (base::ProcessId process : processes) {
    base::Process::OpenWithAccess(process, PROCESS_TERMINATE)
        .Terminate(/*exit_code=*/1, /*wait=*/false);
  }
}

// Each waited for in turn, by its handle, until the deadline: a process
// already gone opens no handle.
void WaitForProcesses(const std::vector<base::ProcessId>& processes,
                      base::TimeDelta timeout) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  for (base::ProcessId pid : processes) {
    const base::Process process = base::Process::OpenWithAccess(
        pid, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION);
    if (!process.IsValid()) {
      continue;
    }
    process.WaitForExitWithTimeout(
        std::max(deadline - base::TimeTicks::Now(), base::TimeDelta()),
        /*exit_code=*/nullptr);
  }
}

// The command line as the system gave it, before Chromium's own reading.
base::CommandLine::StringVector StartedWith() {
  return base::CommandLine::FromString(::GetCommandLineW()).argv();
}

bool Start(const base::CommandLine::StringVector& argv) {
  return base::LaunchProcess(base::CommandLine(argv), base::LaunchOptions())
      .IsValid();
}

void EndNow() {
  base::Process::TerminateCurrentProcessImmediately(0);
}

}  // namespace pane::model
