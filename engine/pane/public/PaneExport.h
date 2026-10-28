// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEEXPORT_H_
#define PANE_PUBLIC_PANEEXPORT_H_

// Marks the classes and symbols PaneCore.framework exports to the Pane app.
// Chromium builds with hidden visibility; chrome/app/framework.exports lists
// the same `Pane*` names for the final link.
#define PANE_EXPORT __attribute__((visibility("default")))

#endif  // PANE_PUBLIC_PANEEXPORT_H_
