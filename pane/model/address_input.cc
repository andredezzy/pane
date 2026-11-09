// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/address_input.h"

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace pane::model {

GURL AddressURL(std::string_view input) {
  const std::string_view text =
      base::TrimWhitespaceASCII(input, base::TRIM_ALL);
  if (text.empty()) {
    return GURL();
  }
  if (text.starts_with("http://") || text.starts_with("https://")) {
    return GURL(text);
  }
  if (text.starts_with("localhost") ||
      (text.find('.') != std::string_view::npos &&
       text.find(' ') == std::string_view::npos)) {
    return GURL(base::StrCat({"https://", text}));
  }
  // Every character a query can't hold as it is escaped, "&" and "+" among
  // them, which the Swift app's URLComponents left as they were.
  return GURL(base::StrCat({"https://www.google.com/search?q=",
                            base::EscapeQueryParamValue(text, false)}));
}

std::optional<AddressLabel> AddressLabelFor(std::string_view text,
                                            std::string_view title) {
  const GURL url(text);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return std::nullopt;
  }
  std::string_view host = url.host();
  if (host.starts_with("www.")) {
    host.remove_prefix(4);
  }
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(title, base::TRIM_ALL);
  return AddressLabel{.domain = std::string(host),
                      .title = trimmed == text ? "" : std::string(trimmed)};
}

}  // namespace pane::model
