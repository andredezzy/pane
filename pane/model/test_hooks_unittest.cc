// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/test_hooks.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/test/scoped_command_line.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace pane::model {
namespace {

constexpr char kHook[] = "PANE_TEST_HOOKS_UNITTEST";

class TestHooksTest : public testing::Test {
 protected:
  void TearDown() override {
    base::Environment::Create()->UnSetVar(kHook);
    base::Environment::Create()->UnSetVar("PANE_TIME_SCALE");
    base::Environment::Create()->UnSetVar("PANE_SHOWN_TIME");
  }

  void SetVar(base::cstring_view name, const std::string& value) {
    base::Environment::Create()->SetVar(name, value);
  }

  base::test::ScopedCommandLine command_line_;
};

// A launch of the user's, off the mock keychain, gets nothing from a hook.
TEST_F(TestHooksTest, NothingOffTheMockKeychain) {
  SetVar(kHook, "on");
  SetVar("PANE_TIME_SCALE", "60");
  SetVar("PANE_SHOWN_TIME", "1767260460");
  EXPECT_EQ(TestHook(kHook), std::nullopt);
  EXPECT_EQ(TestTimeScale(), 1);
  EXPECT_EQ(TestShownTime(), std::nullopt);
}

TEST_F(TestHooksTest, AScenariosRunReadsThem) {
  command_line_.GetProcessCommandLine()->AppendSwitch("use-mock-keychain");
  EXPECT_EQ(TestHook(kHook), std::nullopt);
  SetVar(kHook, "");
  EXPECT_EQ(TestHook(kHook), std::nullopt);
  SetVar(kHook, "on");
  EXPECT_EQ(TestHook(kHook), "on");
}

// At least 1: a clock never runs slower than the user's.
TEST_F(TestHooksTest, TimeScale) {
  command_line_.GetProcessCommandLine()->AppendSwitch("use-mock-keychain");
  EXPECT_EQ(TestTimeScale(), 1);
  SetVar("PANE_TIME_SCALE", "60");
  EXPECT_EQ(TestTimeScale(), 60);
  SetVar("PANE_TIME_SCALE", "0.5");
  EXPECT_EQ(TestTimeScale(), 1);
  SetVar("PANE_TIME_SCALE", "fast");
  EXPECT_EQ(TestTimeScale(), 1);
}

// A whole number of seconds since 1970, or nothing shown in its place.
TEST_F(TestHooksTest, ShownTime) {
  command_line_.GetProcessCommandLine()->AppendSwitch("use-mock-keychain");
  EXPECT_EQ(TestShownTime(), std::nullopt);
  SetVar("PANE_SHOWN_TIME", "1767260460");
  EXPECT_EQ(TestShownTime(), base::Time::FromSecondsSinceUnixEpoch(1767260460));
  SetVar("PANE_SHOWN_TIME", "09:41");
  EXPECT_EQ(TestShownTime(), std::nullopt);
}

}  // namespace
}  // namespace pane::model
