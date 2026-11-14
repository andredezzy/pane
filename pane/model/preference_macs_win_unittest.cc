// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/preference_macs.h"

#include <windows.h>

#include <string>

#include "base/test/test_reg_util_win.h"
#include "base/win/registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

// Chromium's preference MACs in the registry, as it writes them
// (RegistryHashStoreContentsWin): a key per profile folder under
// <key>\PreferenceMACs, a value per tracked preference, and a key per split
// preference's items. HKCU is the test's own, never the user's.

constexpr wchar_t kKey[] = L"Software\\Pane";
constexpr char kProfile[] = "1F4A9C2E-7B3D-4E5F-8A6B-9C0D1E2F3A4B";
constexpr wchar_t kProfileWide[] = L"1F4A9C2E-7B3D-4E5F-8A6B-9C0D1E2F3A4B";
constexpr wchar_t kOther[] = L"2A5B8C3D-6E7F-4A1B-9C2D-3E4F5A6B7C8D";

class PreferenceMacsTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(registry_.OverrideRegistry(HKEY_CURRENT_USER));
    for (const wchar_t* profile : {kProfileWide, kOther, L"Default"}) {
      WriteMacs(profile);
    }
    // Another subkey of Pane's, which no forget touches.
    base::win::RegKey hosts;
    ASSERT_EQ(hosts.Create(HKEY_CURRENT_USER,
                           L"Software\\Pane\\NativeMessagingHosts\\host",
                           KEY_SET_VALUE),
              ERROR_SUCCESS);
    ASSERT_EQ(hosts.WriteValue(L"", L"C:\\host.json"), ERROR_SUCCESS);
  }

  static std::wstring Macs(const wchar_t* profile) {
    return std::wstring(kKey) + L"\\PreferenceMACs\\" + profile;
  }

  static void WriteMacs(const wchar_t* profile) {
    base::win::RegKey macs;
    ASSERT_EQ(macs.Create(HKEY_CURRENT_USER, Macs(profile).c_str(),
                          KEY_SET_VALUE | KEY_WOW64_32KEY),
              ERROR_SUCCESS);
    ASSERT_EQ(macs.WriteValue(L"homepage", std::wstring(64, L'A').c_str()),
              ERROR_SUCCESS);
    base::win::RegKey split;
    ASSERT_EQ(split.Create(HKEY_CURRENT_USER,
                           (Macs(profile) + L"\\extensions.settings").c_str(),
                           KEY_SET_VALUE | KEY_WOW64_32KEY),
              ERROR_SUCCESS);
    ASSERT_EQ(split.WriteValue(L"abcdefghijklmnopabcdefghijklmnop",
                               std::wstring(64, L'B').c_str()),
              ERROR_SUCCESS);
  }

  static bool Exists(const std::wstring& path) {
    base::win::RegKey key;
    return key.Open(HKEY_CURRENT_USER, path.c_str(),
                    KEY_QUERY_VALUE | KEY_WOW64_32KEY) == ERROR_SUCCESS;
  }

  registry_util::RegistryOverrideManager registry_;
};

// A deleted profile's MACs go, its split preferences' with them, and no
// other profile's.
TEST_F(PreferenceMacsTest, ForgetsOneProfiles) {
  EXPECT_TRUE(ForgetPreferenceMacs(kKey, kProfile));
  EXPECT_FALSE(Exists(Macs(kProfileWide)));
  EXPECT_TRUE(Exists(Macs(kOther)));
  EXPECT_TRUE(Exists(Macs(L"Default")));
  // Gone already: nothing is left, nothing fails.
  EXPECT_TRUE(ForgetPreferenceMacs(kKey, kProfile));
}

// The wipe's: every profile's, Default's too, and nothing else of Pane's key.
TEST_F(PreferenceMacsTest, ForgetsEveryProfiles) {
  EXPECT_TRUE(ForgetEveryPreferenceMacs(kKey));
  EXPECT_FALSE(Exists(std::wstring(kKey) + L"\\PreferenceMACs"));
  EXPECT_TRUE(Exists(L"Software\\Pane\\NativeMessagingHosts\\host"));
  EXPECT_TRUE(ForgetEveryPreferenceMacs(kKey));
}

// An empty id or key would name every profile's, or every key's: refused.
TEST_F(PreferenceMacsTest, RefusesAnEmptyName) {
  EXPECT_FALSE(ForgetPreferenceMacs(kKey, ""));
  EXPECT_FALSE(ForgetPreferenceMacs(L"", kProfile));
  EXPECT_FALSE(ForgetEveryPreferenceMacs(L""));
  EXPECT_TRUE(Exists(Macs(kProfileWide)));
  EXPECT_TRUE(Exists(Macs(kOther)));
}

}  // namespace
}  // namespace pane::model
