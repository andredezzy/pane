// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profile_contexts.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "pane/app/data_launch.h"
#include "pane/browser/caches_in_memory.h"
#include "pane/browser/profiles.h"
#include "pane/model/browser_model.h"
#include "pane/model/data_key.h"
#include "pane/model/profile.h"

namespace pane {

class ProfileContexts::Closing : public base::RefCounted<Closing> {
 public:
  explicit Closing(base::OnceCallback<void(bool)> done)
      : done_(std::move(done)) {}

  // One more close to wait for, answered by what this returns.
  base::OnceCallback<void(bool)> Add() {
    ++pending_;
    return base::BindOnce(&Closing::Finished, base::WrapRefCounted(this));
  }

  // Every close is added: the answer comes once they have all come.
  void Seal() {
    sealed_ = true;
    MaybeDone();
  }

 private:
  friend class base::RefCounted<Closing>;
  ~Closing() = default;

  void Finished(bool closed) {
    every_closed_ = every_closed_ && closed;
    --pending_;
    MaybeDone();
  }

  void MaybeDone() {
    if (sealed_ && pending_ == 0 && done_) {
      std::move(done_).Run(every_closed_);
    }
  }

  int pending_ = 0;
  bool sealed_ = false;
  bool every_closed_ = true;
  base::OnceCallback<void(bool)> done_;
};

struct ProfileContexts::Work : base::RefCounted<Work> {
  // Set by a lock that came first: the profile closes for it, as it is.
  base::OnceCallback<void(bool)> lock;
  bool locked = false;

 private:
  friend class base::RefCounted<Work>;
  ~Work() = default;
};

ProfileContexts::Open::Open() = default;
ProfileContexts::Open::Open(Open&&) = default;
ProfileContexts::Open& ProfileContexts::Open::operator=(Open&&) = default;
ProfileContexts::Open::~Open() = default;

ProfileContexts::ProfileContexts(AppData& data, model::BrowserModel& model)
    : data_(data), model_(model) {}

ProfileContexts::~ProfileContexts() = default;

Profile* ProfileContexts::Get(const std::string& profile_id) const {
  const auto open = open_.find(profile_id);
  return open == open_.end() ? nullptr : open->second.profile.get();
}

void ProfileContexts::With(const std::string& profile_id,
                           base::OnceCallback<void(Profile*)> body) {
  if (Profile* profile = Get(profile_id)) {
    std::move(body).Run(profile);
    return;
  }
  std::vector<base::OnceCallback<void(Profile*)>>& waiting =
      waiters_[profile_id];
  waiting.push_back(std::move(body));
  if (waiting.size() > 1) {
    return;
  }

  auto work = base::MakeRefCounted<Work>();
  under_way_.insert(work);
  // The profile's engine data is in the volume: its context opens only once
  // the volume is attached and the profile's links lead into it, so nothing
  // of it lands in the clear (ADR 0113).
  data_->WhenAttached(base::BindOnce(&ProfileContexts::Attached,
                                     weak_factory_.GetWeakPtr(), profile_id,
                                     work));
}

void ProfileContexts::Attached(const std::string& profile_id,
                               scoped_refptr<Work> work,
                               bool attached) {
  if (!attached || work->locked) {
    Opened(profile_id, std::move(work), nullptr, nullptr);
    return;
  }
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  data_->Prepare(
      profile_id, !profile || profile->saves_history,
      base::BindOnce(&ProfileContexts::Prepared, weak_factory_.GetWeakPtr(),
                     profile_id, std::move(work)));
}

void ProfileContexts::Prepared(const std::string& profile_id,
                               scoped_refptr<Work> work,
                               bool prepared) {
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!prepared || work->locked || !profile) {
    Opened(profile_id, std::move(work), nullptr, nullptr);
    return;
  }

  // The fingerprint goes to the engine before the profile's context opens,
  // since its network context takes the User-Agent as it is created.
  if (!SetProfileFingerprint(profile_id, profile->fingerprint)) {
    LOG(ERROR) << "Could not apply a profile's fingerprint";
  }
  // A profile that saves no history keeps its caches off the disk (ADR 0119).
  SetKeepsCachesInMemory(profile_id, !profile->saves_history);

  // Gone only behind a lock, whose work closes as it opens.
  const std::optional<model::Key> engine_key = data_->EngineKey(profile_id);
  if (!engine_key) {
    Opened(profile_id, std::move(work), nullptr, nullptr);
    return;
  }
  OpenProfile(
      profile_id, *engine_key,
      base::BindOnce(&ProfileContexts::ProfileOpened,
                     weak_factory_.GetWeakPtr(), profile_id, std::move(work)));
}

void ProfileContexts::ProfileOpened(const std::string& profile_id,
                                    scoped_refptr<Work> work,
                                    Profile* profile) {
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive;
  if (profile) {
    keep_alive = std::make_unique<ScopedProfileKeepAlive>(
        profile, ProfileKeepAliveOrigin::kBrowserWindow);
    if (!work->locked) {
      ApplyProfileDefaults(profile);
      ApplyFingerprintLanguages(profile);
    }
  } else {
    LOG(ERROR) << "Could not open a profile";
  }
  Opened(profile_id, std::move(work), profile, std::move(keep_alive));
}

// The open ended: what waited has the profile, or null when it would not
// open. A lock that came first closes it instead.
void ProfileContexts::Opened(
    const std::string& profile_id,
    scoped_refptr<Work> work,
    Profile* profile,
    std::unique_ptr<ScopedProfileKeepAlive> keep_alive) {
  under_way_.erase(work);
  if (work->locked) {
    if (profile) {
      CloseProfile(profile, std::move(keep_alive), std::move(work->lock));
    } else {
      std::move(work->lock).Run(true);
    }
    return;
  }

  std::vector<base::OnceCallback<void(Profile*)>> waiting;
  if (const auto found = waiters_.find(profile_id); found != waiters_.end()) {
    waiting = std::move(found->second);
    waiters_.erase(found);
  }
  if (profile) {
    Open& open = open_[profile_id];
    open.profile = profile;
    open.keep_alive = std::move(keep_alive);
    data_->ProfileDidOpen(profile_id);
  }
  for (base::OnceCallback<void(Profile*)>& body : waiting) {
    std::move(body).Run(profile);
  }
}

void ProfileContexts::CloseForLock(
    base::OnceCallback<void(bool every_closed)> closed) {
  auto closing = base::MakeRefCounted<Closing>(std::move(closed));
  std::map<std::string, Open> open = std::exchange(open_, {});
  for (auto& [id, context] : open) {
    CloseProfile(context.profile, std::move(context.keep_alive),
                 closing->Add());
  }
  // Each open under way answers the first lock that came: one still under
  // way at a later lock is that first lock's to wait for, whose detach the
  // later one's waits behind.
  for (const scoped_refptr<Work>& work : std::exchange(under_way_, {})) {
    work->locked = true;
    work->lock = closing->Add();
  }
  std::map<std::string, std::vector<base::OnceCallback<void(Profile*)>>>
      waiting = std::exchange(waiters_, {});
  closing->Seal();
  for (auto& [id, bodies] : waiting) {
    for (base::OnceCallback<void(Profile*)>& body : bodies) {
      std::move(body).Run(nullptr);
    }
  }
}

}  // namespace pane
