// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_INSTALLER_FLOW_H_
#define PANE_INSTALLER_FLOW_H_

#include <windows.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/process/process_handle.h"
#include "base/threading/thread.h"
#include "base/version.h"
#include "pane/installer/payload.h"
#include "pane/installer/removal.h"
#include "pane/installer/screen.h"
#include "pane/installer/unpack.h"
#include "pane/installer/window.h"

namespace pane::installer {

// What the installer notes as it goes, written beside it if it fails ("the
// log beside the installer names the step", Paper 11.12.8), and to the
// system's debug log.
void Note(const std::string& line);
void WriteNotes(const base::FilePath& path);

// HRESULT_FROM_WIN32's code, as a failure shows it: "0x80070570".
std::u16string CodeText(DWORD error);

// Pane's install (Paper 11.12.6 to 11.12.8): checks, then copies the payload
// into a folder of its own beside Application and renames it into place once
// whole, registers through chrome.exe, opens Pane, and closes as Pane's
// first window paints.
class InstallFlow : public InstallWindow::Delegate {
 public:
  InstallFlow(base::FilePath installer, std::optional<Payload> payload);
  InstallFlow(const InstallFlow&) = delete;
  InstallFlow& operator=(const InstallFlow&) = delete;
  ~InstallFlow() override;

  // Shows the first screen and runs until the window closes: the process's
  // exit code.
  int Run();

  // InstallWindow::Delegate:
  void OnButton(int id) override;
  void OnBoxToggled(bool ticked) override {}
  bool OnCloseAsked() override;
  double BarAt(std::u16string& right) override;

 private:
  enum class Step {
    kChecking,
    kNewer,
    kOpen,
    kSpace,
    kCopying,
    kAdding,
    kOpening,
    kBlocked,
    kFailed,
  };

  // Checks, in the boards' order, then copies.
  void Check();
  void ShowStep(Step step);
  void Fail(DWORD error, std::u16string what);
  // The worker's parts.
  void Copy();
  void Register(bool first_install, base::FilePath old_app);
  void Launch();
  // Posted to the window's thread.
  void Copied(UnpackResult result,
              bool intact,
              std::optional<base::FilePath> old_app,
              bool first_install,
              DWORD swap_error);
  void Registered(DWORD error);
  void Opened(bool painted, DWORD error);
  void WatchUntilGone();

  const base::FilePath installer_;
  const std::optional<Payload> payload_;
  const base::Version version_;
  InstallWindow window_;
  base::Thread worker_;
  Step step_ = Step::kChecking;
  UnpackProgress progress_;
  std::atomic<bool> cancel_{false};
  bool close_when_done_ = false;
  // The install is done but Pane didn't open: Try again opens it again.
  bool open_failed_ = false;
  std::u16string failure_message_;
  std::u16string blocked_file_;
  std::optional<base::Version> installed_;
  uint64_t free_ = 0;
  int exit_code_ = 1;
};

// Pane's uninstall dialog (Paper 11.12.9, 11.12.10), in the helper copied
// out of the app folder: asks, with the data kept unless the box is ticked;
// waits for Pane to exit; has chrome.exe remove every registration and, if
// asked, erase the data the panic wipe's way; then removes the app folder,
// and its own copy as it exits.
class UninstallFlow : public InstallWindow::Delegate {
 public:
  UninstallFlow(base::FilePath app_folder, base::ProcessId after);
  UninstallFlow(const UninstallFlow&) = delete;
  UninstallFlow& operator=(const UninstallFlow&) = delete;
  ~UninstallFlow() override;

  int Run();

  // InstallWindow::Delegate:
  void OnButton(int id) override;
  void OnBoxToggled(bool ticked) override;
  bool OnCloseAsked() override;
  double BarAt(std::u16string& right) override;

 private:
  enum class Step { kAsking, kOpen, kRemoving, kDone, kFailed };

  void ShowStep(Step step);
  void Start();
  // removal.h's RemovePane on the worker: refused, as everything that
  // deletes or unregisters refuses, unless the folder is the install's own.
  void Remove();
  void Removed(RemovalResult result);
  // chrome.exe's unregister, the erase's progress read as it goes.
  std::optional<int> Unregister(const base::FilePath& app);
  // The failure for a folder that isn't the install's own: Close alone.
  void NotInstalledHere();
  void Fail(std::u16string message, bool retry);
  void WatchUntilGone();
  // Starts what deletes this copy once it has exited.
  void DeleteOwnCopy();

  const base::FilePath app_folder_;
  const base::ProcessId after_;
  InstallWindow window_;
  base::Thread worker_;
  Step step_ = Step::kAsking;
  bool erase_ = false;
  bool close_when_done_ = false;
  std::atomic<uint64_t> data_size_{0};
  std::atomic<uint64_t> erased_{0};
  std::atomic<uint64_t> removed_{0};
  std::atomic<uint64_t> app_size_{0};
  bool has_data_ = false;
  // The registrations are gone: a Try again removes what files are left.
  bool unregistered_ = false;
  // Whether the failure shown has a Try again, which its words then say.
  bool retry_ = false;
  std::u16string failure_message_;
  int exit_code_ = 1;
};

}  // namespace pane::installer

#endif  // PANE_INSTALLER_FLOW_H_
