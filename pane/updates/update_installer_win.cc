// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_installer.h"

#include <windows.h>

#include <utility>

#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "pane/app/test_copy.h"
#include "pane/installer/files.h"
#include "pane/installer/swap.h"
#include "pane/installer/switches.h"
#include "pane/installer/update_folder.h"
#include "pane/model/test_hooks.h"

namespace pane {

namespace {

// What a scenario sets for its copy to install its update
// (PANE_UPDATE_INSTALL=1): only a copy laid out in the temporary folder
// takes it, never the user's installed Pane, which the test VM's runs start
// their test copies from.
constexpr char kScenarioInstalls[] = "1";

bool IsTestCopy() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain");
}

// The helper's copies an earlier exit left in the temporary folder, where
// their own deletion didn't run, and nothing else: each a folder of its own,
// named by CopyHelperOut, holding the helper alone. One that still runs
// stays.
void RemoveHelperCopies() {
  base::FilePath temp;
  if (!base::GetTempDir(&temp)) {
    return;
  }
  base::FileEnumerator copies(
      temp, /*recursive=*/false, base::FileEnumerator::DIRECTORIES,
      base::StrCat({installer::kUpdateCopyPrefix, FILE_PATH_LITERAL("*")}));
  for (base::FilePath path = copies.Next(); !path.empty();
       path = copies.Next()) {
    if (installer::IsHelperCopy(path, installer::kUpdateCopyPrefix)) {
      base::DeletePathRecursively(path);
    }
  }
}

}  // namespace

// static
std::optional<UpdateInstaller> UpdateInstaller::ForRunningApp() {
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe)) {
    return std::nullopt;
  }
  const base::FilePath app = exe.DirName();
  if (!installer::IsInstallLayout(app)) {
    return std::nullopt;
  }
  if (IsTestCopy()) {
    if (model::TestHook("PANE_UPDATE_INSTALL") != kScenarioInstalls ||
        !InTemporaryFolder(app)) {
      return std::nullopt;
    }
  } else {
    // The installed folder alone, %LOCALAPPDATA%\Programs\Pane\Application:
    // a Pane run from elsewhere leaves the install to its installer.
    const base::FilePath installed = installer::AppFolder();
    if (installed.empty() || !installer::IsInFolder(app, installed) ||
        !installer::IsInFolder(installed, app)) {
      return std::nullopt;
    }
  }
  // Whether this user can write the install's folder shows when it stages:
  // nothing more is read at launch.
  return UpdateInstaller(app, std::string());
}

std::optional<base::FilePath> UpdateInstaller::Stage(const base::FilePath& file,
                                                     const std::string& version,
                                                     bool verified) const {
  // Only an installer the release's digest vouched for: one that wasn't keeps
  // its Mark of the Web, and runs at Exit and install, where Windows asks
  // first (Paper 11.12.11).
  if (!verified) {
    LOG(ERROR) << "The update's installer had no digest to check";
    return std::nullopt;
  }
  // Its payload, read where it is: the downloaded program never runs, so no
  // SmartScreen and no UAC. It matched the release's SHA-256, its payload
  // with it, so the payload's own digest isn't read again.
  return installer::StageUpdate(file, version, installer::UpdateFolderOf(app_));
}

bool UpdateInstaller::Install(const base::FilePath& staged,
                              const base::FilePath& file,
                              const std::optional<Relaunch>& relaunch) const {
  // Not as Windows signs the user out, as Chromium's own swap waits then
  // (browser_shutdown.cc, kEndSession): the staged copy stays, and the next
  // exit installs it.
  if (browser_shutdown::GetShutdownType() ==
      browser_shutdown::ShutdownType::kEndSession) {
    return true;
  }
  // The new version's helper, out of the folder it renames.
  const std::optional<base::FilePath> helper = installer::CopyHelperOut(
      staged.Append(installer::kHelperName), installer::kUpdateCopyPrefix);
  if (!helper) {
    PLOG(ERROR) << "Could not copy the update's helper";
    return false;
  }

  installer::SwapRequest request;
  request.app = app_;
  request.staged = staged;
  request.download = file;
  request.browser = base::GetCurrentProcId();
  if (relaunch) {
    request.relaunch = true;
    request.in_front = relaunch->in_front;
    for (const std::string& value : relaunch->switches) {
      request.arguments.push_back(base::ASCIIToWide(value));
    }
    // A scenario's copy names its engine's folder too, which chrome_elf
    // reads from the command line alone (pane/app/data_launch_win.cc).
    const base::FilePath engine =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "user-data-dir");
    if (IsTestCopy() && !engine.empty()) {
      request.arguments.push_back(base::StrCat(
          {FILE_PATH_LITERAL("--user-data-dir="), engine.value()}));
    }
  }

  base::FilePath temp;
  base::GetTempDir(&temp);
  base::LaunchOptions options;
  // Never in the app folder or the update folder, which it renames.
  options.current_directory = temp;
  // Out of the browser's job, which may end with it.
  options.force_breakaway_from_job_ = true;
  // The front, which it hands the new Pane.
  options.grant_foreground_privilege = relaunch && relaunch->in_front;
  // A plain exit opens nothing: no app-starting pointer.
  options.feedback_cursor_off = !relaunch;
  options.start_hidden = true;
  const base::CommandLine command =
      installer::SwapCommandLine(*helper, request);
  base::Process started = base::LaunchProcess(command, options);
  if (!started.IsValid()) {
    // A job that lets nothing leave it refuses the breakaway: a terminal's,
    // a scheduler's, or one the helper itself runs in, as the Pane it opened
    // after Restart to update does (W7c's runs). The helper then stays in
    // it, which outlives Pane: the job ends only with the handles of
    // whatever made it.
    options.force_breakaway_from_job_ = false;
    started = base::LaunchProcess(command, options);
  }
  if (!started.IsValid()) {
    PLOG(ERROR) << "The update's helper did not start";
    base::DeletePathRecursively(helper->DirName());
    return false;
  }
  return true;
}

std::optional<UpdateInstaller::Kept> UpdateInstaller::RemoveLeftovers(
    std::string_view running_version) const {
  RemoveHelperCopies();
  const std::optional<installer::KeptUpdate> left =
      installer::CleanUpdateFolder(installer::UpdateFolderOf(app_),
                                   base::Version(running_version));
  if (!left) {
    return std::nullopt;
  }
  Kept kept;
  kept.version = left->version;
  kept.staged = left->staged;
  kept.file = left->download;
  kept.not_installed = left->not_installed;
  return kept;
}

void UpdateInstaller::Unstage(const base::FilePath& staged,
                              const base::FilePath& file) const {
  installer::UnstageUpdate(staged, file);
}

}  // namespace pane
