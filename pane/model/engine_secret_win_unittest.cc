// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_secret.h"

#include <windows.h>

#include <dpapi.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "base/win/scoped_localalloc.h"
#include "components/os_crypt/async/browser/os_crypt_win.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// os_crypt's key under DPAPI, the secret the data key is sealed under on
// Windows (U5): what Pane makes is what Chromium reads as its own, and what
// is there is never written over.

base::DictValue LocalState(const base::FilePath& directory) {
  std::string text;
  EXPECT_TRUE(base::ReadFileToString(
      directory.Append(FILE_PATH_LITERAL("Local State")), &text));
  std::optional<base::DictValue> value =
      base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  EXPECT_TRUE(value);
  return value ? std::move(*value) : base::DictValue();
}

std::vector<uint8_t> Unprotected(const std::string& encoded) {
  const std::optional<std::vector<uint8_t>> blob = base::Base64Decode(encoded);
  EXPECT_TRUE(blob && blob->size() > 5);
  if (!blob || blob->size() <= 5) {
    return {};
  }
  const base::span<const uint8_t> sealed = base::span(*blob).subspan(5u);
  DATA_BLOB input = {.cbData = static_cast<DWORD>(sealed.size()),
                     .pbData = const_cast<BYTE*>(sealed.data())};
  DATA_BLOB output = {};
  EXPECT_TRUE(::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                                   0, &output));
  const base::win::ScopedLocalAlloc owned =
      base::win::TakeLocalAlloc(output.pbData);
  // SAFETY: DPAPI gives `cbData` bytes at `pbData`.
  return base::ToVector(UNSAFE_BUFFERS(
      base::span(static_cast<const uint8_t*>(owned.get()), output.cbData)));
}

// The user's first launch makes it at BasicStartupComplete, before the engine
// has a FeatureList, where content makes reading any feature fail at once
// (RunContentProcess): making it, and finding it again, read none.
TEST(EngineSecretTest, MadeBeforeTheEngineHasFeatures) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());

  std::unique_ptr<base::FeatureList> features =
      base::FeatureList::ClearInstanceForTesting();
  const std::optional<std::vector<uint8_t>> made =
      EngineSecret(directory.GetPath(), /*makes_one=*/true);
  const std::optional<std::vector<uint8_t>> found =
      EngineSecret(directory.GetPath(), /*makes_one=*/false);
  const base::Feature* read_too_early =
      base::FeatureList::GetEarlyAccessedFeatureForTesting();
  base::FeatureList::ResetEarlyFeatureAccessTrackerForTesting();
  base::FeatureList::RestoreInstanceForTesting(std::move(features));

  ASSERT_TRUE(made);
  EXPECT_EQ(found, made);
  EXPECT_EQ(read_too_early, nullptr) << read_too_early->name;
  EXPECT_EQ(Unprotected(*LocalState(directory.GetPath())
                             .FindStringByDottedPath("os_crypt.encrypted_key")),
            *made);
}

// None there: made as Chromium makes it, in Local State, which Chromium then
// reads as its own; found again after.
TEST(EngineSecretTest, MakesTheKeyChromiumReads) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());

  const std::optional<std::vector<uint8_t>> made =
      EngineSecret(directory.GetPath(), /*makes_one=*/true);
  ASSERT_TRUE(made);
  EXPECT_EQ(made->size(), 32u);

  const base::DictValue state = LocalState(directory.GetPath());
  const std::string* encoded =
      state.FindStringByDottedPath("os_crypt.encrypted_key");
  ASSERT_TRUE(encoded);
  EXPECT_TRUE(base::Base64Decode(*encoded)->size() > 5);
  EXPECT_EQ(Unprotected(*encoded), *made);
  EXPECT_EQ(state.FindBoolByDottedPath("os_crypt.audit_enabled"), true);

  TestingPrefServiceSimple prefs;
  os_crypt_async::RegisterLocalPrefs(prefs.registry());
  prefs.SetString("os_crypt.encrypted_key", *encoded);
  EXPECT_EQ(os_crypt_async::InitWithExistingKey(&prefs),
            os_crypt_async::InitResult::kSuccess);

  EXPECT_EQ(EngineSecret(directory.GetPath(), /*makes_one=*/false), made);
  EXPECT_EQ(EngineSecret(directory.GetPath(), /*makes_one=*/true), made);
}

// Find-only makes nothing.
TEST(EngineSecretTest, FindsWithoutMaking) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_FALSE(EngineSecret(directory.GetPath(), /*makes_one=*/false));
  EXPECT_FALSE(base::PathExists(
      directory.GetPath().Append(FILE_PATH_LITERAL("Local State"))));
}

// A key that won't open stays as it is: a new one would leave the data key
// sealed under nothing there.
TEST(EngineSecretTest, KeepsAKeyThatWontOpen) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath file =
      directory.GetPath().Append(FILE_PATH_LITERAL("Local State"));
  const std::string broken = R"({"os_crypt":{"encrypted_key":")" +
                             base::Base64Encode("DPAPInot a blob") + R"("}})";
  ASSERT_TRUE(base::WriteFile(file, broken));

  EXPECT_FALSE(EngineSecret(directory.GetPath(), /*makes_one=*/true));
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(file, &after));
  EXPECT_EQ(after, broken);
}

// What else Local State holds stays.
TEST(EngineSecretTest, KeepsTheRestOfLocalState) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      directory.GetPath().Append(FILE_PATH_LITERAL("Local State")),
      R"({"profile":{"last_used":"Default"}})"));
  ASSERT_TRUE(EngineSecret(directory.GetPath(), /*makes_one=*/true));
  const base::DictValue state = LocalState(directory.GetPath());
  EXPECT_EQ(*state.FindStringByDottedPath("profile.last_used"), "Default");
  EXPECT_TRUE(state.FindStringByDottedPath("os_crypt.encrypted_key"));
}

}  // namespace
}  // namespace pane::model
