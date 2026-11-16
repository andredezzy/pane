// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_files.h"

#include <windows.h>

#include <shellapi.h>

#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "components/services/quarantine/quarantine.h"
#include "pane/installer/switches.h"
#include "url/gurl.h"

namespace pane {

namespace {

// Where Windows keeps a file's Mark of the Web: its Zone.Identifier stream
// (components/services/quarantine/common_win.cc's suffix).
constexpr wchar_t kZoneIdentifier[] = L":Zone.Identifier";

}  // namespace

// Never while Pane runs: its installer would find Pane open.
void OpenUpdateFile(const base::FilePath& file) {}

void OpenUpdateFileAfterExit(const base::FilePath& file) {
  // On the shell's COM thread, as Chrome opens a download: SmartScreen may
  // look at a file that kept its mark for seconds, which the window's thread
  // doesn't wait for. The exit waits for it to have started (BLOCK_SHUTDOWN),
  // and the installer for the exit.
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> shell(
      base::ThreadPool::CreateCOMSTATaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  (*shell)->PostTask(FROM_HERE,
                     base::BindOnce(base::IgnoreResult(&OpenAfterProcessExits),
                                    static_cast<int>(base::GetCurrentProcId()),
                                    file, base::FilePath()));
}

bool OpenAfterProcessExits(int pid,
                           const base::FilePath& file,
                           const base::FilePath& opener) {
  // Pane's installer waits for `pid` itself, hidden, then shows its window
  // (pane/installer/setup_main.cc).
  const std::wstring wait =
      base::StrCat({L"--", base::ASCIIToWide(installer::kAfterSwitch), L"=",
                    base::NumberToWString(pid)});
  const std::wstring parameters =
      opener.empty() ? wait : base::StrCat({L"\"", file.value(), L"\" ", wait});
  const std::wstring program = opener.empty() ? file.value() : opener.value();
  // In the user's folder, never Pane's, which an install replaces.
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  // As Explorer opens it: a file that kept its Mark of the Web meets
  // SmartScreen first.
  SHELLEXECUTEINFOW info = {.cbSize = sizeof(info),
                            .fMask = SEE_MASK_NOASYNC,
                            .lpVerb = L"open",
                            .lpFile = program.c_str(),
                            .lpParameters = parameters.c_str(),
                            .lpDirectory = home.value().c_str(),
                            .nShow = SW_SHOWNORMAL};
  return ::ShellExecuteExW(&info);
}

bool ReleaseFromQuarantine(const base::FilePath& file) {
  const std::wstring mark = file.value() + kZoneIdentifier;
  return ::DeleteFileW(mark.c_str()) ||
         ::GetLastError() == ERROR_FILE_NOT_FOUND;
}

void Quarantine(const base::FilePath& file) {
  quarantine::SetInternetZoneIdentifierDirectly(file, GURL(), GURL());
}

}  // namespace pane
