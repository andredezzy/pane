// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_FINGERPRINTS_H_
#define PANE_BROWSER_FINGERPRINTS_H_

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/no_destructor.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace base {
class CommandLine;
}

namespace content {
class BrowserContext;
}

namespace pane {

// What a fingerprinted profile presents instead of Chrome's own: parsed from
// the JSON the app builds (PaneKit's EngineFingerprint).
struct Fingerprint {
  Fingerprint();
  Fingerprint(const Fingerprint&);
  Fingerprint& operator=(const Fingerprint&);
  ~Fingerprint();

  std::string user_agent;
  blink::UserAgentMetadata user_agent_metadata;
  // The Accept-Language list and navigator.languages.
  std::vector<std::string> languages;
  // What each of the profile's renderers reports, as JSON for
  // --pane-fingerprint (blink's PaneFingerprint).
  std::string renderer;
};

// The fingerprint of each profile, by its identifier (its directory's name).
// UI thread only. Set before the profile opens: its network context takes
// the User-Agent when it is created, and each renderer the rest when it
// starts.
class Fingerprints {
 public:
  static Fingerprints& Get();

  Fingerprints(const Fingerprints&) = delete;
  Fingerprints& operator=(const Fingerprints&) = delete;

  // Sets `identifier`'s fingerprint from `json`, or clears it for an empty
  // `json`. False, with `error`, for JSON that is not a fingerprint.
  bool Set(const std::string& identifier,
           std::string_view json,
           std::string* error);

  // The fingerprint of `browser_context`'s profile, or null.
  const Fingerprint* For(content::BrowserContext* browser_context) const;

  // Passes the fingerprint of the renderer `child_process_id`'s profile to it.
  void AppendSwitches(base::CommandLine* command_line,
                      int child_process_id) const;

 private:
  friend class base::NoDestructor<Fingerprints>;

  Fingerprints();
  ~Fingerprints();

  std::map<std::string, Fingerprint> fingerprints_;
};

}  // namespace pane

#endif  // PANE_BROWSER_FINGERPRINTS_H_
