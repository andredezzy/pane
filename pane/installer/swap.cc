// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/swap.h"

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/scoped_handle.h"
#include "chrome/installer/util/work_item.h"
#include "chrome/installer/util/work_item_list.h"
#include "pane/installer/files.h"
#include "pane/installer/running_pane.h"
#include "pane/installer/switches.h"
#include "pane/installer/update_folder.h"

namespace pane::installer {

namespace {

// How long Pane's browser has to exit once it has started the helper: its
// shutdown saves, closes its profiles, and stops the engine in about a
// second; one that hasn't gone after this is stuck, and the next exit tries
// again.
constexpr base::TimeDelta kBrowserExits = base::Minutes(2);
// How long the rest of Pane's processes have to follow it: a renderer, the
// GPU's, and the network service's go within a few hundred milliseconds of
// their browser; what is still there after this outlived it, and is ended.
constexpr base::TimeDelta kChildrenFollow = base::Seconds(1);
// How long an ended process has to go.
constexpr base::TimeDelta kEndedGo = base::Seconds(5);

// How long the old app's removal is tried for: 10 s.
constexpr int kRemoveTries = 50;
constexpr base::TimeDelta kRemoveInterval = base::Milliseconds(200);

constexpr std::wstring_view kSwitchTerminator = L"--";

// What the helper notes as it goes: the system's debug log, and the mark a
// failed swap leaves, which names the step.
std::string& Notes() {
  static base::NoDestructor<std::string> notes;
  return *notes;
}

void Note(const std::string& line) {
  LOG(WARNING) << line;
  Notes() += line + "\r\n";
}

// Whether two paths name one place, compared as NTFS compares names.
bool SamePath(const base::FilePath& a, const base::FilePath& b) {
  return IsInFolder(a, b) && IsInFolder(b, a);
}

// The app folders a swap replaces, and whose old copy it deletes: the
// install's own, or a scenario's copy laid out in the temporary folder
// (UpdateInstaller::ForRunningApp takes no other), never one a command line
// names elsewhere.
bool MaySwap(const base::FilePath& app) {
  base::FilePath temp;
  return IsInstalledAppFolder(app) ||
         (base::GetTempDir(&temp) && IsInFolder(app, temp));
}

// The folder a launch of Pane starts in: the user's own, as a shortcut's
// does, never the app folder, which its current folder would hold.
base::FilePath UserFolder() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home;
}

// Opens the Pane in `app`, as Restart to update asks: with the arguments it
// was opened with, in front where it may take it. The app-starting pointer
// shows until its window does (Paper 11.12.14).
void OpenPane(const base::FilePath& app, const SwapRequest& request) {
  std::vector<std::wstring> argv = {app.Append(L"chrome.exe").value()};
  argv.insert(argv.end(), request.arguments.begin(), request.arguments.end());
  base::LaunchOptions options;
  options.current_directory = UserFolder();
  options.grant_foreground_privilege = request.in_front;
  const base::Process pane =
      base::LaunchProcess(base::CommandLine(argv), options);
  Note(pane.IsValid()
           ? "opened Pane"
           : "could not open Pane: " + base::NumberToString(::GetLastError()));
}

}  // namespace

const std::string& SwapNotes() {
  return Notes();
}

void NoteSwapForScenario(const SwapRequest& request) {
  base::FilePath temp;
  const std::optional<std::string> channel =
      base::Environment::Create()->GetVar("PANE_AUTOMATION_DIR");
  if (!channel || channel->empty() || !base::GetTempDir(&temp) ||
      !IsInFolder(request.app, temp)) {
    return;
  }
  const base::FilePath folder = base::FilePath::FromUTF8Unsafe(*channel);
  if (!IsInFolder(folder, temp)) {
    return;
  }
  const base::FilePath notes = folder.Append(L"swap-notes.txt");
  if (base::PathExists(notes)) {
    base::AppendToFile(notes, Notes());
  } else {
    base::WriteFile(notes, Notes());
  }
}

SwapRequest::SwapRequest() = default;
SwapRequest::SwapRequest(const SwapRequest&) = default;
SwapRequest& SwapRequest::operator=(const SwapRequest&) = default;
SwapRequest::~SwapRequest() = default;

std::optional<SwapRequest> SwapRequestFrom(
    const base::CommandLine& command_line) {
  if (!command_line.HasSwitch(kSwapSwitch)) {
    return std::nullopt;
  }
  SwapRequest request;
  request.app = command_line.GetSwitchValuePath(kAppSwitch);
  request.staged = command_line.GetSwitchValuePath(kStagedSwitch);
  request.download = command_line.GetSwitchValuePath(kDownloadSwitch);
  uint64_t browser = 0;
  if (!base::StringToUint64(command_line.GetSwitchValueASCII(kAfterSwitch),
                            &browser)) {
    return std::nullopt;
  }
  request.browser = static_cast<base::ProcessId>(browser);
  request.relaunch = command_line.HasSwitch(kRelaunchSwitch);
  request.in_front = command_line.HasSwitch(kFrontSwitch);
  for (const std::wstring& argument : command_line.GetArgs()) {
    if (request.arguments.empty() && argument == kSwitchTerminator) {
      continue;
    }
    request.arguments.push_back(argument);
  }
  // Only Pane's own layout: the app folder an install lays out, and a
  // version staged in the update folder beside it, never a folder a command
  // line names elsewhere.
  const auto plain = [](const base::FilePath& path) {
    return path.IsAbsolute() && !path.ReferencesParent();
  };
  if (!plain(request.app) || !IsInstallLayout(request.app) ||
      !MaySwap(request.app) || !plain(request.staged) ||
      !SamePath(request.staged.DirName(), UpdateFolderOf(request.app)) ||
      !IsStagedVersion(request.staged.BaseName().AsUTF8Unsafe()) ||
      (!request.download.empty() && !plain(request.download))) {
    return std::nullopt;
  }
  return request;
}

base::CommandLine SwapCommandLine(const base::FilePath& helper,
                                  const SwapRequest& request) {
  base::CommandLine command(helper);
  command.AppendSwitch(kSwapSwitch);
  command.AppendSwitchPath(kAppSwitch, request.app);
  command.AppendSwitchPath(kStagedSwitch, request.staged);
  if (!request.download.empty()) {
    command.AppendSwitchPath(kDownloadSwitch, request.download);
  }
  command.AppendSwitchASCII(kAfterSwitch,
                            base::NumberToString(request.browser));
  if (request.relaunch) {
    command.AppendSwitch(kRelaunchSwitch);
  }
  if (request.in_front) {
    command.AppendSwitch(kFrontSwitch);
  }
  // Pane's own arguments after the terminator, so none reads as the
  // helper's.
  if (!request.arguments.empty()) {
    command.AppendArgNative(std::wstring(kSwitchTerminator));
    for (const std::wstring& argument : request.arguments) {
      command.AppendArgNative(argument);
    }
  }
  return command;
}

PaneProcesses::PaneProcesses(const base::FilePath& folder) : folder_(folder) {
  for (base::ProcessId pid : ProcessesFrom(folder)) {
    base::Process process = base::Process::OpenWithAccess(
        pid,
        SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION);
    if (process.IsValid()) {
      processes_.push_back(std::move(process));
    }
  }
}

PaneProcesses::~PaneProcesses() = default;

bool PaneProcesses::Contains(base::ProcessId pid) const {
  return std::ranges::any_of(processes_, [pid](const base::Process& process) {
    return process.Pid() == pid;
  });
}

bool PaneProcesses::WaitForExitOf(base::ProcessId pid,
                                  base::TimeDelta timeout) const {
  for (const base::Process& process : processes_) {
    if (process.Pid() == pid) {
      return process.WaitForExitWithTimeout(timeout, nullptr);
    }
  }
  return true;
}

bool PaneProcesses::WaitForExit(base::TimeDelta timeout) const {
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  for (const base::Process& process : processes_) {
    if (!process.WaitForExitWithTimeout(
            std::max(deadline - base::TimeTicks::Now(), base::TimeDelta()),
            nullptr)) {
      return false;
    }
  }
  return true;
}

void PaneProcesses::End() const {
  for (const base::Process& process : processes_) {
    if (!process.WaitForExitWithTimeout(base::TimeDelta(), nullptr)) {
      process.Terminate(/*exit_code=*/1, /*wait=*/false);
    }
  }
}

void PaneProcesses::Adopt() {
  // Each process's parent, now.
  std::map<base::ProcessId, base::ProcessId> parents;
  base::win::ScopedHandle snapshot(
      ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.is_valid()) {
    return;
  }
  PROCESSENTRY32W entry = {.dwSize = sizeof(entry)};
  for (BOOL more = ::Process32FirstW(snapshot.get(), &entry); more;
       more = ::Process32NextW(snapshot.get(), &entry)) {
    parents[entry.th32ProcessID] = entry.th32ParentProcessID;
  }
  // Each one held keeps its id, so a parent's names one of them alone.
  for (bool grew = true; grew;) {
    grew = false;
    for (base::ProcessId pid : ProcessesFrom(folder_)) {
      const auto parent = parents.find(pid);
      if (Contains(pid) || parent == parents.end() ||
          !Contains(parent->second)) {
        continue;
      }
      base::Process process = base::Process::OpenWithAccess(
          pid,
          SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION);
      if (process.IsValid()) {
        processes_.push_back(std::move(process));
        grew = true;
      }
    }
  }
}

bool PaneProcesses::OthersRun() const {
  return std::ranges::any_of(
      ProcessesFrom(folder_),
      [this](base::ProcessId pid) { return !Contains(pid); });
}

bool PaneProcesses::AnotherRuns() const {
  return OthersRun() || !PanesFrom(folder_).empty();
}

std::unique_ptr<WorkItemList> SwapIn(const base::FilePath& app,
                                     const base::FilePath& staged,
                                     const base::FilePath& swapping) {
  if (!base::CreateDirectory(swapping)) {
    Note("could not make " + swapping.AsUTF8Unsafe());
    return nullptr;
  }
  // Chromium's MoveTreeWorkItem: the app folder moves aside into a folder
  // of its own under `swapping`, then the staged one into its place; its
  // rollback moves each back. A folder held open moves file by file, and
  // a file held without FILE_SHARE_DELETE fails it.
  std::unique_ptr<WorkItemList> list(WorkItem::CreateWorkItemList());
  list->AddMoveTreeWorkItem(staged, app, swapping, {});
  if (list->Do()) {
    return list;
  }
  const DWORD error = ::GetLastError();
  Note("the swap failed: " + base::NumberToString(error) + "; rolling back");
  list->Rollback();
  list.reset();
  ::RemoveDirectoryW(swapping.value().c_str());
  return nullptr;
}

SwapOutcome RunSwap(const SwapRequest& request) {
  // Checked again, whatever made the request.
  if (!MaySwap(request.app)) {
    return SwapOutcome::kRefused;
  }
  const base::FilePath update = UpdateFolderOf(request.app);
  Note("swapping " + request.staged.AsUTF8Unsafe() + " in for " +
       request.app.AsUTF8Unsafe());
  const base::TimeTicks started = base::TimeTicks::Now();
  const auto elapsed = [&started] {
    return base::NumberToString(
               (base::TimeTicks::Now() - started).InMilliseconds()) +
           " ms";
  };

  // Pane's processes as it exits, before any later launch: its browser among
  // them, unless it has gone already.
  PaneProcesses pane(request.app);
  if (!pane.WaitForExitOf(request.browser, kBrowserExits)) {
    Note("Pane didn't exit");
    base::WriteFile(NotInstalledMark(update), Notes());
    return SwapOutcome::kNotInstalled;
  }
  Note("Pane's browser gone at " + elapsed());
  pane.Adopt();
  const bool followed = pane.WaitForExit(kChildrenFollow);

  // Windows is ending the session: the next exit installs it.
  if (::GetSystemMetrics(SM_SHUTTINGDOWN)) {
    Note("Windows is ending the session: not now");
    return SwapOutcome::kDeferred;
  }
  // Another Pane runs from the folder, opened again or on data of its own:
  // nothing of Pane's is ended, and the next exit installs it. Restart to
  // update opens Pane as it was, which says why (11.12.14, E).
  const auto not_now = [&request, &update] {
    Note("another Pane runs: not now");
    base::WriteFile(NotInstalledMark(update), Notes());
    if (request.relaunch) {
      OpenPane(request.app, request);
    }
    return SwapOutcome::kDeferred;
  };
  if (pane.AnotherRuns()) {
    return not_now();
  }
  if (!followed) {
    Note("ending what outlived it");
    pane.End();
    if (!pane.WaitForExit(kEndedGo)) {
      Note("what outlived it didn't end");
    }
  }
  Note("Pane gone at " + elapsed());
  if (pane.AnotherRuns()) {
    return not_now();
  }

  std::unique_ptr<WorkItemList> swapped =
      SwapIn(request.app, request.staged, SwappingFolder(update));
  if (!swapped) {
    // As it was: the old Pane opens again for Restart to update, and the
    // next launch says why (11.12.14, E).
    base::WriteFile(NotInstalledMark(update), Notes());
    if (request.relaunch) {
      OpenPane(request.app, request);
    }
    return SwapOutcome::kNotInstalled;
  }
  Note("swapped at " + elapsed());
  if (request.relaunch) {
    OpenPane(request.app, request);
  }

  // Then what it leaves: the old app, the installer and its record, the
  // mark of a swap that failed before, and the update folder where nothing
  // else is in it. The old app's files go as the list does, and what is still
  // held then (Windows Defender reading a file a moment, as W7b's installer
  // found) goes in the tries after; what outlasts them, the next launch
  // removes.
  swapped.reset();
  const base::FilePath swapping = SwappingFolder(update);
  for (int attempt = 0; base::PathExists(swapping); ++attempt) {
    if (base::DeletePathRecursively(swapping)) {
      break;
    }
    if (attempt == kRemoveTries) {
      Note("the old app's folder is still held");
      break;
    }
    ::Sleep(kRemoveInterval.InMilliseconds());
  }
  Note("the old app gone at " + elapsed());
  const std::string version = request.staged.BaseName().AsUTF8Unsafe();
  if (!request.download.empty()) {
    base::DeleteFile(request.download);
  }
  base::DeleteFile(DownloadRecord(update, version));
  base::DeleteFile(NotInstalledMark(update));
  ::RemoveDirectoryW(update.value().c_str());
  Note("done at " + elapsed());
  return SwapOutcome::kInstalled;
}

}  // namespace pane::installer
