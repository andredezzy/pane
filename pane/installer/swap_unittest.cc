// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/swap.h"

#include <windows.h>

#include <string>

#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
#include "chrome/installer/util/work_item_list.h"
#include "pane/installer/running_pane.h"
#include "pane/installer/update_folder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

namespace pane::installer {

namespace {

std::string Contents(const base::FilePath& file) {
  std::string contents;
  base::ReadFileToString(file, &contents);
  return contents;
}

// An install laid out as Pane's: Pane\Application at 1.0.0, and 2.0.0
// staged beside it in Pane\Update, each with a chrome.exe and a library
// that says which it is.
class SwapTest : public testing::Test {
 protected:
  void SetUp() override {
    // In the temporary folder, where a scenario lays its copy out, and which
    // a swap of anything but the install's own folder must be in: not
    // %SystemRoot%\SystemTemp, where ScopedTempDir makes one for an
    // administrator, as the test VM's SSH runs the tests.
    base::FilePath temporary;
    ASSERT_TRUE(base::GetTempDir(&temporary));
    ASSERT_TRUE(temp_.CreateUniqueTempDirUnderPath(temporary));
    app_ = temp_.GetPath().Append(L"Pane\\Application");
    update_ = UpdateFolderOf(app_);
    staged_ = StagedFolder(update_, "2.0.0");
    Write(app_, "1.0.0", "old");
    Write(staged_, "2.0.0", "new");
  }

  static void Write(const base::FilePath& folder,
                    const std::string& version,
                    const std::string& which) {
    ASSERT_TRUE(base::CreateDirectory(folder.Append(L"locales")));
    ASSERT_TRUE(base::WriteFile(folder.Append(L"pane-version"), version));
    ASSERT_TRUE(base::WriteFile(folder.Append(L"chrome.exe"), which));
    ASSERT_TRUE(base::WriteFile(folder.Append(L"chrome.dll"), which));
    ASSERT_TRUE(base::WriteFile(folder.Append(L"pane_setup.exe"), which));
    ASSERT_TRUE(base::WriteFile(folder.Append(L"locales\\en-US.pak"), which));
  }

  SwapRequest Request() const {
    SwapRequest request;
    request.app = app_;
    request.staged = staged_;
    request.browser = base::kNullProcessId;
    return request;
  }

  base::ScopedTempDir temp_;
  base::FilePath app_;
  base::FilePath update_;
  base::FilePath staged_;
};

}  // namespace

// Chromium's WorkItemList swaps the staged folder in, the app aside, and
// removes the old app once done with.
TEST_F(SwapTest, SwapsTheStagedFolderIn) {
  std::unique_ptr<WorkItemList> swapped =
      SwapIn(app_, staged_, SwappingFolder(update_));
  ASSERT_TRUE(swapped);
  EXPECT_EQ(Contents(app_.Append(L"chrome.dll")), "new");
  EXPECT_EQ(Contents(app_.Append(L"locales\\en-US.pak")), "new");
  EXPECT_FALSE(base::PathExists(staged_));
  // The old app waits aside until the list goes.
  EXPECT_FALSE(base::IsDirectoryEmpty(SwappingFolder(update_)));
  swapped.reset();
  EXPECT_TRUE(base::IsDirectoryEmpty(SwappingFolder(update_)));
}

// A file of the app held without FILE_SHARE_DELETE (an antivirus scanning
// it, another program reading it) fails the swap, and the rollback leaves
// both folders as they were.
TEST_F(SwapTest, RollsBackWhenAFileIsHeld) {
  base::File held(app_.Append(L"chrome.dll"),
                  base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(held.IsValid());
  EXPECT_FALSE(SwapIn(app_, staged_, SwappingFolder(update_)));
  held.Close();
  for (const wchar_t* file : {L"chrome.exe", L"chrome.dll", L"pane_setup.exe",
                              L"locales\\en-US.pak"}) {
    EXPECT_EQ(Contents(app_.Append(file)), "old") << file;
    EXPECT_EQ(Contents(staged_.Append(file)), "new") << file;
  }
  EXPECT_EQ(Contents(app_.Append(L"pane-version")), "1.0.0");
  EXPECT_FALSE(base::PathExists(SwappingFolder(update_)));
}

// The whole run: a swap that fails leaves the mark the next launch reads
// (Paper 11.12.14, E) and keeps the staged copy and its installer; the next
// one installs, and removes the old app, the installer, its record, the
// mark, and the update folder.
TEST_F(SwapTest, MarksAFailureAndCleansUpAfterSuccess) {
  const base::FilePath download = temp_.GetPath().Append(L"Pane-2.0.0-x64.exe");
  ASSERT_TRUE(base::WriteFile(download, "installer"));
  ASSERT_TRUE(WriteDownloadRecord(DownloadRecord(update_, "2.0.0"), download));
  SwapRequest request = Request();
  request.download = download;

  {
    base::File held(app_.Append(L"locales\\en-US.pak"),
                    base::File::FLAG_OPEN | base::File::FLAG_READ);
    ASSERT_TRUE(held.IsValid());
    EXPECT_EQ(RunSwap(request), SwapOutcome::kNotInstalled);
  }
  EXPECT_TRUE(base::PathExists(NotInstalledMark(update_)));
  EXPECT_EQ(Contents(app_.Append(L"chrome.dll")), "old");
  EXPECT_EQ(Contents(staged_.Append(L"chrome.dll")), "new");
  EXPECT_TRUE(base::PathExists(download));
  const UpdateFolderScan scan =
      ScanUpdateFolder(update_, base::Version("1.0.0"));
  EXPECT_EQ(scan.kept, "2.0.0");
  EXPECT_TRUE(scan.not_installed);

  EXPECT_EQ(RunSwap(request), SwapOutcome::kInstalled);
  EXPECT_EQ(Contents(app_.Append(L"chrome.dll")), "new");
  EXPECT_FALSE(base::PathExists(download));
  EXPECT_FALSE(base::PathExists(update_));
}

// What Pane hands the helper reads back whole, its arguments for the new
// Pane among them, spaces and all.
TEST_F(SwapTest, RequestReadsBack) {
  SwapRequest request = Request();
  request.download = temp_.GetPath().Append(L"Down loads\\Pane-2.0.0-x64.exe");
  request.browser = 4242;
  request.relaunch = true;
  request.in_front = true;
  request.arguments = {L"--use-mock-keychain",
                       L"--user-data-dir=C:\\a b\\Pane\\User Data"};
  const base::CommandLine command =
      SwapCommandLine(base::FilePath(L"C:\\t\\pane_setup.exe"), request);
  const base::CommandLine parsed =
      base::CommandLine::FromString(command.GetCommandLineString());
  const std::optional<SwapRequest> read = SwapRequestFrom(parsed);
  ASSERT_TRUE(read);
  EXPECT_EQ(read->app, request.app);
  EXPECT_EQ(read->staged, request.staged);
  EXPECT_EQ(read->download, request.download);
  EXPECT_EQ(read->browser, 4242u);
  EXPECT_TRUE(read->relaunch);
  EXPECT_TRUE(read->in_front);
  EXPECT_EQ(read->arguments, request.arguments);
  // None of them reads as the helper's.
  EXPECT_FALSE(parsed.HasSwitch("use-mock-keychain"));
}

// Only Pane's own layout: the staged version beside the app folder, never a
// folder a command line names elsewhere.
TEST_F(SwapTest, RefusesAnotherLayout) {
  const auto refused = [](const SwapRequest& request) {
    return !SwapRequestFrom(
        SwapCommandLine(base::FilePath(L"C:\\t\\pane_setup.exe"), request));
  };
  EXPECT_FALSE(refused(Request()));
  SwapRequest request = Request();
  request.app = temp_.GetPath().Append(L"Other\\Application");
  EXPECT_TRUE(refused(request));
  request = Request();
  request.staged = temp_.GetPath().Append(L"2.0.0");
  EXPECT_TRUE(refused(request));
  request = Request();
  request.staged = update_.Append(L"..\\Application");
  EXPECT_TRUE(refused(request));
  request = Request();
  request.staged = update_.Append(L"Swapping");
  EXPECT_TRUE(refused(request));
  // Laid out as an install, but neither the install's own folder nor a
  // scenario's copy in the temporary folder: a user's copy elsewhere is never
  // replaced, its old files never deleted, whatever names it.
  request = Request();
  request.app = base::FilePath(L"C:\\Copies\\Pane\\Application");
  request.staged = base::FilePath(L"C:\\Copies\\Pane\\Update\\2.0.0");
  EXPECT_TRUE(refused(request));
  EXPECT_EQ(RunSwap(request), SwapOutcome::kRefused);
}

namespace {

// A process of Pane's that outlived its browser, as an idle renderer did
// (W5j, W7a): it waits to be ended.
MULTIPROCESS_TEST_MAIN(PaneLeftover) {
  base::PlatformThread::Sleep(TestTimeouts::action_max_timeout());
  return 0;
}

// A browser that starts a process of its own after the helper's snapshot,
// as Pane's may as it goes, then waits.
MULTIPROCESS_TEST_MAIN(PaneStartingLate) {
  base::PlatformThread::Sleep(base::Milliseconds(500));
  base::Process late = base::SpawnMultiProcessTestChild(
      "PaneLeftover", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  base::PlatformThread::Sleep(TestTimeouts::action_max_timeout());
  return 0;
}

// A browser that exits a moment after the helper starts.
MULTIPROCESS_TEST_MAIN(PaneExiting) {
  base::PlatformThread::Sleep(base::Milliseconds(500));
  return 0;
}

// Another Pane's browser from the same folder, opened again or on data of its
// own: its process singleton's window (running_pane.h's PanesFrom), then it
// waits.
MULTIPROCESS_TEST_MAIN(PaneBrowser) {
  const WNDCLASSEXW window_class = {.cbSize = sizeof(window_class),
                                    .lpfnWndProc = ::DefWindowProcW,
                                    .hInstance = ::GetModuleHandleW(nullptr),
                                    .lpszClassName = L"Chrome_MessageWindow"};
  ::RegisterClassExW(&window_class);
  ::CreateWindowExW(0, window_class.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                    HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
  base::PlatformThread::Sleep(TestTimeouts::action_max_timeout());
  return 0;
}

}  // namespace

// The helper waits for the browser to have exited, and says so: an exit code
// base::Process reads after its wait needs its handle opened for it, which
// one opened to wait alone (SYNCHRONIZE) lacks, and the swap read every exit
// as "Pane didn't exit" (W7c's first runs).
TEST(SwapProcessesTest, WaitsForTheBrowsersExit) {
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  base::Process browser = base::SpawnMultiProcessTestChild(
      "PaneExiting", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(browser.IsValid());
  const PaneProcesses pane(exe.DirName());
  ASSERT_TRUE(pane.Contains(browser.Pid()));
  const base::TimeTicks started = base::TimeTicks::Now();
  EXPECT_TRUE(
      pane.WaitForExitOf(browser.Pid(), TestTimeouts::action_timeout()));
  EXPECT_LT(base::TimeTicks::Now() - started, TestTimeouts::action_timeout());
  // Gone already, or never one of them.
  EXPECT_TRUE(pane.WaitForExitOf(browser.Pid(), base::TimeDelta()));
  EXPECT_TRUE(pane.WaitForExitOf(base::kNullProcessId, base::TimeDelta()));
  // The same of the installer's own wait (running_pane.h).
  base::Process other = base::SpawnMultiProcessTestChild(
      "PaneExiting", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(other.IsValid());
  EXPECT_TRUE(::pane::installer::WaitForExit({other.Pid()},
                                             TestTimeouts::action_timeout()));
}

// What Pane's own processes started since the snapshot is Pane's: taken in,
// and ended with them, never read as Pane opened again.
TEST(SwapProcessesTest, AdoptsWhatPaneStartedSince) {
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  base::Process browser = base::SpawnMultiProcessTestChild(
      "PaneStartingLate", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(browser.IsValid());
  PaneProcesses pane(exe.DirName());
  const size_t first = pane.size();
  // Its own process, started after the snapshot.
  for (int wait = 0; wait < 100 && !pane.OthersRun(); ++wait) {
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }
  ASSERT_TRUE(pane.OthersRun());
  pane.Adopt();
  EXPECT_EQ(pane.size(), first + 1);
  EXPECT_FALSE(pane.OthersRun());
  pane.End();
  EXPECT_TRUE(pane.WaitForExit(TestTimeouts::action_timeout()));
}

// The helper ends what ran from the app folder as Pane exited, and only
// that: a process started from it after (Pane, opened again) it leaves, and
// says it runs.
TEST(SwapProcessesTest, EndsWhatOutlivedPaneAlone) {
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  base::Process leftover = base::SpawnMultiProcessTestChild(
      "PaneLeftover", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(leftover.IsValid());
  const PaneProcesses pane(exe.DirName());
  EXPECT_TRUE(pane.Contains(leftover.Pid()));
  EXPECT_FALSE(pane.OthersRun());

  base::Process later = base::SpawnMultiProcessTestChild(
      "PaneLeftover", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(later.IsValid());
  EXPECT_FALSE(pane.Contains(later.Pid()));
  EXPECT_TRUE(pane.OthersRun());

  EXPECT_FALSE(pane.WaitForExit(base::Milliseconds(100)));
  pane.End();
  EXPECT_TRUE(pane.WaitForExit(TestTimeouts::action_timeout()));
  EXPECT_TRUE(leftover.WaitForExitWithTimeout(base::TimeDelta(), nullptr));
  EXPECT_FALSE(later.WaitForExitWithTimeout(base::TimeDelta(), nullptr));
  later.Terminate(0, /*wait=*/true);
}

// Another Pane's browser from the folder is never ended: the helper sees it,
// ends nothing, and leaves the update for the next exit (as the installer
// ends nothing while a browser is there to ask).
TEST(SwapProcessesTest, LeavesAnotherPaneAlone) {
  base::FilePath exe;
  ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &exe));
  base::Process other = base::SpawnMultiProcessTestChild(
      "PaneBrowser", base::GetMultiProcessTestChildBaseCommandLine(),
      base::LaunchOptions());
  ASSERT_TRUE(other.IsValid());
  for (int wait = 0; wait < 100 && PanesFrom(exe.DirName()).empty(); ++wait) {
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }
  const PaneProcesses pane(exe.DirName());
  ASSERT_TRUE(pane.Contains(other.Pid()));
  EXPECT_FALSE(pane.OthersRun());
  EXPECT_TRUE(pane.AnotherRuns());
  other.Terminate(0, /*wait=*/true);
  EXPECT_FALSE(pane.AnotherRuns());
}

}  // namespace pane::installer
