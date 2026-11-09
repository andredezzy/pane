// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_BUNDLE_IDENTIFIER_H_
#define PANE_MODEL_MAC_BUNDLE_IDENTIFIER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace pane::model {

// Pane's identifier (ADR 0114), the reverse of its domain,
// pane.roundedcorners.xyz: its bundle's, its data root's and caches' folder
// names, its preferences' domain, and its log's subsystem. The engine's
// BRANDING (MAC_BUNDLE_ID) names it too. Until 0.14.0 it was
// com.pane.desktop, whose data moves at the first launch after
// (MoveFormerData).
inline constexpr char kBundleIdentifier[] = "xyz.roundedcorners.pane";
inline constexpr char kFormerBundleIdentifier[] = "com.pane.desktop";

// What an earlier Pane kept under its former identifier moves to this one's,
// before anything reads it: the data root and the caches, a rename each on
// the same volume, and the window's frame from its preferences, whose domain
// then goes (it kept the open panel's last folder, ADR 0113). Nothing moves
// once this identifier's data root exists. Returns what could not move.
// `preferences` names the domain the frame moves into, the app's own;
// nullopt leaves the preferences: under a fixed home (scenarios) the data is
// the run's, but the preferences are the user's own, since cfprefsd ignores
// a fixed home. Tests name identifiers of their own: a persistent domain is
// the whole Mac's, whoever reads it.
std::vector<std::string> MoveFormerData(
    const base::FilePath& application_support,
    const base::FilePath& caches,
    const std::optional<std::string>& preferences,
    std::string_view former = kFormerBundleIdentifier,
    std::string_view pane = kBundleIdentifier);

// What URLSession's default sessions kept for an earlier Pane, in the clear
// and outside the encrypted data (ADR 0113): the update check's responses
// (Cache.db, fsCachedData, under the caches' folder, which the move above
// takes along) and its HTTP storage, under either identifier. Pane's
// sessions are ephemeral since, so none of it comes back.
std::vector<std::string> RemoveSessionCaches(
    const base::FilePath& library,
    const std::vector<std::string>& identifiers = {kBundleIdentifier,
                                                   kFormerBundleIdentifier});

// The user's cache folder (DARWIN_USER_CACHE_DIR, under /var/folders), which
// the system names for each app: HOME does not move it.
std::optional<base::FilePath> UserCacheDirectory();

// What the system kept there for the former identifier, which nothing reads
// now: its URL sessions' responses (Cache.db, fsCachedData, 0.13's update
// check among them) and its helpers' shader caches (ADR 0115).
std::vector<std::string> RemoveFormerUserCaches(
    const base::FilePath& user_caches,
    std::string_view former = kFormerBundleIdentifier);

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_BUNDLE_IDENTIFIER_H_
