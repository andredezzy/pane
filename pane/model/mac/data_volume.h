// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_DATA_VOLUME_H_
#define PANE_MODEL_MAC_DATA_VOLUME_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/process/process.h"
#include "base/types/expected.h"

namespace pane::model {

// Why the volume would not attach, open, or take a profile.
struct VolumeError {
  enum class Kind {
    // A disk image tool that failed, with what it said.
    kTool,
    // A volume in the image that is not encrypted, refused (DataVolume::Open).
    kNotEncrypted,
    // A file or folder the disk would not take.
    kFile,
  };

  static VolumeError Tool(std::string command, int status, std::string output);
  static VolumeError NotEncrypted(std::string volume);
  static VolumeError File(std::string what);

  // "<command> exited with <status>: <output>", "the volume <volume> is not
  // encrypted", or the file and why.
  std::string ToString() const;

  Kind kind = Kind::kTool;
  // The tool and its first two arguments, the volume refused, or the file and
  // why.
  std::string what;
  // The tool's exit status.
  int status = 0;
  // What the tool wrote, its errors first.
  std::string output;
};

// The encrypted volume that holds every profile's engine folder and caches
// (ADR 0113): a sparse bundle in the data root, a terabyte at most and
// sparse, whose APFS volume is encrypted with the passphrase the data key
// derives, as FileVault encrypts one: in the kernel, its files' contents and
// names alike. The image attaches before the key opens, its volume locked;
// once the key opens, the volume unlocks and mounts at Engine/Volume, out of
// the Finder. Engine/<profile id>, and the profile's caches' folder when the
// caches live apart, are links into it. The mount point is inside the
// engine's user data dir because the network service's sandbox allows only
// that and the caches' folder, by their resolved paths
// (GetNetworkContextsParentDirectory): a volume beside it would leave the
// cookies and the HTTP cache unopened. The mount point is read-only, so while
// the volume is not mounted the links lead nowhere and nothing written
// through them lands in the clear.
//
// Every call blocks on the disk, and those that run a tool or mount wait for
// it: they belong on the volume's sequence (MayBlock, WithBaseSyncPrimitives).
class DataVolume {
 public:
  // Makes and attaches images with hdiutil, as on the macOS versions before
  // 26, on any macOS: the tests run that path on this one too.
  static void UseHdiutilForTesting(bool use);

  // An attached image's devices: its own (/dev/diskN), which detaches it
  // whole, and its encrypted volume's.
  struct Devices {
    friend bool operator==(const Devices&, const Devices&) = default;

    std::string image;
    std::string volume;
  };

  // What the first opens after a migration do with its safety copy.
  enum class SafetyCopyStep {
    kNone,
    // Two opens in a row never came up: back to the copy, and migrate again.
    kRestore,
  };

  // `cache_directory` holds the profiles' caches when they live apart (under
  // ~/Library/Caches, PanicWipe's CacheDirectoryFor); nullopt beside them.
  DataVolume(const base::FilePath& data_directory,
             std::optional<base::FilePath> cache_directory);
  DataVolume(const DataVolume&);
  DataVolume& operator=(const DataVolume&);
  ~DataVolume();

  const base::FilePath& image() const { return image_; }
  const base::FilePath& mount_point() const { return mount_point_; }

  bool Exists() const;
  bool IsMounted() const;

  // Attaching

  // Attaches the image, its volume locked, or makes it the first time, or
  // finishes one a quit cut short while making it, which has no volume and so
  // holds nothing. Made, it has no file system, an APFS container on it, and
  // in that one volume, born encrypted with the passphrase, which diskutil
  // reads from stdin: no plain volume is ever made to be mounted in its place.
  // 1.2 s, where hdiutil's image with a plain volume, removed once the
  // encrypted one was added beside it, took 3.5.
  base::expected<Devices, VolumeError> AttachOrMake(
      std::string_view passphrase) const;

  // The image attached once, its volume locked. One with no volume yet, whose
  // making a quit cut short, stays attached for AttachOrMake to finish.
  base::expected<Devices, VolumeError> AttachImageOnce() const;

  // Unlocks the attached image's volume with the passphrase and mounts it,
  // and keeps it only if it is encrypted: a plain volume put in its place
  // unlocks with nothing and would take the profiles in the clear, so its
  // image detaches before anything is written. One left unlocked mounts as it
  // is, and is checked the same.
  //
  // A volume an earlier Pane made still takes `former` (ADR 0121): it unlocks
  // with that once, and changes to `passphrase` before anything converts, so
  // that Pane no longer opens what this one will encrypt. The change is one
  // call to APFS, so a kill leaves one passphrase or the other, and each is
  // tried.
  base::expected<void, VolumeError> Open(
      const Devices& devices,
      std::string_view passphrase,
      std::optional<std::string_view> former = std::nullopt) const;

  // At the mount point, out of the Finder (nobrowse), through Disk
  // Arbitration in this process: 50 ms, where diskutil takes 200 to start. One
  // a lock could not detach is still there.
  base::expected<void, VolumeError> Mount(const Devices& devices) const;

  // By the image's device, which detaches it whole.
  base::expected<void, VolumeError> Detach(std::string_view device,
                                           bool force) const;

  // Every attachment of this image hdiutil lists, by its device.
  void DetachLeftovers() const;

  // Whether `device` still holds this image. Detached, its number goes to the
  // next disk attached, so one remembered from before is checked first
  // (DataVault).
  bool Holds(std::string_view device) const;

  // Runs `helper` (pane-volume-watch), which detaches this image, on
  // whichever device it then has, once this process ends, a crash included.
  // Its process, for a quit that detached to end it.
  base::expected<base::Process, VolumeError> Watch(
      const base::FilePath& helper) const;

  // Profiles

  // A profile's folder in the volume: Profile, the engine's, and Cache.
  base::FilePath Folder(std::string_view profile_id) const;

  // A profile made without a tab has none until it first opens.
  bool HasFolder(std::string_view profile_id) const;

  // Makes the profile's folders in the attached volume and the links to them,
  // before the engine opens the profile. A plain folder still in their place
  // moves in first. One that saves no history loses what it wrote while it
  // did (ADR 0119): the leveldb log of its downloads, which a deletion does
  // not rewrite, beside which is only Chrome's in-product help, which Pane
  // does not show; and its caches, which it now keeps in memory.
  base::expected<void, VolumeError> Prepare(std::string_view profile_id,
                                            bool saves_history = true) const;

  // Encrypting a profile's files (ADR 0121)

  // Whether the profile's files are all in their encrypted formats: an empty
  // file beside them, in the volume, so it says what the volume holds.
  bool FilesAreEncrypted(std::string_view profile_id) const;

  // Encrypted by this Pane, so nothing of an earlier one's is there to put
  // right.
  void MarkFilesEncrypted(std::string_view profile_id) const;

  // Whether what a migration before 0.18.2 did to the profile has been put
  // right.
  bool FilesAreRepaired(std::string_view profile_id) const;
  void MarkFilesRepaired(std::string_view profile_id) const;

  // A profile is about to open. Once it has come up, it never goes back: the
  // copy is older than everything written since. An open that a kill ended
  // counts; one that Pane closed, by a quit or a lock, does not
  // (SafetyCopiesClosed).
  SafetyCopyStep SafetyCopyWillOpen(std::string_view profile_id) const;

  // A copy restored: the profile migrates again, with its counts from the
  // start. The files are plain, so their marker goes first: a kill here must
  // not leave them marked encrypted, which would stop the process at every
  // open.
  void SafetyCopyDidRestore(std::string_view profile_id) const;

  // The profile came up: nothing counts against it any more. From the third
  // time in a row, its copy goes, once the repair that reads its listing has
  // run.
  void SafetyCopyDidOpen(std::string_view profile_id) const;

  // Pane closes the profiles itself, for a quit or a lock: the opens under way
  // failed at nothing.
  void SafetyCopiesClosed() const;

  // Moves the plain profile folders an earlier Pane left (Engine/<id>) into
  // the attached volume, each behind its link, and stages them, and their
  // caches, which rebuild, for the panic wipe's erase (EraseStagedWipes).
  // `progress` gets the share copied, 0 to 1. Each folder is staged only once
  // its copy is whole, so one cut short is copied again.
  base::expected<void, VolumeError> MoveIn(
      const std::vector<std::string>& profile_ids,
      base::FunctionRef<void(double share)> progress) const;

  // Whether an earlier Pane's plain folder is still in the profile's place.
  bool HasPlainFolder(std::string_view profile_id) const;

  // Whether Engine holds any profile's plain folder: a profile's is named by
  // its id, a UUID, where the engine's own are not (Default, and the rest).
  bool HasPlainProfiles() const;

  // The profile's links, its plain folders if an earlier Pane left any, and
  // its folder in the attached volume.
  base::expected<void, VolumeError> Remove(std::string_view profile_id) const;

 private:
  // An attached image's devices: its own, and its APFS container's and
  // volume's, once made.
  struct Attachment;

  base::expected<void, VolumeError> MakeImage() const;
  base::expected<std::string, VolumeError> MakeContainer(
      const std::string& image,
      bool made) const;
  base::expected<void, VolumeError> Unlock(const std::string& volume,
                                           std::string_view passphrase) const;
  base::expected<Attachment, VolumeError> AttachOnce() const;
  base::expected<std::string, VolumeError> ContainerOf(
      const std::string& volume) const;
  base::expected<std::string, VolumeError> VolumeNamed(
      std::string_view name,
      const std::string& container) const;
  base::expected<void, VolumeError> ChangePassphrase(
      const Devices& devices,
      std::string_view old_passphrase,
      std::string_view new_passphrase) const;
  void StopFileEventsLog() const;
  std::vector<std::string> DevicesHoldingImage() const;
  base::expected<void, VolumeError> PrepareMountPoint() const;

  base::FilePath EngineFolder(std::string_view profile_id) const;
  std::optional<base::FilePath> CacheFolder(std::string_view profile_id) const;
  base::expected<void, VolumeError> Stage(
      const base::FilePath& path,
      const std::optional<base::FilePath>& root,
      std::string_view id) const;

  base::FilePath data_directory_;
  std::optional<base::FilePath> cache_directory_;
  base::FilePath image_;
  base::FilePath mount_point_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_DATA_VOLUME_H_
