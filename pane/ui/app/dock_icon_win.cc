// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/dock_icon.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "pane/app/windows_integration.h"
#include "pane/browser/ui/window_host.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// The taskbar's, Alt+Tab's, and the title bar's icon: each window's, as
// Windows shows an app's, which it takes as the window gives it (WM_SETICON,
// through Widget::UpdateWindowIcon), at its own sizes, from the sky's image
// as the Mac's Dock shows it. The executable's own icon, which shows before
// the window does, in Explorer, and in Task Manager, is Pane.ico's Day
// (patch 0001's chrome_exe.rc; Paper 11.12.15).
// static
void DockIcon::SetImage(const gfx::ImageSkia& image) {
  WindowHost::SetIcon(image);
}

// The shortcuts name the sky's own .ico, beside chrome.exe (a new path,
// which Windows' icon cache can't serve stale): rewritten at the setting's
// change, and at launch for a pin made while Pane was closed, off the UI
// thread, through the shell's COM objects. Not at BEST_EFFORT, nor after
// startup: Chromium holds both until its first page has loaded or 3 minutes
// have gone (content's BrowserMainLoop), and a Pane on its Lock or with no
// profile open loads none, so a change would wait minutes. A test copy's sky
// is never the user's: its launch leaves them alone.
// static
void DockIcon::FollowInShortcuts(model::AppIcon sky) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("use-mock-keychain")) {
    return;
  }
  base::FilePath chrome_exe;
  if (!base::PathService::Get(base::FILE_EXE, &chrome_exe)) {
    return;
  }
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> shell(
      base::ThreadPool::CreateCOMSTATaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  (*shell)->PostTask(
      FROM_HERE, base::BindOnce(base::IgnoreResult(&windows::PointShortcutsAt),
                                chrome_exe, sky));
}

}  // namespace pane
