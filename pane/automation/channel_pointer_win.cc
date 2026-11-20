// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/channel_pointer.h"

#include <windows.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "pane/ui/app/app_events.h"

namespace pane {

namespace {

// The pointer's messages as Windows posts them to the thread's queue, each
// at the system's pointer: its moves, a drag's too, its leave and hover
// (TrackMouseEvent's), and its wheels, over the client area or the frame.
// The channel's go to the window's event sink itself, past the queue, and
// its clicks are none of these (input_win.cc).
bool IsSystemPointer(UINT message) {
  switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_MOUSEHOVER:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_NCMOUSEMOVE:
    case WM_NCMOUSELEAVE:
    case WM_NCMOUSEHOVER:
      return true;
  }
  return false;
}

// Each such message taken off the queue becomes WM_NULL, which nothing
// handles, before it is dispatched.
LRESULT CALLBACK DropSystemPointer(int code, WPARAM w_param, LPARAM l_param) {
  if (code == HC_ACTION && w_param == PM_REMOVE) {
    MSG* message = reinterpret_cast<MSG*>(l_param);
    if (IsSystemPointer(message->message)) {
      message->message = WM_NULL;
    }
  }
  return ::CallNextHookEx(nullptr, code, w_param, l_param);
}

}  // namespace

// static
base::ScopedClosureRunner ChannelPointer::KeepSystemPointerOut() {
  // In front the pointer is a person's, as in a pass by hand (qa-hold).
  if (MayTakeFront()) {
    return base::ScopedClosureRunner();
  }
  // Windows sends the window under its pointer a move whenever a window
  // shows, hides, or moves there, where the pointer rests: a button's
  // tooltip, shown under the channel's pointer, sent Pane's window one at
  // the VM's own pointer, elsewhere on the window, and the button's hover
  // left it 0.5 s after the channel's move (commit dde4090). The UI thread's
  // queue, where every window of the app takes its input: a hook on it alone.
  HHOOK hook = ::SetWindowsHookExW(WH_GETMESSAGE, &DropSystemPointer, nullptr,
                                   ::GetCurrentThreadId());
  if (!hook) {
    PLOG(ERROR) << "Could not keep the system's pointer from the window";
    return base::ScopedClosureRunner();
  }
  return base::ScopedClosureRunner(
      base::BindOnce([](HHOOK hook) { ::UnhookWindowsHookEx(hook); }, hook));
}

}  // namespace pane
