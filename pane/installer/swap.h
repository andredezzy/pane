// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_SWAP_H_
#define PANE_INSTALLER_SWAP_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"

class WorkItemList;

namespace base {
class CommandLine;
}

namespace pane::installer {

// The update's last step on Windows (ADR 0127; Paper 11.12.14): the helper,
// copied out of the staged folder (a folder a running program is in can't be
// renamed) and started by Pane as it exits, waits for Pane's processes,
// renderers too, ends what outlived the browser, swaps the staged folder in
// for the app folder, rolling back on any failure, opens the new Pane for
// Restart to update, and removes the old app, the installer it came from,
// and its own copy. It has no window.

// What the swap came to, which is the helper's exit code too.
enum class SwapOutcome {
  // The new version is in place, and opened where asked.
  kInstalled = 0,
  // A step failed and rolled back: the app as it was, the staged copy kept
  // for the next exit, and the mark left for the next launch, which says so
  // above the pill (Paper 11.12.14, call E). Restart to update opens the
  // old Pane again.
  kNotInstalled = 1,
  // Not tried: Windows is ending the session, where Chromium's own swap
  // waits too (browser_shutdown.cc, kEndSession); or another Pane runs from
  // the folder, opened again or on data of its own, which is never ended,
  // and the mark is left as for a failure. The next exit installs it.
  kDeferred = 2,
  // The command line names no swap this helper makes.
  kRefused = 3,
};

// The swap Pane asks for at its exit (UpdateInstaller::Install).
struct SwapRequest {
  SwapRequest();
  SwapRequest(const SwapRequest&);
  SwapRequest& operator=(const SwapRequest&);
  ~SwapRequest();

  // The app folder, laid out as an install lays it out, and the version
  // staged beside it.
  base::FilePath app;
  base::FilePath staged;
  // The installer the staged version came from, which goes once it is
  // installed; empty where none is known.
  base::FilePath download;
  // The browser that is exiting.
  base::ProcessId browser = base::kNullProcessId;
  // Restart to update: the new Pane opens, with these arguments, in front
  // where it may take it (UpdateInstaller::Relaunch).
  bool relaunch = false;
  bool in_front = false;
  std::vector<std::wstring> arguments;
};

// The request a helper's command line makes, where it names a staged version
// beside an app folder laid out as an install: nullopt otherwise.
std::optional<SwapRequest> SwapRequestFrom(
    const base::CommandLine& command_line);

// The command line Pane starts `helper` with for `request`.
base::CommandLine SwapCommandLine(const base::FilePath& helper,
                                  const SwapRequest& request);

// The processes running from a folder when the helper starts, the exiting
// Pane's, each held by its handle, so none's id can name another process
// later: a Pane opened after the snapshot is never one of them.
class PaneProcesses {
 public:
  explicit PaneProcesses(const base::FilePath& folder);
  PaneProcesses(const PaneProcesses&) = delete;
  PaneProcesses& operator=(const PaneProcesses&) = delete;
  ~PaneProcesses();

  size_t size() const { return processes_.size(); }
  bool Contains(base::ProcessId pid) const;

  // Waits until each has exited, `timeout` at most: whether they all have.
  bool WaitForExit(base::TimeDelta timeout) const;

  // Waits until the one of them `pid` names has exited, `timeout` at most:
  // whether it has. True where it isn't one of them, gone before the
  // snapshot.
  bool WaitForExitOf(base::ProcessId pid, base::TimeDelta timeout) const;

  // Takes in what one of them started from the folder since the snapshot,
  // and what that started in turn: Pane's own, as it went.
  void Adopt();

  // Ends those still running: what outlived Pane's browser, an idle renderer
  // (the test VM's rounds of 2026-11-14 and 2026-11-15 found them), a
  // notification helper.
  void End() const;

  // Whether a process from the folder runs that isn't one of them, nor
  // started by one (Adopt): Pane, opened again since.
  bool OthersRun() const;

  // Whether another Pane runs from the folder: a browser still there once
  // the exiting one has gone (its process singleton's window, PanesFrom),
  // opened again or on data of its own, or a process none of them started.
  // Then nothing is ended, as the installer ends nothing while a browser is
  // there to ask (running_pane.h).
  bool AnotherRuns() const;

 private:
  const base::FilePath folder_;
  std::vector<base::Process> processes_;
};

// The swap: `staged` in place of `app`, the app aside under `swapping` while
// it runs, through Chromium's WorkItemList, which puts everything back if a
// step fails. The list it returns holds the old app aside, and removes it
// once destroyed: the new Pane opens first. Null where the swap failed and
// rolled back.
std::unique_ptr<WorkItemList> SwapIn(const base::FilePath& app,
                                     const base::FilePath& staged,
                                     const base::FilePath& swapping);

// The helper's whole run for `request`. Blocks until Pane has gone, then
// for the swap.
SwapOutcome RunSwap(const SwapRequest& request);

// What RunSwap noted as it went, each step and when (a failed swap's mark
// holds them too).
const std::string& SwapNotes();

// For a scenario's copy alone, its app folder in the temporary folder and its
// channel's folder named (PANE_AUTOMATION_DIR, which the helper inherits from
// the Pane that started it): the notes, appended to swap-notes.txt there.
void NoteSwapForScenario(const SwapRequest& request);

}  // namespace pane::installer

#endif  // PANE_INSTALLER_SWAP_H_
