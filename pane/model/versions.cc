// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/versions.h"

#include <vector>

#include "base/strings/string_split.h"
#include "base/types/expected_macros.h"
#include "base/version.h"

namespace pane::model {

namespace {

// Chromium's version, from a tag: without a leading "v", and up to the first
// "-" or "+" (empty pieces skipped, as Swift's split leaves them).
base::expected<base::Version, InvalidVersion> VersionOf(
    std::string_view version) {
  if (version.starts_with('v') || version.starts_with('V')) {
    version.remove_prefix(1);
  }
  const std::vector<std::string_view> pieces = base::SplitStringPiece(
      version, "-+", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  base::Version parsed(pieces.empty() ? std::string_view() : pieces.front());
  if (!parsed.IsValid()) {
    return base::unexpected(InvalidVersion());
  }
  return parsed;
}

}  // namespace

base::expected<bool, InvalidVersion> IsNewerVersion(std::string_view candidate,
                                                    std::string_view current) {
  ASSIGN_OR_RETURN(const base::Version candidate_version, VersionOf(candidate));
  ASSIGN_OR_RETURN(const base::Version current_version, VersionOf(current));
  // Missing trailing parts count as zero.
  return candidate_version > current_version;
}

}  // namespace pane::model
