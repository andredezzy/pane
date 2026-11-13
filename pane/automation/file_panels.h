// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_FILE_PANELS_H_
#define PANE_AUTOMATION_FILE_PANELS_H_

#include <optional>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/values.h"
#include "ui/gfx/native_ui_types.h"

namespace pane {

// The system's own open and save panels (Chrome's stock dialog, U2), as a
// scenario's copy sees and answers them: a factory, set only while the
// channel runs, hands each panel Chrome asks for to the platform's stock
// dialog behind a listener of its own, so the real panel shows on the
// window as it does for the user, and the channel can answer it beside it.
// The panels run out of process on macOS 27 and take no programmatic choice
// (the Swift's answerPanel did the same).
void WatchFilePanels();

// The panel showing, as the Swift's snapshot read it: { kind "fileChooser",
// panel "open" or "save", message }; none without one.
base::Value FilePanelShown();

// Answers the panel showing with `paths`, or cancels it with none, as its
// own buttons would, and takes it down: false with none showing.
bool AnswerFilePanel(std::optional<std::vector<base::FilePath>> paths);

// The platform's half: the stock panel on `owner` taken down as its own
// Cancel takes it, once its listener has gone. The Mac's goes with its
// bridge; Windows' runs on a thread of its own until its window closes.
void TakeDownStockPanel(gfx::NativeWindow owner);

// The platform's half: `start` once the stock panels let go of on `owner`
// have ended: at once on the Mac; on Windows once the dialog Windows runs on
// a thread of its own has closed and given its owner back, as Chromium shows
// one at a time (AutoCloseDialogEventHandler CHECKs it), where the channel's
// answer comes before the dialog has even shown.
void WhenStockPanelsEnded(gfx::NativeWindow owner, base::OnceClosure start);

}  // namespace pane

#endif  // PANE_AUTOMATION_FILE_PANELS_H_
