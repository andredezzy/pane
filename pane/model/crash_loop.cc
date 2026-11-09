// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/crash_loop.h"

namespace pane::model {

CrashLoop::CrashLoop() = default;
CrashLoop::~CrashLoop() = default;

bool CrashLoop::Crashed(const std::string& tab_id, base::Time now) {
  const auto last = last_crash_.find(tab_id);
  const bool looping =
      last != last_crash_.end() && now - last->second < kWindow;
  last_crash_[tab_id] = now;
  return looping;
}

void CrashLoop::Forget(const std::string& tab_id) {
  last_crash_.erase(tab_id);
}

}  // namespace pane::model
