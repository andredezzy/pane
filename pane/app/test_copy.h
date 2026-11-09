// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_TEST_COPY_H_
#define PANE_APP_TEST_COPY_H_

namespace base {
class FilePath;
}

namespace pane {

// Until the Lock's UI exists (docs/plans/views-migration.md, Phase 3), Pane's
// Views UI runs only as a scenario's test copy (StartViewsLaunch): on the mock
// keychain, with its data and its home in the temporary folder, as the
// scenario runner makes them. Whatever lies outside that folder is the user's
// own, which such a launch neither reads nor writes. This goes with the guard.

// Whether `path` lies inside the temporary folder, by where it leads: the part
// of it that exists, its links resolved, is there, and the rest names no
// parent. Absolute paths only.
bool InTemporaryFolder(const base::FilePath& path);

}  // namespace pane

#endif  // PANE_APP_TEST_COPY_H_
