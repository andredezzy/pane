// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/encryption/whole_file.h"

#include <string>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/pane_file_codec.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::encryption {
namespace {

constexpr char kID[] = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02";

ProfileKeys KeysOf(uint8_t seed) {
  ProfileKeys keys;
  keys.whole_files.fill(seed);
  return keys;
}

TEST(WholeFileTest, RoundTrips) {
  const std::string text = "{\"a\": \"secret\"}";
  const std::string sealed = SealWholeFile(KeysOf(1), base::as_byte_span(text));
  EXPECT_TRUE(IsSealedWholeFile(base::as_byte_span(sealed)));
  EXPECT_EQ(sealed.find("secret"), std::string::npos);
  EXPECT_EQ(OpenWholeFile(KeysOf(1), base::as_byte_span(sealed)), text);
}

TEST(WholeFileTest, EachSealIsDifferent) {
  const std::string text = "same";
  EXPECT_NE(SealWholeFile(KeysOf(1), base::as_byte_span(text)),
            SealWholeFile(KeysOf(1), base::as_byte_span(text)));
}

TEST(WholeFileTest, AnotherKeyOrAChangedByteDoesNotOpen) {
  const std::string text = "secret";
  std::string sealed = SealWholeFile(KeysOf(1), base::as_byte_span(text));
  EXPECT_FALSE(OpenWholeFile(KeysOf(2), base::as_byte_span(sealed)));
  sealed.back() ^= 1;
  EXPECT_FALSE(OpenWholeFile(KeysOf(1), base::as_byte_span(sealed)));
}

TEST(WholeFileTest, AnEmptyFileRoundTrips) {
  const std::string sealed = SealWholeFile(KeysOf(1), {});
  EXPECT_EQ(OpenWholeFile(KeysOf(1), base::as_byte_span(sealed)), "");
}

class WholeFileHooksTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    ResetForTesting();
    root_ = base::MakeAbsoluteFilePath(temp_.GetPath());
    AddRoots({root_});
    Key key;
    key.fill(9);
    RegisterKey(kID, key);
    profile_ = root_.Append(kID);
    ASSERT_TRUE(base::CreateDirectory(profile_));
    InstallWholeFileCodec();
  }

  void TearDown() override {
    base::SetPaneFileCodec(nullptr);
    ResetForTesting();
  }

  std::string RawBytes(const base::FilePath& path) {
    base::SetPaneFileCodec(nullptr);
    std::string raw;
    EXPECT_TRUE(base::ReadFileToString(path, &raw));
    InstallWholeFileCodec();
    return raw;
  }

  base::ScopedTempDir temp_;
  base::FilePath root_;
  base::FilePath profile_;
};

TEST_F(WholeFileHooksTest, WriteFileAndImportantFileWriterSealAProfilesFile) {
  const base::FilePath plain = profile_.Append("Written");
  const base::FilePath atomic = profile_.Append("Preferences");
  ASSERT_TRUE(base::WriteFile(plain, "secret one"));
  ASSERT_TRUE(base::ImportantFileWriter::WriteFileAtomically(atomic, "secret two"));

  for (const auto& path : {plain, atomic}) {
    const std::string raw = RawBytes(path);
    EXPECT_TRUE(IsSealedWholeFile(base::as_byte_span(raw)));
    EXPECT_EQ(raw.find("secret"), std::string::npos);
  }

  std::string read;
  ASSERT_TRUE(base::ReadFileToString(plain, &read));
  EXPECT_EQ(read, "secret one");
  ASSERT_TRUE(base::ReadFileToString(atomic, &read));
  EXPECT_EQ(read, "secret two");
  EXPECT_EQ(base::ReadFileToBytes(atomic),
            std::vector<uint8_t>(read.begin(), read.end()));
}

TEST_F(WholeFileHooksTest, AFileOutsideAProfileAndExtensionsCodeStayPlain) {
  const base::FilePath outside = root_.Append("Local State");
  const base::FilePath code = profile_.Append("Extensions/abc/manifest.json");
  ASSERT_TRUE(base::CreateDirectory(code.DirName()));
  ASSERT_TRUE(base::WriteFile(outside, "plain"));
  ASSERT_TRUE(base::WriteFile(code, "plain"));
  EXPECT_EQ(RawBytes(outside), "plain");
  EXPECT_EQ(RawBytes(code), "plain");
}

TEST_F(WholeFileHooksTest, APlainFileInAProfilePassesAsItIs) {
  base::SetPaneFileCodec(nullptr);
  const base::FilePath path = profile_.Append("Older");
  ASSERT_TRUE(base::WriteFile(path, "older"));
  InstallWholeFileCodec();
  std::string read;
  ASSERT_TRUE(base::ReadFileToString(path, &read));
  EXPECT_EQ(read, "older");
}

TEST_F(WholeFileHooksTest, ASealedFileThatDoesNotOpenCrashesAndStays) {
  const base::FilePath path = profile_.Append("Preferences");
  ASSERT_TRUE(base::WriteFile(path, "secret"));
  std::string raw = RawBytes(path);
  raw.back() ^= 1;
  base::SetPaneFileCodec(nullptr);
  ASSERT_TRUE(base::WriteFile(path, raw));
  InstallWholeFileCodec();
  std::string read;
  EXPECT_DEATH_IF_SUPPORTED(base::ReadFileToString(path, &read), "");
  EXPECT_EQ(RawBytes(path), raw);
}

}  // namespace
}  // namespace pane::encryption
