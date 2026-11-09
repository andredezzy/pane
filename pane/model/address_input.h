// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_ADDRESS_INPUT_H_
#define PANE_MODEL_ADDRESS_INPUT_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

namespace pane::model {

// What ⌘T opens.
inline constexpr char kNewTabURL[] = "https://www.google.com";

// What the address field loads for what the user typed: a web URL as is, a
// bare host ("example.com", "localhost:3000") over https, anything else as a
// Google search. Mirrors the Electron app's normalizeUrl. An empty GURL for
// blank input.
GURL AddressURL(std::string_view input);

// The field at rest: the domain (without www.) and the page's title, for a web
// address; nullopt for any other text, which shows as it is.
struct AddressLabel {
  std::string domain;
  std::string title;

  friend bool operator==(const AddressLabel&, const AddressLabel&) = default;
};
std::optional<AddressLabel> AddressLabelFor(std::string_view text,
                                            std::string_view title);

}  // namespace pane::model

#endif  // PANE_MODEL_ADDRESS_INPUT_H_
