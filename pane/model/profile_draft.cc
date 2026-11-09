// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile_draft.h"

#include <utility>

#include "base/strings/string_number_conversions.h"

namespace pane::model {

namespace {

std::optional<ProfileDraft::Problem> HostProblem(std::string_view host) {
  if (host.empty()) {
    return ProfileDraft::Problem::kHostMissing;
  }
  return IsProxyHost(host)
             ? std::nullopt
             : std::make_optional(ProfileDraft::Problem::kHostInvalid);
}

std::optional<std::string> UnlessEmpty(std::string_view text) {
  return text.empty() ? std::nullopt : std::make_optional(std::string(text));
}

}  // namespace

ProfileDraft::ProfileDraft() = default;

ProfileDraft::ProfileDraft(const BrowserProfile& profile)
    : name(profile.name),
      color(profile.color),
      group(profile.group.value_or("")),
      saves_history(profile.saves_history),
      spoofing(profile.fingerprint.has_value()),
      platform(profile.fingerprint ? profile.fingerprint->platform
                                   : kHostPlatform),
      time_zone(profile.fingerprint ? profile.fingerprint->timezone : ""),
      proxy_enabled(profile.proxy.has_value()) {
  if (profile.proxy) {
    const ProxyConfig& proxy = *profile.proxy;
    proxy_type = proxy.type;
    proxy_host = proxy.host;
    proxy_port = base::NumberToString(proxy.port);
    proxy_username = proxy.username.value_or("");
    proxy_password = proxy.password.value_or("");
  }
}

ProfileDraft::ProfileDraft(const ProfileDraft&) = default;
ProfileDraft& ProfileDraft::operator=(const ProfileDraft&) = default;
ProfileDraft::~ProfileDraft() = default;

std::string_view ProfileDraft::TrimmedName() const {
  return TrimSpaces(name);
}

std::optional<ProfileDraft::Problem> ProfileDraft::Validate() const {
  if (TrimmedName().empty()) {
    return Problem::kNameMissing;
  }
  if (!proxy_enabled) {
    return std::nullopt;
  }
  if (const std::optional<Problem> host = HostProblem(TrimSpaces(proxy_host))) {
    return host;
  }
  return ProxyPort(proxy_port) ? std::nullopt
                               : std::make_optional(Problem::kPortInvalid);
}

std::optional<ProfileDraft::Problem> ProfileDraft::Remaining(
    Problem problem) const {
  switch (problem) {
    case Problem::kNameMissing:
      return TrimmedName().empty() ? std::make_optional(Problem::kNameMissing)
                                   : std::nullopt;
    case Problem::kHostMissing:
    case Problem::kHostInvalid:
      return proxy_enabled ? HostProblem(TrimSpaces(proxy_host)) : std::nullopt;
    case Problem::kPortInvalid:
      return proxy_enabled && !ProxyPort(proxy_port)
                 ? std::make_optional(Problem::kPortInvalid)
                 : std::nullopt;
  }
}

std::optional<ProxyConfig> ProfileDraft::Proxy() const {
  const std::string_view host = TrimSpaces(proxy_host);
  const std::optional<int> port = ProxyPort(proxy_port);
  if (!proxy_enabled || host.empty() || !port) {
    return std::nullopt;
  }
  ProxyConfig proxy;
  proxy.type = proxy_type;
  proxy.host = std::string(host);
  proxy.port = *port;
  proxy.username = UnlessEmpty(proxy_username);
  proxy.password = UnlessEmpty(proxy_password);
  return proxy;
}

bool ProfileDraft::PasteProxy(std::string_view raw) {
  std::optional<ProxyString> parsed = ProxyString::Parse(raw);
  if (!parsed) {
    return false;
  }
  proxy_host = std::move(parsed->host);
  proxy_port = base::NumberToString(parsed->port);
  if (parsed->username) {
    proxy_username = std::move(*parsed->username);
  }
  if (parsed->password) {
    proxy_password = std::move(*parsed->password);
  }
  return true;
}

void ProfileDraft::ApplyTo(BrowserProfile& profile,
                           base::FunctionRef<Fingerprint()> generate) const {
  profile.name = std::string(TrimmedName());
  profile.color = color;
  profile.group = UnlessEmpty(TrimSpaces(group));
  profile.SetSavesHistory(saves_history);
  profile.fingerprint =
      EditedFingerprint(profile.fingerprint, spoofing, platform, generate);
  if (profile.fingerprint) {
    profile.fingerprint->timezone = time_zone;
  }
  profile.proxy = Proxy();
}

std::string_view MessageOf(ProfileDraft::Problem problem) {
  switch (problem) {
    case ProfileDraft::Problem::kNameMissing:
      return "Profile name is required";
    case ProfileDraft::Problem::kHostMissing:
      return "Proxy host is required";
    case ProfileDraft::Problem::kHostInvalid:
      return "Host must be a name or an IP address";
    case ProfileDraft::Problem::kPortInvalid:
      return "Port must be between 1 and 65535";
  }
}

}  // namespace pane::model
