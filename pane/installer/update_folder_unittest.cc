// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/installer/update_folder.h"

#include <stdint.h>

#include <array>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/version.h"
#include "pane/installer/files.h"
#include "pane/installer/payload.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::installer {

namespace {

// A payload as package-win.sh compresses one (7-Zip's BCJ2 and LZMA, each
// file apart), of a stand-in app folder at 99.0.0: chrome.exe,
// pane_setup.exe, pane-version, and locales\en-US.pak, each a line of
// text.
constexpr std::array<uint8_t, 422> kPayload = {
    0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x04, 0x22, 0xFF, 0x11, 0x30,
    0x63, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1C, 0x55, 0xC0, 0xB1, 0x00, 0x31, 0x9A, 0x0A,
    0xAB, 0xB6, 0x82, 0x24, 0x4B, 0xD4, 0xFA, 0xB5, 0x58, 0x44, 0x9B, 0x30,
    0x06, 0x33, 0x67, 0xFC, 0x9A, 0x6F, 0x7A, 0x08, 0xD3, 0xB2, 0x15, 0xD3,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x88, 0x09, 0x87, 0x33, 0x9F, 0xCA, 0x71,
    0x3B, 0x87, 0x23, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0xE0, 0xB4, 0x60,
    0x2F, 0xC1, 0xC1, 0xB9, 0xB6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x18,
    0x4A, 0x21, 0x0C, 0xAC, 0x09, 0xB7, 0x18, 0x81, 0x86, 0xEF, 0x65, 0x92,
    0x4F, 0xBD, 0x96, 0x3A, 0x85, 0xC1, 0xDB, 0xC2, 0x0E, 0x39, 0x8C, 0xA4,
    0x94, 0x83, 0x3F, 0x10, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x33,
    0x07, 0xAF, 0xB6, 0x4C, 0xF3, 0xD2, 0x5E, 0xFB, 0xBB, 0x07, 0x20, 0x66,
    0x67, 0x2A, 0x23, 0x79, 0x4E, 0x6E, 0xEF, 0x1B, 0xEA, 0x6B, 0x6E, 0x21,
    0xFF, 0x51, 0x72, 0x95, 0xE2, 0xDE, 0xEE, 0x9E, 0x71, 0xD4, 0x3F, 0x17,
    0x71, 0x07, 0xC8, 0x6C, 0x96, 0x55, 0xCE, 0x7B, 0x6F, 0x77, 0x8D, 0x33,
    0x50, 0x8D, 0x1F, 0x0D, 0x96, 0x3F, 0x62, 0x94, 0xD9, 0xB5, 0x53, 0xFE,
    0xC6, 0xF3, 0xA6, 0x6A, 0x39, 0xF0, 0x86, 0x3B, 0x75, 0x2F, 0xF9, 0x11,
    0xB8, 0x6D, 0xD2, 0xAB, 0xA3, 0x58, 0x01, 0x1E, 0x22, 0x01, 0x0D, 0x99,
    0x4F, 0xDA, 0x3D, 0x05, 0x1D, 0x17, 0x9C, 0x80, 0x5A, 0xD3, 0x21, 0xCC,
    0xD1, 0xE3, 0xBC, 0xD3, 0x16, 0x37, 0x3A, 0xC1, 0x1E, 0x27, 0x3D, 0xAE,
    0xC0, 0xE4, 0x7A, 0xD9, 0x6F, 0x56, 0xA2, 0xCE, 0xA3, 0x93, 0x2B, 0x4A,
    0x65, 0xEC, 0xF0, 0xE3, 0x87, 0x56, 0x58, 0x80, 0x3C, 0xA0, 0xED, 0x2E,
    0x66, 0xBA, 0x7E, 0xD8, 0xE4, 0x04, 0x02, 0xAA, 0x79, 0x67, 0xCB, 0xCC,
    0x24, 0x59, 0x7B, 0x49, 0x13, 0x71, 0xD2, 0x0F, 0x4A, 0x4A, 0x8C, 0x6C,
    0x45, 0x5B, 0x72, 0x0D, 0x72, 0x24, 0xF5, 0x9F, 0xD8, 0x4F, 0xE7, 0x4B,
    0x9A, 0x5E, 0xA5, 0x6A, 0x2C, 0x93, 0xC3, 0x21, 0x06, 0x28, 0x9F, 0x6B,
    0x46, 0x9A, 0x75, 0xCB, 0x38, 0x7F, 0x2B, 0x54, 0xE5, 0xD0, 0xDB, 0x28,
    0x86, 0xEE, 0x6E, 0x01, 0x09, 0xAB, 0x05, 0xFC, 0xA2, 0xCF, 0xC6, 0x79,
    0x00, 0x00, 0x00, 0x17, 0x06, 0x80, 0x90, 0x01, 0x09, 0x80, 0xD3, 0x00,
    0x07, 0x0B, 0x01, 0x00, 0x01, 0x23, 0x03, 0x01, 0x01, 0x05, 0x5D, 0x00,
    0x10, 0x00, 0x00, 0x0C, 0x81, 0xDA, 0x0A, 0x01, 0xAF, 0x1F, 0x0F, 0x41,
    0x00, 0x00,
};

// An installer: a program's bytes, then `kPayload`, then the trailer naming
// it as `version` (package-win.sh's layout).
base::FilePath WriteInstaller(const base::FilePath& folder,
                              const std::string& version) {
  std::vector<uint8_t> bytes(4096, 0x90);
  bytes[0] = 'M';
  bytes[1] = 'Z';
  Payload payload;
  payload.offset = bytes.size();
  payload.size = kPayload.size();
  payload.unpacked_size = 62;
  payload.version = version;
  bytes.insert(bytes.end(), kPayload.begin(), kPayload.end());
  const auto trailer = TrailerFor(payload);
  bytes.insert(bytes.end(), trailer.begin(), trailer.end());
  const base::FilePath installer =
      folder.AppendASCII("Pane-" + version + "-x64.exe");
  EXPECT_TRUE(base::WriteFile(installer, bytes));
  return installer;
}

// A whole version's folder: its pane-version, chrome.exe, and helper.
void WriteStaged(const base::FilePath& folder, const std::string& version) {
  ASSERT_TRUE(base::CreateDirectory(folder));
  ASSERT_TRUE(base::WriteFile(folder.Append(L"pane-version"), version));
  ASSERT_TRUE(base::WriteFile(folder.Append(L"chrome.exe"), "chrome"));
  ASSERT_TRUE(base::WriteFile(folder.Append(L"pane_setup.exe"), "helper"));
}

std::string Contents(const base::FilePath& file) {
  std::string contents;
  base::ReadFileToString(file, &contents);
  return contents;
}

}  // namespace

// The update folder beside the app folder, and its names (ADR 0127).
TEST(UpdateFolderTest, BesideTheAppFolder) {
  const base::FilePath app(
      L"C:\\Users\\u\\AppData\\Local\\Programs\\Pane\\Application");
  const base::FilePath update = UpdateFolderOf(app);
  EXPECT_EQ(update.value(),
            L"C:\\Users\\u\\AppData\\Local\\Programs\\Pane\\Update");
  EXPECT_EQ(StagedFolder(update, "0.21.0").BaseName().value(), L"0.21.0");
  EXPECT_EQ(PartialFolder(update, "0.21.0").BaseName().value(),
            L"0.21.0.partial");
  EXPECT_EQ(DownloadRecord(update, "0.21.0").BaseName().value(),
            L"0.21.0.download");
  EXPECT_EQ(SwappingFolder(update).BaseName().value(), L"Swapping");
  EXPECT_EQ(NotInstalledMark(update).BaseName().value(), L"not-installed");

  EXPECT_TRUE(IsInstallLayout(app));
  EXPECT_TRUE(IsInstallLayout(base::FilePath(L"D:\\x\\pane\\application\\")));
  EXPECT_FALSE(IsInstallLayout(base::FilePath(L"C:\\chromium\\src\\out\\Win")));
  EXPECT_FALSE(IsInstallLayout(base::FilePath(L"C:\\x\\Other\\Application")));

  // A release's version names a folder; nothing else does.
  EXPECT_TRUE(IsStagedVersion("0.21.0"));
  EXPECT_TRUE(IsStagedVersion("99.0.0"));
  EXPECT_FALSE(IsStagedVersion(""));
  EXPECT_FALSE(IsStagedVersion("..\\Application"));
  EXPECT_FALSE(IsStagedVersion("0.21.0-beta"));
  EXPECT_FALSE(IsStagedVersion("0.21.0\\x"));
  EXPECT_FALSE(IsStagedVersion("1..2"));
}

// A launch keeps the newest version staged whole and newer than the one
// running, with its installer, and takes the helper's mark for this launch
// alone; everything else goes, the installers of the versions that go with
// them.
TEST(UpdateFolderTest, LaunchKeepsTheNewestWholeVersion) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath update = temp.GetPath().Append(L"Update");
  const base::FilePath downloads = temp.GetPath().Append(L"Downloads");
  ASSERT_TRUE(base::CreateDirectory(downloads));
  WriteStaged(StagedFolder(update, "0.21.0"), "0.21.0");
  WriteStaged(StagedFolder(update, "0.22.0"), "0.22.0");
  WriteStaged(StagedFolder(update, "0.20.0"), "0.20.0");
  // Not whole: no chrome.exe, and another version's pane-version.
  ASSERT_TRUE(base::CreateDirectory(StagedFolder(update, "0.24.0")));
  WriteStaged(StagedFolder(update, "0.25.0"), "0.21.0");
  ASSERT_TRUE(base::CreateDirectory(PartialFolder(update, "0.23.0")));
  ASSERT_TRUE(base::CreateDirectory(RemovingFolder(update, "0.19.0")));
  ASSERT_TRUE(base::CreateDirectory(
      SwappingFolder(update).Append(L"1_2\\Application")));
  const base::FilePath kept_download = downloads.Append(L"Pane-0.22.0-x64.exe");
  const base::FilePath passed_download =
      downloads.Append(L"Pane-0.21.0-x64.exe");
  ASSERT_TRUE(base::WriteFile(kept_download, "0.22.0"));
  ASSERT_TRUE(base::WriteFile(passed_download, "0.21.0"));
  ASSERT_TRUE(
      WriteDownloadRecord(DownloadRecord(update, "0.22.0"), kept_download));
  ASSERT_TRUE(
      WriteDownloadRecord(DownloadRecord(update, "0.21.0"), passed_download));
  ASSERT_TRUE(base::WriteFile(NotInstalledMark(update), "the swap failed"));

  const UpdateFolderScan scan =
      ScanUpdateFolder(update, base::Version("0.20.1"));
  EXPECT_EQ(scan.kept, "0.22.0");
  EXPECT_TRUE(scan.not_installed);
  EXPECT_EQ(scan.leftovers.size(), 8u);
  EXPECT_EQ(scan.leftover_downloads,
            std::vector<base::FilePath>{passed_download});

  const std::optional<KeptUpdate> kept =
      CleanUpdateFolder(update, base::Version("0.20.1"));
  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->version, "0.22.0");
  EXPECT_EQ(kept->staged, StagedFolder(update, "0.22.0"));
  EXPECT_EQ(kept->download, kept_download);
  EXPECT_TRUE(kept->not_installed);
  // What's left: the version kept, its record, and its installer.
  EXPECT_TRUE(base::PathExists(StagedFolder(update, "0.22.0")));
  EXPECT_TRUE(base::PathExists(DownloadRecord(update, "0.22.0")));
  EXPECT_TRUE(base::PathExists(kept_download));
  EXPECT_FALSE(base::PathExists(passed_download));
  EXPECT_FALSE(base::PathExists(NotInstalledMark(update)));
  int entries = 0;
  base::FileEnumerator left(
      update, false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = left.Next(); !path.empty(); path = left.Next()) {
    ++entries;
  }
  EXPECT_EQ(entries, 2);

  // The mark spoke once: the next launch keeps the version, and says nothing.
  const std::optional<KeptUpdate> again =
      CleanUpdateFolder(update, base::Version("0.20.1"));
  ASSERT_TRUE(again);
  EXPECT_FALSE(again->not_installed);

  // Installed meanwhile: nothing is kept, and the folder goes.
  EXPECT_FALSE(CleanUpdateFolder(update, base::Version("0.22.0")));
  EXPECT_FALSE(base::PathExists(update));
  EXPECT_FALSE(base::PathExists(kept_download));
}

// Staging reads the installer's payload where it is, unpacks it beside the
// app, checks it whole, and renames it into place with a record of the
// installer; what doesn't check out leaves nothing.
TEST(UpdateFolderTest, StagesTheInstallersPayload) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath update = temp.GetPath().Append(L"Update");
  const base::FilePath installer = WriteInstaller(temp.GetPath(), "99.0.0");

  const std::optional<base::FilePath> staged =
      StageUpdate(installer, "99.0.0", update);
  ASSERT_TRUE(staged);
  EXPECT_EQ(*staged, StagedFolder(update, "99.0.0"));
  EXPECT_EQ(VersionIn(*staged), base::Version("99.0.0"));
  EXPECT_EQ(Contents(staged->Append(L"chrome.exe")), "chrome.exe, a stand-in");
  EXPECT_EQ(Contents(staged->Append(L"pane_setup.exe")),
            "pane_setup.exe, a stand-in");
  EXPECT_EQ(Contents(staged->Append(L"locales\\en-US.pak")), "a locale");
  EXPECT_FALSE(base::PathExists(PartialFolder(update, "99.0.0")));
  EXPECT_EQ(ReadDownloadRecord(DownloadRecord(update, "99.0.0")), installer);

  // Offered as another version than its payload holds: nothing staged.
  EXPECT_FALSE(StageUpdate(installer, "98.0.0", update));
  // A payload whose own pane-version is another version's: unpacked, then
  // refused, nothing left.
  const base::FilePath mislabelled = WriteInstaller(temp.GetPath(), "98.0.0");
  EXPECT_FALSE(StageUpdate(mislabelled, "98.0.0", update));
  EXPECT_FALSE(base::PathExists(PartialFolder(update, "98.0.0")));
  EXPECT_FALSE(base::PathExists(StagedFolder(update, "98.0.0")));
  // No installer at all.
  EXPECT_FALSE(
      StageUpdate(temp.GetPath().Append(L"none.exe"), "99.0.0", update));
}

// A newer offer passes what was staged: the version, its record, and its
// installer go.
TEST(UpdateFolderTest, UnstagesWithItsInstaller) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath update = temp.GetPath().Append(L"Update");
  const base::FilePath installer = WriteInstaller(temp.GetPath(), "99.0.0");
  const std::optional<base::FilePath> staged =
      StageUpdate(installer, "99.0.0", update);
  ASSERT_TRUE(staged);

  UnstageUpdate(*staged, installer);
  EXPECT_FALSE(base::PathExists(*staged));
  EXPECT_FALSE(base::PathExists(RemovingFolder(update, "99.0.0")));
  EXPECT_FALSE(base::PathExists(DownloadRecord(update, "99.0.0")));
  EXPECT_FALSE(base::PathExists(installer));
}

}  // namespace pane::installer
