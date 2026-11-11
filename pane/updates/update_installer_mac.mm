// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/updates/update_installer.h"

#import <Foundation/Foundation.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/mac/mac_util.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "pane/model/update_install.h"

namespace pane {

namespace {

constexpr char kDiskutil[] = "/usr/sbin/diskutil";
constexpr char kHdiutil[] = "/usr/bin/hdiutil";

// A tool's run: whether it exited 0. What it said stays out of the log, which
// would name the user's folders.
bool Run(const std::vector<std::string>& argv) {
  std::string output;
  if (base::GetAppOutputAndError(argv, &output)) {
    return true;
  }
  LOG(ERROR) << "The update's " << base::FilePath(argv.front()).BaseName()
             << " failed";
  return false;
}

// Detaches the dmg at `mount` and removes the folder, which rmdir does only
// once it is empty: never a volume still mounted there. `quietly` for a
// leftover, whose dmg is usually gone already.
void Unmount(const base::FilePath& mount, bool quietly) {
  std::string output;
  if (!base::GetAppOutputAndError({kDiskutil, "eject", mount.value()},
                                  &output)) {
    if (!quietly) {
      LOG(WARNING) << "Ejecting the update's dmg failed, detaching it by force";
    }
    base::GetAppOutputAndError(
        {kHdiutil, "detach", mount.value(), "-force", "-quiet"}, &output);
  }
  if (rmdir(mount.value().c_str()) != 0 && !quietly) {
    PLOG(ERROR) << "Could not remove the update's mount point";
  }
}

// The new Pane's `open`: behind the app in use when it may not take the
// front, with Pane's own PANE_ variables, and a home of its own
// (CFFIXED_USER_HOME, with HOME), which only scenarios set, so it opens on
// the same throwaway data and home; then the switches it opens with.
std::vector<std::string> OpenArguments(
    const base::FilePath& app,
    const UpdateInstaller::Relaunch& relaunch) {
  std::vector<std::string> arguments;
  if (!relaunch.in_front) {
    arguments.push_back("-g");
  }
  NSDictionary<NSString*, NSString*>* environment =
      NSProcessInfo.processInfo.environment;
  const bool fixed_home = environment[@"CFFIXED_USER_HOME"] != nil;
  NSArray<NSString*>* names =
      [environment.allKeys sortedArrayUsingSelector:@selector(compare:)];
  for (NSString* name in names) {
    if ([name hasPrefix:@"PANE_"] ||
        (fixed_home && ([name isEqualToString:@"CFFIXED_USER_HOME"] ||
                        [name isEqualToString:@"HOME"]))) {
      arguments.push_back("--env");
      arguments.push_back(base::SysNSStringToUTF8(name) + "=" +
                          base::SysNSStringToUTF8(environment[name]));
    }
  }
  arguments.push_back(app.value());
  if (!relaunch.switches.empty()) {
    arguments.push_back("--args");
    arguments.insert(arguments.end(), relaunch.switches.begin(),
                     relaunch.switches.end());
  }
  return arguments;
}

}  // namespace

UpdateInstaller::UpdateInstaller(base::FilePath bundle, std::string identifier)
    : bundle_(std::move(bundle)), identifier_(std::move(identifier)) {}

UpdateInstaller::UpdateInstaller(const UpdateInstaller&) = default;
UpdateInstaller& UpdateInstaller::operator=(const UpdateInstaller&) = default;
UpdateInstaller::~UpdateInstaller() = default;

// static
std::optional<UpdateInstaller> UpdateInstaller::ForRunningApp() {
  NSBundle* bundle = base::apple::OuterBundle();
  const std::optional<std::string> target =
      model::UpdateInstallTarget(base::SysNSStringToUTF8(bundle.bundlePath));
  NSString* identifier = bundle.bundleIdentifier;
  if (!target || !identifier.length) {
    return std::nullopt;
  }
  const base::FilePath path(*target);
  if (access(path.DirName().value().c_str(), W_OK) != 0) {
    return std::nullopt;
  }
  return UpdateInstaller(path, base::SysNSStringToUTF8(identifier));
}

std::optional<base::FilePath> UpdateInstaller::Stage(
    const base::FilePath& dmg,
    const std::string& version) const {
  base::FilePath temporary;
  if (!base::GetTempDir(&temporary)) {
    return std::nullopt;
  }
  const base::FilePath mount =
      temporary.Append(model::UpdateMountName(getpid()));
  if (!base::CreateDirectory(mount)) {
    return std::nullopt;
  }

  // macOS 27 deprecates hdiutil's attach and detach for diskutil's, which
  // earlier systems lack. The dmg matched the release's SHA-256, so its own
  // checksums need no second read.
  const bool attached =
      base::mac::MacOSMajorVersion() >= 27
          ? Run({kDiskutil, "image", "attach", "--readOnly", "--nobrowse",
                 "--mountPoint", mount.value(), dmg.value()})
          : Run({kHdiutil, "attach", dmg.value(), "-nobrowse", "-readonly",
                 "-noautoopen", "-noverify", "-mountpoint", mount.value()});
  if (!attached) {
    rmdir(mount.value().c_str());
    return std::nullopt;
  }
  base::ScopedClosureRunner unmount(
      base::BindOnce(&Unmount, mount, /*quietly=*/false));

  const base::FilePath app = mount.Append("Pane.app");
  NSDictionary* info = [NSDictionary
      dictionaryWithContentsOfURL:base::apple::FilePathToNSURL(
                                      app.Append("Contents/Info.plist"))];
  NSString* found_identifier =
      base::apple::ObjCCast<NSString>(info[@"CFBundleIdentifier"]);
  NSString* found_version =
      base::apple::ObjCCast<NSString>(info[@"PaneVersion"]);
  if (!found_identifier || !found_version ||
      base::SysNSStringToUTF8(found_identifier) != identifier_ ||
      base::SysNSStringToUTF8(found_version) != version) {
    LOG(ERROR) << "The update's dmg holds no Pane " << version;
    return std::nullopt;
  }

  const base::FilePath staged =
      bundle_.DirName().Append(model::StagedUpdateName(version));
  base::DeletePathRecursively(staged);
  if (!Run({"/usr/bin/ditto", app.value(), staged.value()})) {
    base::DeletePathRecursively(staged);
    return std::nullopt;
  }
  unmount.RunAndReset();

  // Its signature whole, checked on the copy the swap puts in place: the
  // dmg's compressed image is read once, by the copy, and staging takes 4.4 s
  // where checking the dmg's own first took 5.8 (ADR 0092).
  if (!Run({"/usr/bin/codesign", "--verify", "--deep", "--strict",
            staged.value()})) {
    base::DeletePathRecursively(staged);
    return std::nullopt;
  }
  // The dmg left quarantine (ADR 0002); a copy must not carry it either.
  std::string output;
  base::GetAppOutputAndError(
      {"/usr/bin/xattr", "-dr", "com.apple.quarantine", staged.value()},
      &output);
  return staged;
}

bool UpdateInstaller::Install(const base::FilePath& staged,
                              const base::FilePath& dmg,
                              const std::optional<Relaunch>& relaunch) const {
  if (renamex_np(bundle_.value().c_str(), staged.value().c_str(),
                 RENAME_SWAP) != 0) {
    PLOG(ERROR) << "The update's swap failed";
    return false;
  }

  // $1 Pane's process, $2 the old bundle (where the staged one was), $3 the
  // dmg, $4 whether to open the new Pane, then `open`'s arguments. The new
  // Pane opens first, and what it replaced goes after: its window comes 60 ms
  // sooner.
  static constexpr char kScript[] =
      "pid=$1 old=$2 dmg=$3 relaunch=$4; shift 4; "
      "while kill -0 \"$pid\" 2>/dev/null; do sleep 0.05; done; "
      "[ \"$relaunch\" = 1 ] && /usr/bin/open \"$@\"; "
      "rm -rf \"$old\"; [ -n \"$dmg\" ] && rm -f \"$dmg\"; exit 0";
  std::vector<std::string> argv = {"/bin/sh",
                                   "-c",
                                   kScript,
                                   "pane-update",
                                   base::NumberToString(getpid()),
                                   staged.value(),
                                   dmg.value(),
                                   relaunch ? "1" : "0"};
  if (relaunch) {
    const std::vector<std::string> open = OpenArguments(bundle_, *relaunch);
    argv.insert(argv.end(), open.begin(), open.end());
  }
  // Apart from Pane's process group, as the volume's watchdog is: a terminal
  // that closes takes no part of the install with it. It keeps none of
  // Pane's files open (posix_spawn closes them), the data's lock included.
  base::LaunchOptions options;
  options.new_process_group = true;
  if (!base::LaunchProcess(argv, options).IsValid()) {
    LOG(ERROR) << "The update's last step did not start";
  }
  return true;
}

void UpdateInstaller::RemoveLeftovers() const {
  base::FileEnumerator beside(
      bundle_.DirName(), /*recursive=*/false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = beside.Next(); !path.empty();
       path = beside.Next()) {
    if (model::IsUpdateLeftover(path.BaseName().value())) {
      base::DeletePathRecursively(path);
    }
  }

  base::FilePath temporary;
  if (!base::GetTempDir(&temporary)) {
    return;
  }
  const auto running = [](int32_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
  };
  base::FileEnumerator mounts(temporary, /*recursive=*/false,
                              base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = mounts.Next(); !path.empty();
       path = mounts.Next()) {
    if (model::IsLeftoverUpdateMount(path.BaseName().value(), running)) {
      Unmount(path, /*quietly=*/true);
    }
  }
}

}  // namespace pane
