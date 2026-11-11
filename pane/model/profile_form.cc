// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile_form.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "url/url_canon_ip.h"

namespace pane::model {

namespace {

constexpr int kMinProxyPort = 1;
constexpr int kMaxProxyPort = 65535;

// A tab, or one of Unicode's space separators (Zs).
bool IsSpace(UChar32 c) {
  return c == '\t' || u_charType(c) == U_SPACE_SEPARATOR;
}

// Whether every code point of `text` passes `predicate`; false for text that
// is not UTF-8.
template <typename Predicate>
bool AllCodePoints(std::string_view text, Predicate predicate) {
  for (size_t i = 0; i < text.size(); ++i) {
    UChar32 c;
    if (!base::ReadUnicodeCharacter(text, &i, &c) || !predicate(c)) {
      return false;
    }
  }
  return true;
}

// Four parts of 0 to 255 in plain decimal: Chrome reads a part with a leading
// zero as octal. The Mac's inet_pton, which the Swift app asked, took
// "010.1.1.1" as 10.1.1.1, and Chrome went to 8.1.1.1.
bool IsIPv4Address(std::string_view text) {
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      text, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  return parts.size() == 4 &&
         std::ranges::all_of(parts, [](std::string_view part) {
           unsigned value = 0;
           return !part.empty() && (part.size() == 1 || part[0] != '0') &&
                  std::ranges::all_of(part, base::IsAsciiDigit<char>) &&
                  base::StringToUint(part, &value) && value <= 255;
         });
}

// An IPv6 address without its brackets, as Chrome reads one, with an IPv4
// tail in plain decimal; a zone after it ("%en0") is read past, as the Mac's
// inet_pton reads one. That inet_pton also took groups of more than four
// digits ("00001::"), which Chrome refuses.
bool IsIPv6Address(std::string_view text) {
  const size_t zone = text.find('%');
  if (zone != std::string_view::npos) {
    if (text.find('%', zone + 1) != std::string_view::npos) {
      return false;
    }
    text = text.substr(0, zone);
  }
  std::array<uint8_t, 16> address;
  return url::IPv6AddressToNumber(base::StrCat({"[", text, "]"}), address) &&
         (text.find('.') == std::string_view::npos ||
          IsIPv4Address(text.substr(text.rfind(':') + 1)));
}

// Whether each character is a dot or a number, Unicode's numbers among them
// ("²", "١"). Swift read a mark joined to a digit ("1́") as part of it, where
// this reads each code point on its own.
bool IsDotsAndNumbers(std::string_view text) {
  return AllCodePoints(text, [](UChar32 c) {
    return c == '.' ||
           u_getIntPropertyValue(c, UCHAR_NUMERIC_TYPE) != U_NT_NONE;
  });
}

}  // namespace

Fingerprint FingerprintPreset(Platform platform) {
  Fingerprint fingerprint;
  fingerprint.platform = platform;
  fingerprint.language = "en-US";
  fingerprint.languages = {"en-US", "en"};
  fingerprint.hardware_concurrency = 8;
  fingerprint.device_memory = 8;
  fingerprint.max_touch_points = 0;
  switch (platform) {
    case Platform::kWindows:
      fingerprint.user_agent =
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36";
      fingerprint.screen = {.width = 1920, .height = 1080, .color_depth = 24};
      fingerprint.webgl = Fingerprint::WebGL{
          .vendor = "Google Inc. (NVIDIA)",
          .renderer =
              "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 "
              "vs_5_0 ps_5_0, D3D11)"};
      break;
    case Platform::kMacOS:
      fingerprint.user_agent =
          "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 "
          "Safari/537.36";
      fingerprint.screen = {.width = 1440, .height = 900, .color_depth = 30};
      fingerprint.webgl =
          Fingerprint::WebGL{.vendor = "Google Inc. (Apple)",
                             .renderer = "ANGLE (Apple, Apple M1, OpenGL 4.1)"};
      break;
    case Platform::kLinux:
      fingerprint.user_agent =
          "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
          "Gecko) Chrome/136.0.0.0 Safari/537.36";
      fingerprint.screen = {.width = 1920, .height = 1080, .color_depth = 24};
      fingerprint.webgl = Fingerprint::WebGL{
          .vendor = "Google Inc. (Intel)",
          .renderer =
              "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, "
              "OpenGL 4.6)"};
      fingerprint.hardware_concurrency = 4;
      break;
  }
  return fingerprint;
}

Fingerprint GeneratedFingerprint(
    Platform platform,
    const std::optional<Fingerprint::WebGL>& host_webgl) {
  Fingerprint fingerprint = FingerprintPreset(platform);
  if (platform == HostPlatform() && host_webgl) {
    fingerprint.webgl = host_webgl;
  }
  return fingerprint;
}

std::optional<Fingerprint> EditedFingerprint(
    const std::optional<Fingerprint>& stored,
    bool spoofing,
    Platform platform,
    base::FunctionRef<Fingerprint()> generate) {
  if (!spoofing) {
    return std::nullopt;
  }
  if (stored && stored->platform == platform) {
    return stored;
  }
  return generate();
}

ProxyString::ProxyString() = default;
ProxyString::ProxyString(const ProxyString&) = default;
ProxyString& ProxyString::operator=(const ProxyString&) = default;
ProxyString::~ProxyString() = default;

// static
std::optional<ProxyString> ProxyString::Parse(std::string_view raw) {
  const std::string_view line = TrimSpaces(raw);
  if (IsIPv6Address(line)) {
    return std::nullopt;
  }

  const bool bracketed = line.starts_with('[');
  size_t host_end = line.find(bracketed ? ']' : ':');
  if (bracketed && host_end != std::string_view::npos) {
    ++host_end;
  }
  if (host_end >= line.size() || line[host_end] != ':') {
    return std::nullopt;
  }

  // Each field after the host, by its colon; the password is the rest.
  std::string_view rest = line.substr(host_end + 1);
  const size_t port_end = rest.find(':');
  const std::optional<int> port = ProxyPort(rest.substr(0, port_end));
  if (host_end == 0 || !port) {
    return std::nullopt;
  }

  ProxyString parsed;
  parsed.host = std::string(line.substr(0, host_end));
  parsed.port = *port;
  if (port_end != std::string_view::npos) {
    rest = rest.substr(port_end + 1);
    const size_t username_end = rest.find(':');
    const std::string_view username = rest.substr(0, username_end);
    if (!username.empty()) {
      parsed.username = std::string(username);
    }
    if (username_end != std::string_view::npos &&
        username_end + 1 < rest.size()) {
      parsed.password = std::string(rest.substr(username_end + 1));
    }
  }
  return parsed;
}

bool IsProxyHost(std::string_view text) {
  const std::string_view bare = text.starts_with('[') && text.ends_with(']')
                                    ? text.substr(1, text.size() - 2)
                                    : text;
  // IsIPv6Address takes a zone ("fe80::1%en0"), and Chrome does not.
  if (IsIPv6Address(bare)) {
    return bare.find('%') == std::string_view::npos;
  }

  if (bare != text || text.empty() || text.starts_with('.') ||
      text.find("..") != std::string_view::npos) {
    return false;
  }
  if (IsDotsAndNumbers(text)) {
    return IsIPv4Address(text);
  }
  return AllCodePoints(text, [](UChar32 c) {
    return c < 0x80 ? base::IsAsciiAlphaNumeric(c) || c == '-' || c == '.' ||
                          c == '_'
                    : !u_isUWhiteSpace(c);
  });
}

bool IsProxyPort(int port) {
  return port >= kMinProxyPort && port <= kMaxProxyPort;
}

std::optional<int> ProxyPort(std::string_view text) {
  int port = 0;
  return base::StringToInt(TrimSpaces(text), &port) && IsProxyPort(port)
             ? std::make_optional(port)
             : std::nullopt;
}

std::string_view TrimSpaces(std::string_view text) {
  // From the first code point that is not a space to the end of the last.
  size_t begin = text.size();
  size_t end = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const size_t start = i;
    UChar32 c;
    if (base::ReadUnicodeCharacter(text, &i, &c) && IsSpace(c)) {
      continue;
    }
    begin = std::min(begin, start);
    end = i + 1;
  }
  return begin < end ? text.substr(begin, end - begin) : std::string_view();
}

}  // namespace pane::model
