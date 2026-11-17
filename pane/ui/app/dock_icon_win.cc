// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "pane/app/test_copy.h"
#include "pane/app/windows_integration.h"
#include "pane/browser/ui/window_host.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// Alt+Tab's and the title bar's icon: each window's, which Windows takes as
// the window gives it (WM_SETICON, through Widget::UpdateWindowIcon), at its
// own sizes, from the sky's image as the Mac's Dock shows it. The taskbar's
// button shows another: the icon of the shortcut its AppUserModelID names,
// Start's Pane.lnk, which it reads when the window's icon changes and that
// shortcut's icon changes right after it, as FollowInShortcuts rewrites it
// (W10a, measured in the test VM: an icon changed alone, a shortcut changed
// alone or a second after the icon, left the button as it was). The
// executable's own icon, which shows before the window does, in Explorer, and
// in Task Manager, is Pane.ico's Day (patch 0001's chrome_exe.rc; Paper
// 11.12.15).
// static
void DockIcon::SetImage(const gfx::ImageSkia& image) {
  WindowHost::SetIcon(image);
}

namespace {

// The shell's COM objects, off the UI thread, where the shortcuts are
// rewritten. Not at BEST_EFFORT, nor after startup: Chromium holds both until
// its first page has loaded or 3 minutes have gone (content's
// BrowserMainLoop), and a Pane on its Lock or with no profile open loads
// none, so a change would wait minutes.
base::SingleThreadTaskRunner& Shell() {
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> shell(
      base::ThreadPool::CreateCOMSTATaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  return **shell;
}

// Whether a test copy's shortcuts are its own: Start's, the desktop's, and
// the pins' folders all in the temporary folder, under the home it was given
// (TakeTestCopyHome), never the user's.
bool ShortcutsOfATestCopysOwn() {
  constexpr int kFolders[] = {base::DIR_START_MENU, base::DIR_USER_DESKTOP,
                              base::DIR_TASKBAR_PINS};
  for (const int folder : kFolders) {
    base::FilePath path;
    if (!base::PathService::Get(folder, &path) || !InTemporaryFolder(path)) {
      return false;
    }
  }
  return true;
}

windows::PointedShortcuts RewriteShortcuts(const base::FilePath& chrome_exe,
                                           model::AppIcon sky,
                                           bool test_copy) {
  if (test_copy && !ShortcutsOfATestCopysOwn()) {
    return {};
  }
  return windows::PointShortcutsAt(chrome_exe, sky);
}

// The shortcuts following the sky the windows show. Another program may
// hold one as it is rewritten: Windows maps Start's shortcut while it reads
// it for Pane's taskbar button, as Pane's window shows, and an overwrite
// meanwhile fails with ERROR_USER_MAPPED_FILE. Dropped, it left the shortcut
// and the taskbar's button on the sky before until the sky changed again
// (W9a, W9d; W10a found it, 3 changes of 15 within seconds of a launch, the
// test VM starved). A rewrite that fails is tried again after 0.1 s, then
// twice as long each time, eight times, each try giving the windows their
// icon again first, as the change did, so the taskbar's button reads the
// shortcut it rewrites. A newer sky stops the tries.
class ShortcutsFollower {
 public:
  static ShortcutsFollower& Get() {
    static base::NoDestructor<ShortcutsFollower> follower;
    return *follower;
  }

  void Follow(const base::FilePath& chrome_exe, model::AppIcon sky) {
    chrome_exe_ = chrome_exe;
    sky_ = sky;
    ++follow_;
    tries_ = 0;
    retry_.Stop();
    Rewrite();
  }

 private:
  static constexpr int kTries = 8;
  static constexpr base::TimeDelta kFirstWait = base::Milliseconds(100);

  void Rewrite() {
    Shell().PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&RewriteShortcuts, chrome_exe_, sky_,
                       base::CommandLine::ForCurrentProcess()->HasSwitch(
                           "use-mock-keychain")),
        base::BindOnce(&ShortcutsFollower::Rewrote, base::Unretained(this),
                       follow_));
  }

  void Rewrote(int follow, windows::PointedShortcuts pointed) {
    if (follow != follow_ || pointed.failed == 0) {
      return;
    }
    if (tries_ == kTries) {
      LOG(ERROR) << "Could not point the shortcuts at the App icon";
      return;
    }
    retry_.Start(FROM_HERE, kFirstWait * (1 << tries_++),
                 base::BindOnce(
                     [](ShortcutsFollower* follower) {
                       if (const gfx::ImageSkia icon = WindowHost::GetIcon();
                           !icon.isNull()) {
                         WindowHost::SetIcon(icon);
                       }
                       follower->Rewrite();
                     },
                     base::Unretained(this)));
  }

  base::FilePath chrome_exe_;
  model::AppIcon sky_ = model::AppIcon::kDay;
  // Which Follow a rewrite's answer belongs to.
  int follow_ = 0;
  int tries_ = 0;
  base::OneShotTimer retry_;
};

}  // namespace

// The shortcuts name the sky's own .ico, beside chrome.exe (a new path,
// which Windows' icon cache can't serve stale): rewritten at the setting's
// change, and at launch for a pin made while Pane was closed. A test copy's
// sky is never the user's: it follows in the home it was given alone.
// static
void DockIcon::FollowInShortcuts(model::AppIcon sky) {
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    return;
  }
  ShortcutsFollower::Get().Follow(chrome_exe, sky);
}

}  // namespace pane
