// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_TEST_HOOKS_H_
#define PANE_MODEL_TEST_HOOKS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/strings/cstring_view.h"
#include "base/time/time.h"

namespace pane::model {

// The mock keychain's secret, which every scenario's data key is sealed under
// (tests/lib/engine-key.ts): it opens none of the user's data, sealed under
// the Keychain's on the Mac, or os_crypt's on Windows (ADR 0113).
inline constexpr std::string_view kMockKeychainSecret = "Pane mock keychain";

// What the scenarios set (tests/lib), read in a scenario's run alone: one on
// the mock keychain, whose fixed secret opens none of the user's data (ADR
// 0115). A process of the user's that
// starts Pane with them gets nothing from them: no automation channel, no
// data root or update server of its own, no faster clocks.

// `name`'s value in a scenario's run: nullopt elsewhere, and when it is unset
// or empty.
std::optional<std::string> TestHook(base::cstring_view name);

// PANE_TIME_SCALE: how many times faster the clocks of Sleep and the Lock's
// time without use run, at least 1. 60 makes a minute pass every second.
double TestTimeScale();

// PANE_SHOWN_TIME: the moment Pane shows wherever it shows a time of day (the
// update check's "checked at"), in seconds since 1970, so two runs' renders
// show the same; nullopt unset, unreadable, or elsewhere. What Pane keeps and
// counts with stays the clock's.
std::optional<base::Time> TestShownTime();

}  // namespace pane::model

#endif  // PANE_MODEL_TEST_HOOKS_H_
