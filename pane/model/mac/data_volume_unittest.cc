// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/data_volume.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/panic_wipe.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Ported from PaneKit's DataVolumeTests, case by case: the volume's links and
// moves, on a plain folder standing in for the attached volume, and two that
// attach a real image.

// The target `path` leads to, or nullopt when it is no link.
std::optional<std::string> LinkAt(const base::FilePath& path) {
  base::FilePath target;
  return base::ReadSymbolicLink(path, &target)
             ? std::make_optional(target.value())
             : std::nullopt;
}

std::string ReadText(const base::FilePath& path) {
  std::string text;
  base::ReadFileToString(path, &text);
  return text;
}

class DataVolumeTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  // Detaches only this test's own image, found by its path, then lets the
  // next test that attaches one go on.
  void TearDown() override {
    if (attached_) {
      attached_->DetachLeftovers();
      EXPECT_FALSE(attached_->IsMounted());
    }
  }

  base::FilePath Root() const { return directory_.GetPath(); }

  // One at a time, across the test launcher's processes: these attach real
  // images, and a cleanup that detached by device number would take another
  // test's image, given the same number a moment later. A lock on a file of
  // its own in the temporary folder, held until the test ends.
  void SerializeImages() {
    base::FilePath temp;
    ASSERT_TRUE(base::GetTempDir(&temp));
    images_lock_.reset(HANDLE_EINTR(
        open(temp.Append("pane_unittests-data-volume.lock").value().c_str(),
             O_RDWR | O_CREAT | O_CLOEXEC, 0600)));
    ASSERT_TRUE(images_lock_.is_valid());
    ASSERT_EQ(HANDLE_EINTR(flock(images_lock_.get(), LOCK_EX)), 0);
  }

  // The volume whose image the test attaches, so the teardown detaches it.
  void WillAttach(const DataVolume& volume) { attached_ = volume; }

  // A profile with a safety copy, in a volume made for the test.
  DataVolume VolumeWithSafetyCopy() {
    DataVolume volume(Root(), std::nullopt);
    EXPECT_TRUE(volume.Prepare("p").has_value());
    EXPECT_TRUE(base::WriteFile(Copy(volume), "copy"));
    return volume;
  }

  static base::FilePath Copy(const DataVolume& volume) {
    return volume.Folder("p").Append("SafetyCopy");
  }

 private:
  base::ScopedTempDir directory_;
  base::ScopedFD images_lock_;
  std::optional<DataVolume> attached_;
};

using Step = DataVolume::SafetyCopyStep;

// A profile's files count as encrypted once marked, beside them in the volume.
TEST_F(DataVolumeTest, EncryptedMarker) {
  const DataVolume volume(Root(), std::nullopt);
  ASSERT_TRUE(volume.Prepare("p").has_value());

  EXPECT_FALSE(volume.FilesAreEncrypted("p"));
  volume.MarkFilesEncrypted("p");
  EXPECT_TRUE(volume.FilesAreEncrypted("p"));
  EXPECT_FALSE(volume.FilesAreEncrypted("other"));
}

// A safety copy goes from the third open that comes up, once the repair that
// reads it has run.
TEST_F(DataVolumeTest, SafetyCopyGoes) {
  const DataVolume volume = VolumeWithSafetyCopy();
  const base::FilePath folder = volume.Folder("p");

  for (int open = 1; open <= 4; ++open) {
    EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone) << "open " << open;
    volume.SafetyCopyDidOpen("p");
  }

  EXPECT_TRUE(base::PathExists(Copy(volume)));
  EXPECT_FALSE(volume.FilesAreRepaired("p"));

  volume.MarkFilesRepaired("p");
  volume.SafetyCopyDidOpen("p");
  EXPECT_FALSE(base::PathExists(Copy(volume)));
  EXPECT_FALSE(base::PathExists(folder.Append(".safety-opened-3")));
  EXPECT_TRUE(volume.FilesAreRepaired("p"));
}

// A profile this Pane encrypted has nothing to repair.
TEST_F(DataVolumeTest, NothingToRepair) {
  const DataVolume volume = VolumeWithSafetyCopy();

  EXPECT_FALSE(volume.FilesAreRepaired("p"));
  volume.MarkFilesEncrypted("p");
  EXPECT_TRUE(volume.FilesAreRepaired("p"));
}

// No safety copy, nothing to count.
TEST_F(DataVolumeTest, NoSafetyCopy) {
  const DataVolume volume(Root(), std::nullopt);
  ASSERT_TRUE(volume.Prepare("p").has_value());

  for (int open = 1; open <= 3; ++open) {
    EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  }
}

// Two opens killed before any came up restore the copy, once.
TEST_F(DataVolumeTest, SafetyCopyRestores) {
  const DataVolume volume = VolumeWithSafetyCopy();
  volume.MarkFilesEncrypted("p");

  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kRestore);
  volume.SafetyCopyDidRestore("p");
  EXPECT_FALSE(volume.FilesAreEncrypted("p"));

  // After that the copy stays, whatever comes, and nothing restores again.
  for (int open = 1; open <= 3; ++open) {
    EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  }

  EXPECT_TRUE(base::PathExists(Copy(volume)));
}

// A profile that came up once never goes back to its copy, however many opens
// are killed after.
TEST_F(DataVolumeTest, SafetyCopyAfterAGoodOpen) {
  const DataVolume volume = VolumeWithSafetyCopy();

  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  volume.SafetyCopyDidOpen("p");

  for (int open = 1; open <= 5; ++open) {
    EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone) << "open " << open;
  }
}

// A quit or a lock inside an open is no failure: only a kill counts.
TEST_F(DataVolumeTest, SafetyCopyAfterAQuit) {
  const DataVolume volume = VolumeWithSafetyCopy();

  for (int open = 1; open <= 5; ++open) {
    EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone) << "open " << open;
    volume.SafetyCopiesClosed();
  }

  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kRestore);
}

// What 0.18.0's opens left counts for nothing: it counted quits as kills.
TEST_F(DataVolumeTest, SafetyCopyFromAnEarlierPane) {
  const DataVolume volume = VolumeWithSafetyCopy();
  const base::FilePath folder = volume.Folder("p");

  for (std::string_view marker : {".safety-opening", ".safety-failed"}) {
    ASSERT_TRUE(base::WriteFile(folder.Append(marker), ""));
  }

  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_FALSE(base::PathExists(folder.Append(".safety-opening")));
  EXPECT_FALSE(base::PathExists(folder.Append(".safety-failed")));
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kNone);
  EXPECT_EQ(volume.SafetyCopyWillOpen("p"), Step::kRestore);
}

// A profile has a folder only once it has been prepared.
TEST_F(DataVolumeTest, HasFolder) {
  const DataVolume volume(Root(), std::nullopt);

  EXPECT_FALSE(volume.HasFolder("p"));
  ASSERT_TRUE(volume.Prepare("p").has_value());
  EXPECT_TRUE(volume.HasFolder("p"));
}

// A volume a lock could not detach opens as it is.
TEST_F(DataVolumeTest, OpenTwice) {
  SerializeImages();
  const DataVolume volume(Root(), std::nullopt);
  WillAttach(volume);
  const base::expected<DataVolume::Devices, VolumeError> devices =
      volume.AttachOrMake("pass");
  ASSERT_TRUE(devices.has_value()) << devices.error().ToString();

  const base::expected<void, VolumeError> opened =
      volume.Open(*devices, "pass");
  ASSERT_TRUE(opened.has_value()) << opened.error().ToString();
  EXPECT_TRUE(volume.IsMounted());
  const base::expected<void, VolumeError> again = volume.Open(*devices, "pass");
  ASSERT_TRUE(again.has_value()) << again.error().ToString();
  EXPECT_TRUE(volume.IsMounted());
}

// A passphrase that will not change stops the open: nothing converts while
// the former one still opens the volume.
TEST_F(DataVolumeTest, FormerPassphraseThatWillNotChange) {
  SerializeImages();
  const DataVolume volume(Root(), std::nullopt);
  WillAttach(volume);
  const base::expected<DataVolume::Devices, VolumeError> devices =
      volume.AttachOrMake("former");
  ASSERT_TRUE(devices.has_value()) << devices.error().ToString();

  // APFS refuses an empty passphrase, which fails the change.
  const base::expected<void, VolumeError> opened =
      volume.Open(*devices, "", "former");
  ASSERT_FALSE(opened.has_value());
  EXPECT_EQ(opened.error().kind, VolumeError::Kind::kTool)
      << opened.error().ToString();
  EXPECT_FALSE(volume.IsMounted());
}

// A profile's engine folder and caches are links into the volume.
TEST_F(DataVolumeTest, Prepare) {
  const base::FilePath data = Root().Append("data");
  const base::FilePath caches = Root().Append("caches");
  const DataVolume volume(data, caches);

  ASSERT_TRUE(volume.Prepare("p").has_value());
  // Twice changes nothing.
  ASSERT_TRUE(volume.Prepare("p").has_value());
  ASSERT_TRUE(base::WriteFile(
      data.Append("Engine").Append("p").Append("Cookies"), "cookie"));

  EXPECT_EQ(LinkAt(data.Append("Engine").Append("p")),
            volume.Folder("p").Append("Profile").value());
  EXPECT_EQ(LinkAt(caches.Append("Engine").Append("p")),
            volume.Folder("p").Append("Cache").value());
  EXPECT_TRUE(
      base::PathExists(volume.Folder("p").Append("Profile").Append("Cookies")));
  EXPECT_FALSE(volume.IsMounted());
}

// A profile that saves no history opens without its earlier downloads' log or
// caches.
TEST_F(DataVolumeTest, DownloadCache) {
  const base::FilePath data = Root().Append("data");
  const DataVolume volume(data, std::nullopt);

  ASSERT_TRUE(volume.Prepare("p").has_value());

  const base::FilePath folder = volume.Folder("p");
  std::vector<base::FilePath> written;
  for (std::string_view item :
       {"Profile/shared_proto_db", "Profile/Cache", "Profile/Code Cache",
        "Cache/Cache", "Cache/Code Cache"}) {
    written.push_back(folder.Append(item));
  }
  const base::FilePath kept = folder.Append("Profile").Append("Local Storage");

  for (const base::FilePath& item : written) {
    ASSERT_TRUE(base::CreateDirectory(item));
  }
  ASSERT_TRUE(base::CreateDirectory(kept));

  ASSERT_TRUE(volume.Prepare("p", /*saves_history=*/true).has_value());
  EXPECT_TRUE(std::ranges::all_of(written, &base::PathExists));

  ASSERT_TRUE(volume.Prepare("p", /*saves_history=*/false).has_value());
  EXPECT_TRUE(std::ranges::none_of(written, &base::PathExists));
  EXPECT_TRUE(base::PathExists(kept));
}

// A link to somewhere else is replaced.
TEST_F(DataVolumeTest, Relink) {
  const base::FilePath data = Root().Append("data");
  const DataVolume volume(data, std::nullopt);

  ASSERT_TRUE(base::CreateDirectory(data.Append("Engine")));
  ASSERT_TRUE(base::CreateSymbolicLink(Root().Append("elsewhere"),
                                       data.Append("Engine").Append("p")));
  ASSERT_TRUE(volume.Prepare("p").has_value());

  EXPECT_EQ(LinkAt(data.Append("Engine").Append("p")),
            volume.Folder("p").Append("Profile").value());
}

// An earlier Pane's plain folders move in, whole, and wait for the wipe's
// erase.
TEST_F(DataVolumeTest, MoveIn) {
  const base::FilePath data = Root().Append("data");
  const base::FilePath caches = Root().Append("caches");
  const DataVolume volume(data, caches);
  const base::FilePath plain = data.Append("Engine").Append("p");

  const std::string log(300'000, '\x07');
  ASSERT_TRUE(
      base::CreateDirectory(plain.Append("Local Storage").Append("leveldb")));
  ASSERT_TRUE(base::WriteFile(
      plain.Append("Local Storage").Append("leveldb").Append("000003.log"),
      log));
  ASSERT_TRUE(base::WriteFile(plain.Append("Cookies"), "cookie"));
  ASSERT_TRUE(base::CreateSymbolicLink(base::FilePath("12345"),
                                       plain.Append("SingletonCookie")));
  const base::FilePath cache = caches.Append("Engine").Append("p");
  ASSERT_TRUE(base::CreateDirectory(cache.Append("Cache")));
  ASSERT_TRUE(base::WriteFile(cache.Append("Cache").Append("entry"), "page"));

  std::vector<double> shares;
  ASSERT_TRUE(volume
                  .MoveIn({"p", "never-opened"},
                          [&](double share) { shares.push_back(share); })
                  .has_value());

  const base::FilePath moved = volume.Folder("p").Append("Profile");
  EXPECT_EQ(
      ReadText(
          moved.Append("Local Storage").Append("leveldb").Append("000003.log")),
      log);
  EXPECT_EQ(ReadText(moved.Append("Cookies")), "cookie");
  EXPECT_EQ(LinkAt(moved.Append("SingletonCookie")), "12345");
  EXPECT_EQ(LinkAt(plain), moved.value());
  ASSERT_FALSE(shares.empty());
  EXPECT_EQ(shares.back(), 1);
  EXPECT_TRUE(std::ranges::is_sorted(shares));
  EXPECT_FALSE(base::PathExists(cache) || base::IsLink(cache));

  // Staged for the erase, data and caches alike, which then takes them.
  std::vector<int> passes;
  for (const StagedWipe& staged : StagedWipes(data, caches)) {
    passes.push_back(staged.passes);
  }
  std::ranges::sort(passes);
  EXPECT_EQ(passes, (std::vector<int>{1, 3}));
  EXPECT_TRUE(EraseStagedWipes(data, caches).empty());
  EXPECT_TRUE(StagedWipes(data, caches).empty());

  // The caches' link comes with the profile's next prepare.
  ASSERT_TRUE(volume.Prepare("p").has_value());
  EXPECT_EQ(LinkAt(caches.Append("Engine").Append("p")),
            volume.Folder("p").Append("Cache").value());
}

// A move cut short before its stage copies the folder again.
TEST_F(DataVolumeTest, MoveAgain) {
  const base::FilePath data = Root().Append("data");
  const DataVolume volume(data, std::nullopt);
  const base::FilePath plain = data.Append("Engine").Append("p");

  ASSERT_TRUE(base::CreateDirectory(plain));
  ASSERT_TRUE(base::WriteFile(plain.Append("Cookies"), "whole"));
  // What a copy cut short left.
  ASSERT_TRUE(base::CreateDirectory(volume.Folder("p").Append("Profile")));
  ASSERT_TRUE(base::WriteFile(
      volume.Folder("p").Append("Profile").Append("Partial"), "half"));

  ASSERT_TRUE(volume.MoveIn({"p"}, [](double) {}).has_value());

  EXPECT_EQ(ReadText(volume.Folder("p").Append("Profile").Append("Cookies")),
            "whole");
  EXPECT_FALSE(
      base::PathExists(volume.Folder("p").Append("Profile").Append("Partial")));
}

// An earlier engine's database of the pages visited, and its histograms, are
// staged for the erase.
TEST_F(DataVolumeTest, EngineTraces) {
  const base::FilePath engine = Root().Append("Engine");
  const base::FilePath traces =
      engine.Append("segmentation_platform").Append("ukm_db");
  ASSERT_TRUE(base::CreateDirectory(traces));
  ASSERT_TRUE(base::WriteFile(traces.Append("000003.log"),
                              "https://mail.example.com/"));
  const base::FilePath metrics = engine.Append("BrowserMetrics");
  ASSERT_TRUE(base::CreateDirectory(metrics));
  ASSERT_TRUE(
      base::WriteFile(metrics.Append("BrowserMetrics-6A1B2C3D-6C80.pma"),
                      std::string(4096, '\0')));
  ASSERT_TRUE(base::WriteFile(engine.Append("BrowserMetrics-spare.pma"),
                              std::string(4096, '\0')));
  ASSERT_TRUE(base::CreateDirectory(engine.Append("GraphiteDawnCache")));

  EXPECT_TRUE(StageEngineTraces(Root()).empty());

  for (std::string_view trace : {"segmentation_platform", "BrowserMetrics",
                                 "BrowserMetrics-spare.pma"}) {
    EXPECT_FALSE(base::PathExists(engine.Append(trace))) << trace << " stayed";
  }

  // The GPU's shader caches are not the user's, and stay.
  EXPECT_TRUE(base::PathExists(engine.Append("GraphiteDawnCache")));
  EXPECT_EQ(StagedWipes(Root(), std::nullopt).size(), 1u);
  EXPECT_TRUE(EraseStagedWipes(Root(), std::nullopt).empty());
  // Nothing to stage the next time.
  EXPECT_TRUE(StageEngineTraces(Root()).empty());
}

// The wipe erases the volume's image without passes, and the key's copy
// before security.json.
TEST_F(DataVolumeTest, Wipe) {
  const base::FilePath data("/tmp/pane");
  std::vector<std::string> targets;
  for (const base::FilePath& target : WipeTargets(data, std::nullopt)) {
    targets.push_back(target.BaseName().value());
  }

  EXPECT_TRUE(std::ranges::contains(targets, "Data.sparsebundle"));
  ASSERT_GE(targets.size(), 2u);
  EXPECT_EQ(targets[targets.size() - 2], "Data.security.json");
  EXPECT_EQ(targets.back(), "security.json");
}

// Not in PaneKit: an id that isn't one folder ("..", which names the data
// root from Engine) is refused before anything is removed, prepared, or
// moved in.
TEST_F(DataVolumeTest, OutsideItsFolders) {
  const DataVolume volume(Root(), std::nullopt);
  ASSERT_TRUE(volume.Prepare("p").has_value());
  ASSERT_TRUE(base::WriteFile(Root().Append("kept"), "kept"));

  for (std::string_view id : {"..", ".", "", "a/b", "a\\b", "a:b"}) {
    EXPECT_FALSE(volume.Remove(id).has_value()) << id;
    EXPECT_FALSE(volume.Prepare(id).has_value()) << id;
    EXPECT_FALSE(volume.MoveIn({std::string(id)}, [](double) {}).has_value())
        << id;
  }
  EXPECT_TRUE(base::PathExists(Root().Append("kept")));
  EXPECT_TRUE(volume.HasFolder("p"));
}

// Not in PaneKit: a plain folder holding one the move can't read stays where
// it is, whole, never staged for the erase: FileManager's enumerator passed
// over such a folder, and the erase then took what was never copied.
TEST_F(DataVolumeTest, MoveInStopsAtWhatItCantRead) {
  const base::FilePath data = Root().Append("data");
  const DataVolume volume(data, std::nullopt);
  const base::FilePath plain = data.Append("Engine").Append("p");
  const base::FilePath unreadable = plain.Append("Locked");
  ASSERT_TRUE(base::CreateDirectory(unreadable));
  ASSERT_TRUE(base::WriteFile(unreadable.Append("only-copy"), "kept"));
  ASSERT_TRUE(base::WriteFile(plain.Append("Cookies"), "cookie"));
  ASSERT_EQ(chmod(unreadable.value().c_str(), 0), 0);
  // Readable again whatever happens, so the temporary folder goes.
  base::ScopedClosureRunner readable(base::BindOnce(
      [](const base::FilePath& folder) { chmod(folder.value().c_str(), 0700); },
      unreadable));

  EXPECT_FALSE(volume.MoveIn({"p"}, [](double) {}).has_value());

  readable.RunAndReset();
  EXPECT_TRUE(IsPlainFolder(plain));
  EXPECT_EQ(ReadText(unreadable.Append("only-copy")), "kept");
  EXPECT_EQ(ReadText(plain.Append("Cookies")), "cookie");
  EXPECT_TRUE(StagedWipes(data, std::nullopt).empty());
}

}  // namespace
}  // namespace pane::model
