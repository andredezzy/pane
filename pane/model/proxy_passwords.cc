// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/proxy_passwords.h"

#include <algorithm>

namespace pane::model {

std::vector<BrowserProfile> FillProxyPasswords(
    std::vector<BrowserProfile> profiles,
    const std::map<std::string, std::string>& stored) {
  for (BrowserProfile& profile : profiles) {
    if (!profile.proxy || profile.proxy->password) {
      continue;
    }
    if (const auto password = stored.find(profile.id);
        password != stored.end()) {
      profile.proxy->password = password->second;
    }
  }
  return profiles;
}

bool NeedsProxyPasswordStore(const std::vector<BrowserProfile>& profiles) {
  return std::ranges::any_of(profiles, [](const BrowserProfile& profile) {
    return profile.proxy && (!profile.proxy->username.value_or("").empty() ||
                             profile.proxy->password);
  });
}

}  // namespace pane::model
