// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/flow.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "pane/installer/chrome_exe.h"
#include "pane/installer/files.h"
#include "pane/installer/running_pane.h"
#include "pane/installer/switches.h"
#include "pane/installer/update_folder.h"

namespace pane::installer {

namespace {

// The buttons the screens show.
enum ButtonId {
  kClose = 1,
  kCancel,
  kTryAgain,
  kOpenPane,
  kExitPane,
  kUninstall,
  kUninstallAndDelete,
};

// How long Pane has to paint its first window before the install says it
// didn't open (Paper 11.12.6, "past 20 s").
constexpr DWORD kOpenMilliseconds = 20000;
// How often "Pane is open" looks again.
constexpr base::TimeDelta kWatchInterval = base::Milliseconds(250);
// The share of the copy's bar that reading the payload takes, the rest
// writing its files out. Each file is its own block, so the reading leads
// the writing by a file at most: the bar moves from the first page read to
// the last file written.
constexpr double kReadShare = 0.8;

base::Lock& NotesLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

std::vector<std::string>& Notes() {
  static base::NoDestructor<std::vector<std::string>> notes;
  return *notes;
}

// 8 random hex digits, for a folder or an event of this run's.
std::wstring RandomTag() {
  return base::ASCIIToWide(base::HexEncode(base::RandBytesAsVector(4)));
}

Screen::Button Outline(int id, std::u16string label, std::u16string key) {
  return {id, std::move(label), Screen::ButtonKind::kOutline, std::move(key)};
}

Screen::Button Primary(int id, std::u16string label) {
  return {id, std::move(label), Screen::ButtonKind::kPrimary, u"Enter"};
}

// Pane's own two buttons, Close or Cancel with Esc and the default with
// Enter (11.13 A).
void TwoButtons(Screen& screen,
                int cancel,
                std::u16string cancel_label,
                int action,
                std::u16string action_label) {
  screen.buttons = {Outline(cancel, std::move(cancel_label), u"Esc"),
                    Primary(action, std::move(action_label))};
  screen.escape = cancel;
  screen.enter = action;
}

std::u16string VersionText(const base::Version& version) {
  return base::UTF8ToUTF16(version.IsValid() ? version.GetString() : "");
}

// A folder renamed, as an install renames its own into place.
DWORD Rename(const base::FilePath& from, const base::FilePath& to) {
  if (::MoveFileExW(from.value().c_str(), to.value().c_str(), 0)) {
    return ERROR_SUCCESS;
  }
  const DWORD error = ::GetLastError();
  Note(base::StringPrintf("rename %s failed: %lu", from.AsUTF8Unsafe().c_str(),
                          error));
  return error;
}

}  // namespace

void Note(const std::string& line) {
  LOG(WARNING) << line;
  base::AutoLock lock(NotesLock());
  const base::Time::Exploded now = [] {
    base::Time::Exploded exploded;
    base::Time::Now().LocalExplode(&exploded);
    return exploded;
  }();
  Notes().push_back(base::StringPrintf("%02d:%02d:%02d.%03d %s", now.hour,
                                       now.minute, now.second, now.millisecond,
                                       line.c_str()));
}

void WriteNotes(const base::FilePath& path) {
  std::string text;
  {
    base::AutoLock lock(NotesLock());
    for (const std::string& line : Notes()) {
      text += line + "\r\n";
    }
  }
  if (!base::WriteFile(path, text)) {
    base::FilePath temp;
    if (base::GetTempDir(&temp)) {
      base::WriteFile(temp.Append(path.BaseName()), text);
    }
  }
}

std::u16string CodeText(DWORD error) {
  // An exit code that is already an HRESULT stays one.
  const uint32_t code = (error & 0x80000000u)
                            ? error
                            : static_cast<uint32_t>(HRESULT_FROM_WIN32(error));
  return base::UTF8ToUTF16(base::StringPrintf("0x%08X", code));
}

// MARK: The install

InstallFlow::InstallFlow(base::FilePath installer,
                         std::optional<Payload> payload)
    : installer_(std::move(installer)),
      payload_(std::move(payload)),
      version_(payload_ ? base::Version(payload_->version) : base::Version()),
      // What Taskbar and Alt+Tab name it: BRANDING's
      // PRODUCT_INSTALLER_FULLNAME.
      window_(this, L"Pane Installer"),
      worker_("PaneInstall") {
  worker_.StartWithOptions(base::Thread::Options());
}

InstallFlow::~InstallFlow() {
  cancel_ = true;
  worker_.Stop();
}

int InstallFlow::Run() {
  Note(base::StrCat({"installer ", installer_.AsUTF8Unsafe(), ", Pane ",
                     payload_ ? payload_->version : "(no payload)"}));
  if (!payload_ || !version_.IsValid()) {
    // A download cut short, or a program with no payload: nothing to copy.
    Fail(ERROR_BAD_FORMAT,
         u"Something went wrong while copying its files ({code}). Nothing "
         u"was changed. Try again, or download the installer again.");
  } else {
    Check();
  }
  window_.Run();
  return exit_code_;
}

void InstallFlow::Check() {
  const base::FilePath app = AppFolder();
  installed_ = VersionIn(app);
  if (installed_ && *installed_ > version_) {
    Note("a newer Pane is installed: " + installed_->GetString());
    ShowStep(Step::kNewer);
    return;
  }
  if (!ProcessesFrom(app).empty()) {
    Note("Pane is open");
    ShowStep(Step::kOpen);
    WatchUntilGone();
    return;
  }
  free_ = FreeSpace(InstallFolder()).value_or(UINT64_MAX);
  if (free_ < SpaceNeeded(payload_->unpacked_size)) {
    Note(base::StringPrintf("not enough space: %llu free", free_));
    ShowStep(Step::kSpace);
    return;
  }
  progress_.read = 0;
  progress_.written = 0;
  cancel_ = false;
  ShowStep(Step::kCopying);
  worker_.task_runner()->PostTask(
      FROM_HERE,
      // The flow lives until the process ends, past the worker it stops.
      base::BindOnce(&InstallFlow::Copy, base::Unretained(this)));
}

void InstallFlow::ShowStep(Step step) {
  step_ = step;
  Screen screen;
  switch (step) {
    case Step::kChecking:
      return;
    case Step::kCopying:
    case Step::kAdding:
    case Step::kOpening:
      screen.title = u"Installing Pane";
      screen.message = u"It opens as soon as it’s ready.";
      screen.bar = Screen::Bar{
          step == Step::kCopying ? u"Copying Pane’s files"
          : step == Step::kAdding
              ? u"Adding Pane to Start, the desktop, and Default apps"
              : u"Opening Pane",
          step == Step::kCopying ? u"0%" : u""};
      break;
    case Step::kNewer:
      screen.title = u"A newer Pane is installed";
      screen.message =
          base::StrCat({u"This PC has Pane ", VersionText(*installed_),
                        u", newer than this installer’s ",
                        VersionText(version_), u". Pane keeps the newer one."});
      TwoButtons(screen, kClose, u"Close", kOpenPane, u"Open Pane");
      break;
    case Step::kOpen:
      screen.title = u"Pane is open";
      screen.message =
          u"Exit Pane, and the install goes on by itself once it has gone.";
      TwoButtons(screen, kCancel, u"Cancel", kExitPane, u"Exit Pane");
      break;
    case Step::kSpace:
      screen.title = u"Pane needs more space";
      screen.message = base::StrCat(
          {u"It takes ", SizeText(SpaceNeeded(payload_->unpacked_size)),
           u" on ", DriveOf(InstallFolder()), u", which has ", SizeText(free_),
           u" free. Free up some space, then try again."});
      TwoButtons(screen, kClose, u"Close", kTryAgain, u"Try again");
      break;
    case Step::kBlocked:
      screen.title = u"One of Pane’s files was blocked";
      screen.message = base::StrCat(
          {u"Windows Security or your antivirus kept Pane from writing ",
           blocked_file_, u". Allow it there, then try again."});
      TwoButtons(screen, kClose, u"Close", kTryAgain, u"Try again");
      break;
    case Step::kFailed:
      screen.title = u"Pane couldn’t be installed";
      screen.message = failure_message_;
      TwoButtons(screen, kClose, u"Close", kTryAgain, u"Try again");
      break;
  }
  window_.Show(screen);
}

void InstallFlow::Fail(DWORD error, std::u16string what) {
  Note(base::StringPrintf("failed: %lu", error));
  std::u16string message = std::move(what);
  base::ReplaceFirstSubstringAfterOffset(&message, 0, u"{code}",
                                         CodeText(error));
  failure_message_ = std::move(message);
  ShowStep(Step::kFailed);
  // The log beside the installer names the step (11.12.8).
  WriteNotes(installer_.ReplaceExtension(L".log"));
}

double InstallFlow::BarAt(std::u16string& right) {
  if (step_ != Step::kCopying) {
    return 1;
  }
  const double read =
      payload_->size ? static_cast<double>(progress_.read) / payload_->size : 0;
  const double written =
      payload_->unpacked_size
          ? static_cast<double>(progress_.written) / payload_->unpacked_size
          : 0;
  const double fraction =
      std::clamp(kReadShare * std::min(read, 1.0) +
                     (1 - kReadShare) * std::min(written, 1.0),
                 0.0, 1.0);
  right = base::StrCat(
      {base::NumberToString16(static_cast<int>(std::floor(fraction * 100))),
       u"%"});
  return fraction;
}

void InstallFlow::OnButton(int id) {
  switch (id) {
    case kClose:
    case kCancel:
      exit_code_ = 1;
      window_.Close();
      return;
    case kTryAgain:
      // Pane's files are in place and registered: it only has to open.
      if (std::exchange(open_failed_, false)) {
        ShowStep(Step::kOpening);
        worker_.task_runner()->PostTask(
            FROM_HERE,
            base::BindOnce(&InstallFlow::Launch, base::Unretained(this)));
        return;
      }
      Check();
      return;
    case kOpenPane: {
      base::LaunchOptions options;
      options.current_directory = UserFolder();
      options.grant_foreground_privilege = true;
      base::LaunchProcess(base::CommandLine(ChromeIn(AppFolder())), options);
      exit_code_ = 0;
      window_.Close();
      return;
    }
    case kExitPane:
      // Off the window's thread: a busy Pane may take a moment to answer.
      worker_.task_runner()->PostTask(FROM_HERE, base::BindOnce([] {
                                        const base::FilePath app = AppFolder();
                                        const std::vector<HWND> panes =
                                            PanesFrom(app);
                                        if (panes.empty()) {
                                          // What outlived its browser.
                                          EndProcesses(ProcessesFrom(app));
                                        }
                                        for (HWND pane : panes) {
                                          AskToExit(pane);
                                        }
                                      }));
      return;
  }
}

bool InstallFlow::OnCloseAsked() {
  switch (step_) {
    case Step::kCopying:
      // Stops and removes what it wrote (11.12.6): the window goes at once,
      // and the process once its copy has let go.
      cancel_ = true;
      close_when_done_ = true;
      ::ShowWindow(window_.hwnd(), SW_HIDE);
      return false;
    case Step::kAdding:
    case Step::kOpening:
      // Installed, or nearly: the window goes, and Pane still opens.
      close_when_done_ = true;
      ::ShowWindow(window_.hwnd(), SW_HIDE);
      return false;
    default:
      exit_code_ = 1;
      return true;
  }
}

void InstallFlow::WatchUntilGone() {
  worker_.task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](InstallFlow* flow) {
            if (!ProcessesFrom(AppFolder()).empty()) {
              flow->WatchUntilGone();
              return;
            }
            flow->window_.Post(base::BindOnce(
                [](InstallFlow* flow) {
                  if (flow->step_ == Step::kOpen) {
                    flow->Check();
                  }
                },
                flow));
          },
          base::Unretained(this)),
      kWatchInterval);
}

void InstallFlow::Copy() {
  const base::FilePath staging =
      InstallFolder().Append(base::StrCat({L"Installing-", RandomTag()}));
  if (!base::CreateDirectory(staging)) {
    const DWORD error = ::GetLastError();
    window_.Post(base::BindOnce(&InstallFlow::Copied, base::Unretained(this),
                                UnpackResult{.error = error}, true,
                                std::optional<base::FilePath>(), false,
                                static_cast<DWORD>(ERROR_SUCCESS)));
    return;
  }
  Note("unpacking into " + staging.AsUTF8Unsafe());
  // The archive's digest, checked beside the unpack, on a thread of its own.
  base::WaitableEvent hashed;
  bool intact = false;
  base::Thread hasher("PaneHash");
  hasher.Start();
  hasher.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](const base::FilePath& installer, const Payload& payload,
                        bool* intact, base::WaitableEvent* hashed) {
                       *intact = PayloadIsIntact(installer, payload);
                       hashed->Signal();
                     },
                     installer_, *payload_, &intact, &hashed));
  const base::TimeTicks start = base::TimeTicks::Now();
  UnpackResult result =
      Unpack(installer_, *payload_, staging, &progress_, cancel_);
  Note(base::StringPrintf("unpacked in %lld ms: outcome %d, error %lu",
                          (base::TimeTicks::Now() - start).InMilliseconds(),
                          static_cast<int>(result.outcome), result.error));
  hashed.Wait();
  hasher.Stop();
  if (result.outcome == UnpackResult::Outcome::kDone && !intact) {
    Note("the payload's digest doesn't match");
    result = {.outcome = UnpackResult::Outcome::kFailed, .error = ERROR_CRC};
  }
  if (result.outcome == UnpackResult::Outcome::kDone &&
      (VersionIn(staging) != version_ ||
       !base::PathExists(ChromeIn(staging)))) {
    Note("the payload's folder isn't Pane's");
    result = {.outcome = UnpackResult::Outcome::kFailed,
              .error = ERROR_FILE_CORRUPT};
  }
  if (result.outcome != UnpackResult::Outcome::kDone) {
    // Nothing half-done: whatever was copied goes (11.12.7).
    RemoveFolder(staging);
    RemoveEmptyFolders();
    window_.Post(base::BindOnce(&InstallFlow::Copied, base::Unretained(this),
                                result, intact, std::optional<base::FilePath>(),
                                false, static_cast<DWORD>(ERROR_SUCCESS)));
    return;
  }
  // Renamed into place only once whole.
  const base::FilePath app = AppFolder();
  const bool first_install = !base::DirectoryExists(app);
  std::optional<base::FilePath> old_app;
  if (!first_install) {
    old_app = InstallFolder().Append(base::StrCat({L"Removing-", RandomTag()}));
    if (const DWORD error = Rename(app, *old_app)) {
      RemoveFolder(staging);
      RemoveEmptyFolders();
      window_.Post(base::BindOnce(
          &InstallFlow::Copied, base::Unretained(this), result, intact,
          std::optional<base::FilePath>(), first_install, error));
      return;
    }
  }
  if (const DWORD error = Rename(staging, app)) {
    if (old_app) {
      Rename(*old_app, app);
    }
    RemoveFolder(staging);
    RemoveEmptyFolders();
    window_.Post(base::BindOnce(&InstallFlow::Copied, base::Unretained(this),
                                result, intact, std::optional<base::FilePath>(),
                                first_install, error));
    return;
  }
  window_.Post(base::BindOnce(&InstallFlow::Copied, base::Unretained(this),
                              result, intact, old_app, first_install,
                              static_cast<DWORD>(ERROR_SUCCESS)));
}

void InstallFlow::Copied(UnpackResult result,
                         bool intact,
                         std::optional<base::FilePath> old_app,
                         bool first_install,
                         DWORD swap_error) {
  if (result.outcome == UnpackResult::Outcome::kCancelled) {
    exit_code_ = 1;
    window_.Close();
    return;
  }
  if (result.outcome == UnpackResult::Outcome::kBlocked) {
    blocked_file_ = result.file.BaseName().AsUTF16Unsafe();
    ShowStep(Step::kBlocked);
    WriteNotes(installer_.ReplaceExtension(L".log"));
    return;
  }
  const DWORD error = result.outcome == UnpackResult::Outcome::kFailed
                          ? result.error
                          : swap_error;
  if (error) {
    Fail(error,
         u"Something went wrong while copying its files ({code}). Nothing "
         u"was changed. Try again, or download the installer again.");
    return;
  }
  if (close_when_done_) {
    // Closed as the copy ended: Close stops the install, so nothing stays.
    worker_.task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](InstallFlow* flow, std::optional<base::FilePath> old_app) {
              const base::FilePath app = AppFolder();
              RemoveFolder(app);
              if (old_app) {
                Rename(*old_app, app);
              }
              RemoveEmptyFolders();
              flow->window_.Post(base::BindOnce(
                  [](InstallFlow* flow) { flow->window_.Close(); }, flow));
            },
            base::Unretained(this), old_app));
    return;
  }
  ShowStep(Step::kAdding);
  worker_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&InstallFlow::Register, base::Unretained(this),
                     first_install, old_app.value_or(base::FilePath())));
}

void InstallFlow::Register(bool first_install, base::FilePath old_app) {
  const base::FilePath app = AppFolder();
  const std::optional<int> code = RegisterWithWindows(app, first_install, Note);
  if (code != kSwitchDone) {
    // What it registered goes, and the PC is as it was: the earlier folder
    // back in place, or the new one gone with its registrations.
    if (!old_app.empty()) {
      const base::FilePath failed =
          InstallFolder().Append(base::StrCat({L"Failed-", RandomTag()}));
      if (!Rename(app, failed)) {
        Rename(old_app, app);
        RemoveFolder(failed);
      }
    } else {
      RunChrome(app, {base::StrCat({kUnregisterSwitch, "=", kUnregisterKeep})},
                kRegisterTimeout, Note);
      RemoveFolder(app);
      RemoveEmptyFolders();
    }
    window_.Post(base::BindOnce(
        &InstallFlow::Registered, base::Unretained(this),
        code ? static_cast<DWORD>(*code) : static_cast<DWORD>(WAIT_TIMEOUT)));
    return;
  }
  if (!old_app.empty()) {
    RemoveFolder(old_app);
  }
  window_.Post(base::BindOnce(&InstallFlow::Registered, base::Unretained(this),
                              static_cast<DWORD>(ERROR_SUCCESS)));
}

void InstallFlow::Registered(DWORD error) {
  if (error) {
    Fail(error,
         u"Something went wrong while adding Pane to Windows ({code}). "
         u"Nothing was changed. Try again, or download the installer again.");
    return;
  }
  ShowStep(Step::kOpening);
  worker_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&InstallFlow::Launch, base::Unretained(this)));
}

void InstallFlow::Launch() {
  const std::wstring event_name =
      base::StrCat({kInstalledEventPrefix, RandomTag()});
  base::win::ScopedHandle painted(
      ::CreateEventW(nullptr, TRUE, FALSE, event_name.c_str()));
  base::CommandLine command(ChromeIn(AppFolder()));
  command.AppendSwitchNative(kInstalledSwitch, event_name);
  base::LaunchOptions options;
  options.current_directory = UserFolder();
  options.grant_foreground_privilege = true;
  const base::TimeTicks start = base::TimeTicks::Now();
  base::Process pane = base::LaunchProcess(command, options);
  if (!pane.IsValid() || !painted.is_valid()) {
    const DWORD error = ::GetLastError();
    window_.Post(base::BindOnce(&InstallFlow::Opened, base::Unretained(this),
                                false, error ? error : ERROR_GEN_FAILURE));
    return;
  }
  const std::array<HANDLE, 2> waits = {painted.get(), pane.Handle()};
  const DWORD waited = ::WaitForMultipleObjects(
      static_cast<DWORD>(waits.size()), waits.data(), FALSE, kOpenMilliseconds);
  Note(base::StringPrintf("Pane's first window: %lu after %lld ms", waited,
                          (base::TimeTicks::Now() - start).InMilliseconds()));
  if (waited == WAIT_OBJECT_0) {
    window_.Post(base::BindOnce(&InstallFlow::Opened, base::Unretained(this),
                                true, static_cast<DWORD>(ERROR_SUCCESS)));
    return;
  }
  DWORD code = ERROR_TIMEOUT;
  if (waited == WAIT_OBJECT_0 + 1) {
    int exit = 0;
    pane.WaitForExitWithTimeout(base::TimeDelta(), &exit);
    // A Pane already open took the launch, as a second launch hands its
    // command line over, and shows its window.
    if (exit == 0 && !PanesFrom(AppFolder()).empty()) {
      Note("Pane was open: the launch went to it");
      window_.Post(base::BindOnce(&InstallFlow::Opened, base::Unretained(this),
                                  true, static_cast<DWORD>(ERROR_SUCCESS)));
      return;
    }
    code = exit ? static_cast<DWORD>(exit) : ERROR_PROCESS_ABORTED;
  }
  window_.Post(base::BindOnce(&InstallFlow::Opened, base::Unretained(this),
                              false, code));
}

void InstallFlow::Opened(bool painted, DWORD error) {
  if (painted || close_when_done_) {
    // It closes in that frame (11.12.6).
    exit_code_ = 0;
    window_.Close();
    return;
  }
  // Pane is installed; Start opens it (11.12.6, "If Pane doesn't open"),
  // and Try again opens it again.
  open_failed_ = true;
  Fail(error,
       u"Pane’s files are in place, but it didn’t open ({code}). "
       u"Open it from Start, or try again.");
}

// MARK: The uninstall

UninstallFlow::UninstallFlow(base::FilePath app_folder, base::ProcessId after)
    : app_folder_(std::move(app_folder)),
      after_(after),
      // The name 11.12.9 draws in the taskbar.
      window_(this, L"Uninstall Pane"),
      worker_("PaneUninstall") {
  worker_.StartWithOptions(base::Thread::Options());
}

UninstallFlow::~UninstallFlow() {
  worker_.Stop();
}

int UninstallFlow::Run() {
  Start();
  window_.Run();
  return exit_code_;
}

void UninstallFlow::Start() {
  // The chrome.exe that started this copy, on its way out.
  if (after_ != base::kNullProcessId) {
    WaitForExit({after_}, base::Seconds(10));
  }
  Note("uninstalling " + app_folder_.AsUTF8Unsafe());
  // Only Pane's own folder: never one a command line names elsewhere.
  if (!IsInstalledAppFolder(app_folder_) ||
      !base::PathExists(ChromeIn(app_folder_))) {
    NotInstalledHere();
    return;
  }
  has_data_ = base::DirectoryExists(DataFolder());
  ShowStep(Step::kAsking);
  if (has_data_) {
    // Counted as the dialog opens (11.12.9).
    worker_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](UninstallFlow* flow) {
                         const uint64_t size = FolderSize(DataFolder());
                         flow->data_size_ = size;
                         flow->window_.Post(base::BindOnce(
                             [](UninstallFlow* flow, uint64_t size) {
                               flow->window_.SetBoxSize(SizeText(size));
                             },
                             flow, size));
                       },
                       base::Unretained(this)));
  }
}

void UninstallFlow::ShowStep(Step step) {
  step_ = step;
  Screen screen;
  switch (step) {
    case Step::kAsking:
      screen.title = u"Uninstall Pane?";
      screen.message =
          u"Pane leaves this PC: the app, its Start and desktop shortcuts, "
          u"and its place in Default apps.";
      if (has_data_) {
        screen.box = Screen::Box{
            erase_, u"Also delete Pane’s data",
            data_size_ ? SizeText(data_size_) : u"",
            erase_ ? u"Erased for good once you press Uninstall and delete: "
                     u"nothing can bring it back."
                   : u"Your profiles, sign-ins, tabs, extensions, and PIN. "
                     u"Kept, a reinstall finds them."};
      }
      if (erase_) {
        // Red, with no key, never the default (11.12.9).
        screen.buttons = {Outline(kCancel, u"Cancel", u"Esc"),
                          {kUninstallAndDelete, u"Uninstall and delete",
                           Screen::ButtonKind::kDestructive, u""}};
        screen.escape = kCancel;
        screen.enter = 0;
      } else {
        TwoButtons(screen, kCancel, u"Cancel", kUninstall, u"Uninstall");
      }
      break;
    case Step::kOpen:
      screen.title = u"Pane is open";
      screen.message =
          u"Exit Pane, and the uninstall goes on by itself once it has gone.";
      TwoButtons(screen, kCancel, u"Cancel", kExitPane, u"Exit Pane");
      break;
    case Step::kRemoving:
      screen.title = u"Uninstalling Pane";
      if (erase_) {
        screen.message =
            u"Your data can’t be read any more: its key went first. "
            u"Erasing what’s left on disk.";
        screen.bar = Screen::Bar{u"Erasing Pane’s data", u""};
      } else {
        screen.bar = Screen::Bar{u"Removing Pane’s files", u""};
      }
      break;
    case Step::kDone:
      screen.title = u"Pane is uninstalled";
      // Where there was no data, nothing stays to say so of (until commit
      // f475f04 the line showed where the data had been cleared before).
      if (erase_) {
        screen.message = u"Pane and its data are gone from this PC.";
      } else if (has_data_) {
        screen.message =
            u"Your data stays in %LOCALAPPDATA%\\Pane, for when Pane is "
            u"installed again.";
      }
      screen.buttons = {Primary(kClose, u"Close")};
      screen.enter = kClose;
      screen.escape = kClose;
      break;
    case Step::kFailed:
      screen.title = u"Pane couldn’t be uninstalled";
      screen.message = failure_message_;
      // Try again where its words offer it, and only there.
      if (retry_) {
        TwoButtons(screen, kClose, u"Close", kTryAgain, u"Try again");
      } else {
        screen.buttons = {Primary(kClose, u"Close")};
        screen.enter = kClose;
        screen.escape = kClose;
      }
      break;
  }
  window_.Show(screen);
}

void UninstallFlow::NotInstalledHere() {
  failure_message_ = base::StrCat({u"Pane isn’t installed where Windows says (",
                                   CodeText(ERROR_PATH_NOT_FOUND), u")."});
  retry_ = false;
  ShowStep(Step::kFailed);
}

void UninstallFlow::Fail(std::u16string message, bool retry) {
  failure_message_ = std::move(message);
  retry_ = retry;
  ShowStep(Step::kFailed);
}

void UninstallFlow::OnBoxToggled(bool ticked) {
  if (step_ != Step::kAsking) {
    return;
  }
  erase_ = ticked;
  ShowStep(Step::kAsking);
}

void UninstallFlow::OnButton(int id) {
  switch (id) {
    case kCancel:
    case kClose:
      exit_code_ = step_ == Step::kDone ? 0 : 1;
      DeleteOwnCopy();
      window_.Close();
      return;
    case kUninstall:
    case kUninstallAndDelete:
    case kTryAgain:
      if (id == kTryAgain ? step_ != Step::kFailed || !retry_
                          : step_ != Step::kAsking) {
        return;
      }
      if (!IsInstalledAppFolder(app_folder_)) {
        NotInstalledHere();
        return;
      }
      if (!ProcessesFrom(app_folder_).empty()) {
        ShowStep(Step::kOpen);
        WatchUntilGone();
        return;
      }
      Remove();
      return;
    case kExitPane:
      if (step_ != Step::kOpen || !IsInstalledAppFolder(app_folder_)) {
        return;
      }
      worker_.task_runner()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](const base::FilePath& app) {
                           const std::vector<HWND> panes = PanesFrom(app);
                           if (panes.empty()) {
                             EndProcesses(ProcessesFrom(app));
                           }
                           for (HWND pane : panes) {
                             AskToExit(pane);
                           }
                         },
                         app_folder_));
      return;
  }
}

bool UninstallFlow::OnCloseAsked() {
  if (step_ == Step::kRemoving) {
    // While it erases, Close waits for the erase (11.12.10).
    close_when_done_ = true;
    ::ShowWindow(window_.hwnd(), SW_HIDE);
    return false;
  }
  DeleteOwnCopy();
  return true;
}

double UninstallFlow::BarAt(std::u16string& right) {
  if (step_ != Step::kRemoving) {
    return 1;
  }
  if (erase_) {
    const uint64_t total = std::max<uint64_t>(data_size_, 1);
    const uint64_t done = std::min<uint64_t>(erased_, total);
    right = SizeOfText(done, total);
    return static_cast<double>(done) / total;
  }
  return app_size_
             ? static_cast<double>(std::min<uint64_t>(removed_, app_size_)) /
                   app_size_
             : 0;
}

void UninstallFlow::WatchUntilGone() {
  worker_.task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](UninstallFlow* flow) {
            if (!ProcessesFrom(flow->app_folder_).empty()) {
              flow->WatchUntilGone();
              return;
            }
            flow->window_.Post(base::BindOnce(
                [](UninstallFlow* flow) {
                  if (flow->step_ == Step::kOpen) {
                    flow->Remove();
                  }
                },
                flow));
          },
          base::Unretained(this)),
      kWatchInterval);
}

void UninstallFlow::Remove() {
  // Checked again here, as RemovePane checks again before each step: never
  // trusting that Start or a button did.
  if (!IsInstalledAppFolder(app_folder_)) {
    NotInstalledHere();
    return;
  }
  erased_ = 0;
  removed_ = 0;
  ShowStep(Step::kRemoving);
  worker_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](UninstallFlow* flow) {
            const base::FilePath app = flow->app_folder_;
            flow->app_size_ = FolderSize(app);
            const RemovalResult result =
                RemovePane(app, flow->unregistered_,
                           base::BindOnce(&UninstallFlow::Unregister,
                                          base::Unretained(flow)),
                           base::BindRepeating(
                               [](UninstallFlow* flow, uint64_t size) {
                                 flow->removed_ += size;
                               },
                               base::Unretained(flow)));
            flow->window_.Post(base::BindOnce(&UninstallFlow::Removed,
                                              base::Unretained(flow), result));
          },
          base::Unretained(this)));
}

std::optional<int> UninstallFlow::Unregister(const base::FilePath& app) {
  // chrome.exe's erase says how far it has gone, a line at a time:
  // "<erased> <total>".
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  SECURITY_ATTRIBUTES inherit = {.nLength = sizeof(inherit),
                                 .bInheritHandle = TRUE};
  ::CreatePipe(&read, &write, &inherit, 0);
  base::win::ScopedHandle read_end(read);
  base::win::ScopedHandle write_end(write);
  ::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  base::LaunchOptions options;
  options.handles_to_inherit.push_back(write);
  base::Thread reader("PaneProgress");
  reader.Start();
  reader.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](HANDLE pipe, UninstallFlow* flow) {
            std::string pending;
            std::array<char, 256> buffer;
            DWORD count = 0;
            while (::ReadFile(pipe, buffer.data(), buffer.size(), &count,
                              nullptr) &&
                   count) {
              pending.append(buffer.data(), count);
              size_t end;
              while ((end = pending.find('\n')) != std::string::npos) {
                const std::vector<std::string> parts = base::SplitString(
                    pending.substr(0, end), " ", base::TRIM_WHITESPACE,
                    base::SPLIT_WANT_NONEMPTY);
                uint64_t erased = 0;
                uint64_t total = 0;
                if (parts.size() == 2 &&
                    base::StringToUint64(parts[0], &erased) &&
                    base::StringToUint64(parts[1], &total)) {
                  flow->erased_ = erased;
                  if (total) {
                    flow->data_size_ = total;
                  }
                }
                pending.erase(0, end + 1);
              }
            }
          },
          read_end.get(), base::Unretained(this)));
  const std::optional<int> code = RunChrome(
      app,
      {base::StrCat({kUnregisterSwitch, "=",
                     erase_ ? kUnregisterErase : kUnregisterKeep}),
       base::StrCat(
           {kProgressSwitch, "=",
            base::NumberToString(reinterpret_cast<uintptr_t>(write))})},
      base::TimeDelta::Max(), Note, options);
  write_end.Close();
  reader.Stop();
  return code;
}

void UninstallFlow::Removed(RemovalResult result) {
  unregistered_ = result.unregistered;
  if (result.outcome == RemovalOutcome::kPaneOpen) {
    ShowStep(Step::kOpen);
    WatchUntilGone();
    return;
  }
  if (result.outcome == RemovalOutcome::kRemoved) {
    exit_code_ = 0;
    if (close_when_done_) {
      DeleteOwnCopy();
      window_.Close();
      return;
    }
    ShowStep(Step::kDone);
    return;
  }
  base::FilePath temp;
  if (base::GetTempDir(&temp)) {
    WriteNotes(temp.Append(L"Pane uninstall.log"));
  }
  // Closed while it erased (11.12.10): the window comes back to say what
  // stayed, rather than close on it.
  if (close_when_done_) {
    close_when_done_ = false;
    ::ShowWindow(window_.hwnd(), SW_SHOW);
  }
  switch (result.outcome) {
    case RemovalOutcome::kRefused:
      NotInstalledHere();
      return;
    case RemovalOutcome::kNotUnregistered:
      // Nothing of the app removed, and Installed apps keeps its entry.
      Fail(base::StrCat({u"Something went wrong while removing it (",
                         CodeText(result.error), u"). Try again."}),
           /*retry=*/true);
      return;
    case RemovalOutcome::kFilesLeft:
      // Its entry and shortcuts gone: Try again removes what is left, and
      // the words say where it is for after Close.
      Fail(base::StrCat({u"Some of Pane’s files are still in "
                         u"%LOCALAPPDATA%\\Programs\\Pane (",
                         CodeText(result.error),
                         u"). Try again, or delete that folder."}),
           /*retry=*/true);
      return;
    case RemovalOutcome::kRemoved:
    case RemovalOutcome::kPaneOpen:
      return;
  }
}

void UninstallFlow::DeleteOwnCopy() {
  DeleteOwnCopyAfterExit(kUninstallCopyPrefix);
}

}  // namespace pane::installer
