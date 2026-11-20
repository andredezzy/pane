// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/file_panels.h"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"
#include "ui/views/win/hwnd_util.h"

namespace pane {

namespace {

// How long, and how often, a stock dialog let go of is looked for.
constexpr base::TimeDelta kLookFor = base::Seconds(10);
constexpr base::TimeDelta kLookAgainAfter = base::Milliseconds(50);

HWND OwnerOf(gfx::NativeWindow owner) {
  return owner ? views::HWNDForNativeWindow(owner) : nullptr;
}

// The threads the stock dialogs ran on, each seen as its dialog showed, kept
// until it has ended. Chromium keeps one AutoCloseDialogEventHandler in the
// process at a time, and CHECKs at a dialog's start that none is left
// (auto_close_dialog_event_handler_win.cc); the one before outlives the
// re-enabling of its owner while its thread lets go of the dialog's COM
// objects, and a panel asked for at once in that moment ended the browser
// with 0x80000003 (`file-chooser`'s save picker after its cancelled open
// picker, 1 run in 3 in the test VM, every one at that CHECK; commit 490757e).
std::vector<std::pair<DWORD, base::win::ScopedHandle>>& DialogThreads() {
  static base::NoDestructor<
      std::vector<std::pair<DWORD, base::win::ScopedHandle>>>
      threads;
  return *threads;
}

void KeepDialogThread(HWND dialog) {
  const DWORD id = ::GetWindowThreadProcessId(dialog, nullptr);
  auto& threads = DialogThreads();
  if (!id || std::ranges::any_of(threads, [id](const auto& thread) {
        return thread.first == id;
      })) {
    return;
  }
  base::win::ScopedHandle handle(::OpenThread(SYNCHRONIZE, FALSE, id));
  if (handle.is_valid()) {
    threads.emplace_back(id, std::move(handle));
  }
}

// Whether every stock dialog's thread seen has ended, forgetting those that
// have.
bool DialogThreadsEnded() {
  std::erase_if(DialogThreads(), [](const auto& thread) {
    return ::WaitForSingleObject(thread.second.get(), 0) == WAIT_OBJECT_0;
  });
  return DialogThreads().empty();
}

// Each system dialog (its class "#32770") `owner` has, closed as Chromium
// closes its own from outside (WM_CLOSE: AutoCloseDialogEventHandler), its
// thread kept to be waited for.
void CloseDialogsOf(HWND owner) {
  ::EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        wchar_t name[16] = {};
        ::GetClassNameW(hwnd, name, std::size(name));
        if (pid == ::GetCurrentProcessId() &&
            ::GetWindow(hwnd, GW_OWNER) == reinterpret_cast<HWND>(param) &&
            std::wstring_view(name) == L"#32770") {
          KeepDialogThread(hwnd);
          ::PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(owner));
}

// A stock dialog runs while its owner is disabled, from its start
// (BaseShellDialogImpl::BeginRun) to its end on this thread (EndRun): while
// it does, its dialog is closed as it shows, and looked for again a moment
// later; once its owner is enabled again, and the thread each one ran on has
// ended, or `until`, `then` runs. A take-down (`then` empty) stops too once
// another panel is asked for, whose dialog is its own.
void CloseStockDialogs(HWND owner,
                       base::TimeTicks until,
                       base::OnceClosure then) {
  const bool another = !then && !FilePanelShown().is_none();
  const bool ended =
      ::IsWindowEnabled(owner) && (!then || DialogThreadsEnded());
  if (ended || base::TimeTicks::Now() >= until || another) {
    if (then) {
      std::move(then).Run();
    }
    return;
  }
  CloseDialogsOf(owner);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&CloseStockDialogs, owner, until, std::move(then)),
      kLookAgainAfter);
}

}  // namespace

// Windows' stock dialog runs on a thread of its own, its window a dialog of
// the system's owned by Pane's window, and a listener letting it go leaves it
// showing; under emulation it can show only after the channel has answered
// it. So it is closed as it shows, until it has ended.
void TakeDownStockPanel(gfx::NativeWindow owner) {
  if (const HWND owning = OwnerOf(owner)) {
    CloseStockDialogs(owning, base::TimeTicks::Now() + kLookFor,
                      base::OnceClosure());
  }
}

void WhenStockPanelsEnded(gfx::NativeWindow owner, base::OnceClosure start) {
  const HWND owning = OwnerOf(owner);
  if (!owning) {
    std::move(start).Run();
    return;
  }
  CloseStockDialogs(owning, base::TimeTicks::Now() + kLookFor,
                    std::move(start));
}

}  // namespace pane
