// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_TEST_COPY_H_
#define PANE_APP_TEST_COPY_H_

namespace base {
class FilePath;
}

namespace pane {

// Which data a launch may open, and on which Keychain (ADR 0012, ADR 0115).
// A test copy is a launch whose data lies in the temporary folder, where the
// scenario runner makes it, by either name a scenario gives it: the folder
// PANE_DATA_DIR names, or the one under a home of its own (CFFIXED_USER_HOME,
// with HOME). It runs on the mock keychain alone, whose fixed secret opens
// none of the user's data and never reaches their Keychain item, and the mock
// keychain runs a test copy alone. Every other launch is the user's: its data
// in their Application Support, its secret in their Keychain.
enum class LaunchKind {
  kUser,
  kTestCopy,
  // Refused: a test copy without the mock keychain, as 0.19.1's installer
  // relaunches one (no switch, the same PANE_ variables and home), which
  // would read and write the user's Keychain item.
  kTestCopyWithoutMockKeychain,
  // Refused: the mock keychain on data outside the temporary folder, whose
  // fixed secret would seal a key where the user's has none yet, or let a
  // scenario's hooks reach the user's own data.
  kMockKeychainOutsideTest,
};

// `named` is the folder PANE_DATA_DIR names, read whether or not the hooks
// are on (empty when it names none); `derived` is the data folder under the
// home (DefaultDataDirectory). With the mock keychain the hooks are on, and
// the data is `named`, or `derived` when it names none; without it, `derived`.
LaunchKind KindOfLaunch(bool mock_keychain,
                        const base::FilePath& named,
                        const base::FilePath& derived);

// Whether `path` lies inside the temporary folder, by where it leads: the part
// of it that exists, its links resolved, is there, and the rest names no
// parent. Absolute paths only.
bool InTemporaryFolder(const base::FilePath& path);

}  // namespace pane

#endif  // PANE_APP_TEST_COPY_H_
