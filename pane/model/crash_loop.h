// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_CRASH_LOOP_H_
#define PANE_MODEL_CRASH_LOOP_H_

#include <map>
#include <string>

#include "base/time/time.h"

namespace pane::model {

// When each tab's renderer last died, to tell a crash loop from a crash. The
// first crash reloads the page, as a slept tab wakes; a page that dies again
// within a minute shows the sad tab instead of looping.
class CrashLoop {
 public:
  static constexpr base::TimeDelta kWindow = base::Seconds(60);

  CrashLoop();
  ~CrashLoop();

  // Records the crash; whether it is a second one within the window.
  bool Crashed(const std::string& tab_id, base::Time now);

  // A tab reloaded from its sad tab, released, or closed starts over.
  void Forget(const std::string& tab_id);

 private:
  std::map<std::string, base::Time> last_crash_;
};

}  // namespace pane::model

#endif  // PANE_MODEL_CRASH_LOOP_H_
