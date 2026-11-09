// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_PROXY_PASSWORDS_H_
#define PANE_MODEL_PROXY_PASSWORDS_H_

#include <map>
#include <string>
#include <vector>

#include "pane/model/profile.h"

namespace pane::model {

// The proxy passwords an earlier Pane kept apart from profiles.json, by
// profile id (ProxyPasswordVault), go back into its profiles when its plain
// profiles.json is read and sealed (ADR 0113). A password the file still
// holds (the Electron app's, or an older Pane's) is read as it is.

// The profiles with their passwords from `stored`. A password still in the
// file wins: it was never moved.
std::vector<BrowserProfile> FillProxyPasswords(
    std::vector<BrowserProfile> profiles,
    const std::map<std::string, std::string>& stored);

// Whether any password needs the store: one to read for a proxy that signs in.
bool NeedsProxyPasswordStore(const std::vector<BrowserProfile>& profiles);

}  // namespace pane::model

#endif  // PANE_MODEL_PROXY_PASSWORDS_H_
