// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/versions.h"

#include <algorithm>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/types/expected_macros.h"

namespace pane::model {

namespace {

base::expected<std::vector<int>, InvalidVersion> VersionParts(
    std::string_view version) {
  if (version.starts_with('v') || version.starts_with('V')) {
    version.remove_prefix(1);
  }
  // The first piece between "-" and "+", empty ones skipped, as Swift's
  // split leaves them.
  const std::vector<std::string_view> pieces = base::SplitStringPiece(
      version, "-+", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  const std::string_view core = pieces.empty() ? "" : pieces.front();
  if (core.empty()) {
    return base::unexpected(InvalidVersion());
  }
  std::vector<int> parts;
  for (std::string_view part : base::SplitStringPiece(
           core, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    int value = 0;
    if (!base::StringToInt(part, &value)) {
      return base::unexpected(InvalidVersion());
    }
    parts.push_back(value);
  }
  return parts;
}

}  // namespace

base::expected<bool, InvalidVersion> IsNewerVersion(std::string_view candidate,
                                                    std::string_view current) {
  ASSIGN_OR_RETURN(const std::vector<int> candidate_parts,
                   VersionParts(candidate));
  ASSIGN_OR_RETURN(const std::vector<int> current_parts, VersionParts(current));
  for (size_t index = 0;
       index < std::max(candidate_parts.size(), current_parts.size());
       ++index) {
    const int candidate_part =
        index < candidate_parts.size() ? candidate_parts[index] : 0;
    const int current_part =
        index < current_parts.size() ? current_parts[index] : 0;
    if (candidate_part != current_part) {
      return candidate_part > current_part;
    }
  }
  return false;
}

}  // namespace pane::model
