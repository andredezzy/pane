// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/extension_text.h"

#include "base/strings/strcat.h"

namespace pane::model {

namespace {

// Chrome's accept button when it adds an extension.
constexpr std::string_view kChromeAdd = "Add extension";
constexpr std::string_view kOpenQuote = "“";
constexpr std::string_view kCloseQuote = "”";

}  // namespace

std::string ExtensionPromptTitle(std::string_view accept_title,
                                 std::string_view extension_name,
                                 std::string_view chrome_title) {
  return accept_title == kChromeAdd
             ? base::StrCat({"Add ", kOpenQuote, extension_name, kCloseQuote,
                             " to Pane?"})
             : CurlyQuoted(chrome_title);
}

std::string ExtensionPromptAcceptTitle(std::string_view chrome_accept_title) {
  return chrome_accept_title == kChromeAdd ? "Add to Pane"
                                           : std::string(chrome_accept_title);
}

std::string ExtensionRemoveTitle(std::string_view extension_name) {
  return base::StrCat(
      {"Remove ", kOpenQuote, extension_name, kCloseQuote, "?"});
}

std::string CurlyQuoted(std::string_view title) {
  const size_t open = title.find('"');
  const size_t close = title.rfind('"');
  if (open == std::string_view::npos || open >= close) {
    return std::string(title);
  }
  return base::StrCat({title.substr(0, open), kOpenQuote,
                       title.substr(open + 1, close - open - 1), kCloseQuote,
                       title.substr(close + 1)});
}

GURL ReportAbuseURL(std::string_view extension_id) {
  return GURL(
      base::StrCat({"https://chrome.google.com/webstore/report/", extension_id,
                    "?utm_source=chrome-remove-extension-dialog"}));
}

}  // namespace pane::model
