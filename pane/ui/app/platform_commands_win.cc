// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/platform_commands.h"

#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "pane/app/version.h"
#include "pane/browser/ui/window_host.h"
#include "pane/ui/window/root_view.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// The key a field takes the edit by, as Windows' fields do: Ctrl and the
// edit's letter (Textfield::GetCommandForKeyEvent).
ui::KeyboardCode FieldKey(Command command) {
  switch (command) {
    case Command::kUndo:
      return ui::VKEY_Z;
    case Command::kRedo:
      return ui::VKEY_Y;
    case Command::kCut:
      return ui::VKEY_X;
    case Command::kCopy:
      return ui::VKEY_C;
    case Command::kPaste:
      return ui::VKEY_V;
    case Command::kSelectAll:
      return ui::VKEY_A;
    default:
      return ui::VKEY_UNKNOWN;
  }
}

// The page whose view has the keys, or a view of it: its own WebView's.
content::WebContents* FocusedPage(views::View* focused) {
  for (views::View* view = focused; view; view = view->parent()) {
    if (auto* web_view = views::AsViewClass<views::WebView>(view)) {
      return web_view->web_contents();
    }
  }
  return nullptr;
}

// The Edit menu on what has the keys, as Chrome's own menu's Cut, Copy, and
// Paste act on the focused field or the page (BrowserView::CutCopyPaste).
void Edit(views::Widget& widget, Command command) {
  views::View* focused = widget.GetFocusManager()->GetFocusedView();
  if (auto* field = views::AsViewClass<views::Textfield>(focused)) {
    ui::KeyEvent key(ui::EventType::kKeyPressed, FieldKey(command),
                     ui::EF_CONTROL_DOWN);
    // Textfield keeps its own handler private: View's, which it overrides.
    static_cast<views::View*>(field)->OnKeyPressed(key);
    return;
  }
  content::WebContents* page = FocusedPage(focused);
  if (!page) {
    return;
  }
  switch (command) {
    case Command::kUndo:
      page->Undo();
      return;
    case Command::kRedo:
      page->Redo();
      return;
    case Command::kCut:
      page->Cut();
      return;
    case Command::kCopy:
      page->Copy();
      return;
    case Command::kPaste:
      page->Paste();
      return;
    case Command::kSelectAll:
      page->SelectAll();
      return;
    default:
      return;
  }
}

}  // namespace

void RunPlatformCommand(views::Widget* widget, Command command) {
  if (!widget) {
    return;
  }
  switch (command) {
    case Command::kUndo:
    case Command::kRedo:
    case Command::kCut:
    case Command::kCopy:
    case Command::kPaste:
    case Command::kSelectAll:
      Edit(*widget, command);
      return;
    case Command::kMinimize:
      widget->Minimize();
      return;
    // Pane's own dialog over the window (Paper 11.15 A): the sky the taskbar
    // shows, the version, and the copyright.
    case Command::kAbout:
      if (RootView* root = RootView::Of(widget)) {
        root->ShowAbout(WindowHost::GetIcon(),
                        base::UTF8ToUTF16(PaneVersion()));
      }
      return;
    // Maximize, Windows' word for the Mac's Zoom (Paper 11.5.1), restores a
    // maximized window as its caption button does.
    case Command::kZoom:
      if (widget->IsMaximized()) {
        widget->Restore();
      } else {
        widget->Maximize();
      }
      return;
    default:
      return;
  }
}

}  // namespace pane
