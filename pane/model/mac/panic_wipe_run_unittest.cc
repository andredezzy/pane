// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/panic_wipe_run.h"

#include <signal.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "pane/model/data_key.h"
#include "pane/model/mac/data_volume.h"
#include "pane/model/mac/file_operations.h"
#include "pane/model/mac/test_keychain.h"
#include "pane/model/mac/vault_test_support.h"
#include "pane/model/mac/wipe_sources.h"
#include "pane/model/panic_wipe.h"
#include "pane/model/persisted_file.h"
#include "pane/model/security.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// The panic wipe's run at launch (PanicWipeRun.atLaunch), and when it is due:
// the Swift app's had no unit tests, the security scenario ran it.

constexpr std::string_view kSecret = "c2VjcmV0c2VjcmV0c2VjcmV0";

class PanicWipeRunTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    data_ = directory_.GetPath().Append("data");
    ASSERT_TRUE(base::CreateDirectory(data_.Append("Engine").Append("alpha")));
    ASSERT_TRUE(base::WriteFile(
        data_.Append("Engine").Append("alpha").Append("Cookies"), "cookies"));
    ASSERT_TRUE(base::WriteFile(data_.Append("profiles.json"), "{}"));
    tools_ =
        base::MakeRefCounted<FakeVolumeTools>(DataVolume(data_, std::nullopt));
  }

  void SaveSecurity(const SecurityState& state) {
    ASSERT_TRUE(PersistedFile(data_, "security").Save(state.ToValue()));
  }

  // As the last attempt at the lock left it: the PIN, the count, no key.
  void SaveWipeDue() {
    SaveSecurity(SecurityState(Pin{.length = 4}, SecurityState::kMaxAttempts,
                               std::nullopt));
  }

  // The proxy passwords an earlier Pane kept in their own Keychain item.
  void SaveProxyItem() {
    keychain_.SetItem("Pane Proxy Passwords", StandardizedPath(data_).value(),
                      R"({"alpha":"secret"})");
  }

  scoped_refptr<LaunchErase> AtLaunch(
      const std::vector<std::string>& arguments = {}) {
    std::vector<std::string> argv = {"pane"};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return PanicWipeAtLaunch(
        data_, base::CommandLine(argv),
        MacWipeExtras(data_, tools_, keychain_.safe_storage()));
  }

  // Everything of the data's is gone, the root itself kept for a fresh Pane.
  void ExpectErased() {
    EXPECT_FALSE(base::PathExists(data_.Append("Engine")));
    EXPECT_FALSE(base::PathExists(data_.Append("profiles.json")));
    EXPECT_FALSE(base::PathExists(data_.Append("security.json")));
    EXPECT_FALSE(base::PathExists(data_.Append("Wiping")));
    EXPECT_FALSE(keychain_.Item("Pane Proxy Passwords",
                                StandardizedPath(data_).value()));
    EXPECT_EQ(tools_->Count("leftovers"), 1);
  }

  base::ScopedTempDir directory_;
  base::FilePath data_;
  TestKeychain keychain_;
  scoped_refptr<FakeVolumeTools> tools_;
};

// The wiped instance died before starting this one: security.json still
// holds the last attempt, so this one erases.
TEST_F(PanicWipeRunTest, WipesWhenSecurityJsonSaysSo) {
  SaveWipeDue();
  SaveProxyItem();
  const scoped_refptr<LaunchErase> erase = AtLaunch();
  ASSERT_TRUE(erase);
  erase->Wait();
  EXPECT_FALSE(erase->IsErasing());
  EXPECT_TRUE(erase->Failures().empty());
  ExpectErased();
}

// The wiped instance started this one, and named itself and its helpers: this
// one waits them out, then erases, whatever security.json says.
TEST_F(PanicWipeRunTest, WaitsOutTheWipedInstanceThenWipes) {
  SaveProxyItem();
  // A process that isn't this test's child, so it is gone once it ends.
  std::string output;
  ASSERT_TRUE(base::GetAppOutput(
      {"/bin/sh", "-c", "/bin/sleep 0.3 >/dev/null 2>&1 & echo $!"}, &output));
  int pid = 0;
  ASSERT_TRUE(base::StringToInt(
      base::TrimWhitespaceASCII(output, base::TRIM_ALL), &pid));
  ASSERT_EQ(kill(pid, 0), 0);

  const scoped_refptr<LaunchErase> erase = AtLaunch(
      {base::StrCat({"--pane-wipe-after=", base::NumberToString(pid)})});
  EXPECT_NE(kill(pid, 0), 0);
  ASSERT_TRUE(erase);
  erase->Wait();
  ExpectErased();
}

// The switch counts wherever it is, as the Swift read it: after a "--" too.
TEST_F(PanicWipeRunTest, ReadsTheSwitchAfterArguments) {
  const scoped_refptr<LaunchErase> erase =
      AtLaunch({"--", "https://example.com", "--pane-wipe-after="});
  ASSERT_TRUE(erase);
  erase->Wait();
  ExpectErased();
}

// Data a wipe cut short left (a crash, a quit) is erased at the next launch,
// with nothing else due.
TEST_F(PanicWipeRunTest, FinishesAWipeCutShort) {
  const base::FilePath staged =
      data_.Append(kWipeStagingName).Append("A1").Append("Engine");
  ASSERT_TRUE(base::CreateDirectory(staged));
  ASSERT_TRUE(base::WriteFile(staged.Append("Cookies"), "cookies"));

  const scoped_refptr<LaunchErase> erase = AtLaunch();
  ASSERT_TRUE(erase);
  erase->Wait();
  EXPECT_FALSE(base::PathExists(data_.Append(kWipeStagingName)));
  // Nothing was due: the data stays.
  EXPECT_TRUE(base::PathExists(data_.Append("Engine").Append("alpha")));
  EXPECT_EQ(tools_->Count("leftovers"), 0);
}

// Nothing due, nothing staged: the launch goes on, the data untouched.
TEST_F(PanicWipeRunTest, LeavesDataWithNoWipeDue) {
  SaveSecurity(SecurityState(
      Pin{.length = 4}, 2,
      SealDataKey(MakeDataKey(), base::as_byte_span(kSecret), "1234")));
  SaveProxyItem();
  EXPECT_FALSE(AtLaunch());
  EXPECT_TRUE(base::PathExists(data_.Append("Engine").Append("alpha")));
  EXPECT_TRUE(base::PathExists(data_.Append("security.json")));
  EXPECT_TRUE(
      keychain_.Item("Pane Proxy Passwords", StandardizedPath(data_).value()));
  EXPECT_TRUE(tools_->calls().empty());
}

// Due only with a PIN, the last attempt counted, and no key: the lock's fifth
// attempt takes the key in the same save.
TEST_F(PanicWipeRunTest, IsWipeDue) {
  const SealedKey key =
      SealDataKey(MakeDataKey(), base::as_byte_span(kSecret), "1234");
  EXPECT_FALSE(IsWipeDue(data_));

  SaveWipeDue();
  EXPECT_TRUE(IsWipeDue(data_));
  SaveSecurity(
      SecurityState(Pin{.length = 4}, SecurityState::kMaxAttempts, key));
  EXPECT_FALSE(IsWipeDue(data_));
  SaveSecurity(
      SecurityState(std::nullopt, SecurityState::kMaxAttempts, std::nullopt));
  EXPECT_FALSE(IsWipeDue(data_));
  SaveSecurity(SecurityState(Pin{.length = 4}, SecurityState::kMaxAttempts - 1,
                             std::nullopt));
  EXPECT_FALSE(IsWipeDue(data_));

  // A damaged file is read for what is left of it.
  ASSERT_TRUE(base::WriteFile(
      data_.Append("security.json"),
      R"({"state":{"failedAttempts":5,"pin":{"hash":"$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO","length":4})"));
  EXPECT_TRUE(IsWipeDue(data_));
}

// The Electron app's cookie key goes with its data, without asking; any other
// folder names no item of its.
TEST_F(PanicWipeRunTest, ForgetsTheElectronAppsKey) {
  keychain_.SetItem("@pane/veil Safe Storage", "@pane/veil", "key");
  keychain_.SetEngineSecret(kSecret);

  ForgetElectronKey(
      ElectronData{.directory = directory_.GetPath().Append("Elsewhere")},
      keychain_.safe_storage());
  EXPECT_TRUE(keychain_.Item("@pane/veil Safe Storage", "@pane/veil"));

  ForgetElectronKey(
      ElectronData{.directory =
                       directory_.GetPath().Append("@pane").Append("veil")},
      keychain_.safe_storage());
  EXPECT_FALSE(keychain_.Item("@pane/veil Safe Storage", "@pane/veil"));
  EXPECT_EQ(keychain_.EngineSecret(), kSecret);
}

// The engine's helpers are this process's children.
TEST_F(PanicWipeRunTest, ListsTheChildProcesses) {
  base::Process child =
      base::LaunchProcess({"/bin/sleep", "30"}, base::LaunchOptions());
  ASSERT_TRUE(child.IsValid());
  const std::vector<base::ProcessId> children = ChildProcesses();
  EXPECT_TRUE(std::ranges::contains(children, child.Pid()));
  EXPECT_FALSE(std::ranges::contains(children, base::GetCurrentProcId()));
  child.Terminate(0, /*wait=*/true);
}

}  // namespace
}  // namespace pane::model
