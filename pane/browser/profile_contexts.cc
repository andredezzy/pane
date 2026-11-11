// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profile_contexts.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/after_startup_task_utils.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "pane/app/data_launch.h"
#include "pane/browser/background_work.h"
#include "pane/browser/browser_context.h"
#include "pane/browser/caches_in_memory.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/profile_network.h"
#include "pane/browser/profiles.h"
#include "pane/model/browser_model.h"
#include "pane/model/data_key.h"
#include "pane/model/profile.h"

namespace pane {

namespace {

// How long a clear's deletion takes to reach the disk: the engine commits
// within 10 s of a change.
constexpr base::TimeDelta kHistoryClearSettles = base::Seconds(15);

// How long a lock lets Chrome's background work run before its detach: past
// it, the lock goes on as before (ADR 0116).
constexpr base::TimeDelta kBackgroundWorkCap = base::Seconds(5);

// Every profile has closed for a lock. As each was created and as it was
// destroyed, Chrome left work in the thread pool that reaches its files, much
// of it BEST_EFFORT (ADR 0116): it runs now, while the volume is there and the
// keys are held, and the detach and the drop come after. Held past the drop,
// it met the encryption's check of a profile's file with no key, which ends
// Pane (ADR 0121). Chrome holds every BEST_EFFORT task until its startup is
// over, once a visible page has loaded or 3 minutes after launch: a lock that
// came first, right after an unlock or with no page loading, left that work
// held behind it until the 3 minutes ran out, so a lock ends the startup.
// Each close answers as its profile's destruction begins, which posts work of
// its own as the profile's services go, so the wait starts once that has
// ended. A profile still open makes Pane quit, which needs no wait.
void LetBackgroundWorkRun(base::OnceCallback<void(bool)> closed,
                          bool every_closed) {
  if (!every_closed) {
    std::move(closed).Run(false);
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::OnceCallback<void(bool)> closed) {
            if (!AfterStartupTaskUtils::IsBrowserStartupComplete()) {
              // Chromium's own call, as Android's startup ends it; its pages
              // went behind the Lock.
              AfterStartupTaskUtils::SetBrowserStartupIsCompleteForTesting(
                  StartupIsCompleteReason::kVisiblePagesHidden);
            }
            WaitForBackgroundWork(
                kBackgroundWorkCap,
                base::BindOnce(
                    [](base::TimeTicks started,
                       base::OnceCallback<void(bool)> closed, bool ran) {
                      PANE_TRACE(
                          "callback",
                          "PaneBrowserApplication.backgroundWorkRan",
                          base::DictValue()
                              .Set("ran", ran)
                              .Set("ms", (base::TimeTicks::Now() - started)
                                             .InMillisecondsF()));
                      std::move(closed).Run(true);
                    },
                    base::TimeTicks::Now(), std::move(closed)));
          },
          std::move(closed)));
}

}  // namespace

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
  // Set by a lock that came first: what it ends answers the lock, and a
  // profile it opens closes for it, as it is.
  base::OnceCallback<void(bool)> lock;
  bool locked = false;

 private:
  friend class base::RefCounted<Work>;
  ~Work() = default;
};

ProfileContexts::ProfileContexts(AppData& data,
                                 model::BrowserModel& model,
                                 ProfileNetwork& network,
                                 Delegate& delegate)
    : data_(data), model_(model), network_(network), delegate_(delegate) {}

ProfileContexts::~ProfileContexts() = default;

Profile* ProfileContexts::Get(const std::string& profile_id) const {
  const BrowserContext* context = GetContext(profile_id);
  return context ? context->profile() : nullptr;
}

BrowserContext* ProfileContexts::GetContext(
    const std::string& profile_id) const {
  const auto open = open_.find(profile_id);
  return open == open_.end() ? nullptr : open->second.get();
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
  // An open under way answers it; a close under way opens it again after.
  if (waiting.size() > 1 || closing_.contains(profile_id)) {
    return;
  }
  Open(profile_id);
}

void ProfileContexts::Open(const std::string& profile_id) {
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
    Opened(profile_id, std::move(work), nullptr);
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
    Opened(profile_id, std::move(work), nullptr);
    return;
  }

  // The fingerprint goes to the engine before the profile's context opens,
  // since its network context takes the User-Agent as it is created.
  ApplyFingerprint(profile_id);
  // A profile that saves no history keeps its caches off the disk (ADR 0119).
  SetKeepsCachesInMemory(profile_id, !profile->saves_history);

  // Gone only behind a lock, whose work closes as it opens.
  const std::optional<model::Key> engine_key = data_->EngineKey(profile_id);
  if (!engine_key) {
    Opened(profile_id, std::move(work), nullptr);
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
  if (!profile) {
    LOG(ERROR) << "Could not open a profile";
    Opened(profile_id, std::move(work), nullptr);
    return;
  }
  auto context = std::make_unique<BrowserContext>(profile_id, profile);
  // A lock came first: the profile closes for it, as it is.
  if (work->locked) {
    Opened(profile_id, std::move(work), std::move(context));
    return;
  }
  ApplyProfileDefaults(profile);
  ApplyFingerprintLanguages(profile);
  const model::BrowserProfile* saved = model_->FindProfile(profile_id);
  if (!saved || saved->saves_history) {
    caching_on_disk_.insert(profile_id);
  }
  ApplyHistory(*context);
  // The proxy goes on before any page of the profile loads.
  BrowserContext& opening = *context;
  network_->Apply(opening,
                  base::BindOnce(&ProfileContexts::NetworkApplied,
                                 weak_factory_.GetWeakPtr(), profile_id,
                                 std::move(work), std::move(context)));
}

void ProfileContexts::NetworkApplied(const std::string& profile_id,
                                     scoped_refptr<Work> work,
                                     std::unique_ptr<BrowserContext> context) {
  Opened(profile_id, std::move(work), std::move(context));
}

// The open ended: what waited has the profile, or null when it would not
// open. A lock that came first closes it instead.
void ProfileContexts::Opened(const std::string& profile_id,
                             scoped_refptr<Work> work,
                             std::unique_ptr<BrowserContext> context) {
  under_way_.erase(work);
  if (work->locked) {
    if (context) {
      caching_on_disk_.erase(profile_id);
      Profile* profile = context->profile();
      std::unique_ptr<ScopedProfileKeepAlive> keep_alive =
          context->TakeKeepAlive();
      context.reset();
      network_->Stop(profile_id);
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
  Profile* profile = nullptr;
  if (context) {
    profile = context->profile();
    open_[profile_id] = std::move(context);
    data_->ProfileDidOpen(profile_id);
  } else {
    opened_to_clear_history_.erase(profile_id);
  }
  for (base::OnceCallback<void(Profile*)>& body : waiting) {
    std::move(body).Run(profile);
  }
}

void ProfileContexts::ApplyFingerprint(const std::string& profile_id) {
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!profile) {
    return;
  }
  if (!SetProfileFingerprint(profile_id, profile->fingerprint)) {
    LOG(ERROR) << "Could not apply a profile's fingerprint";
  }
  if (Profile* open = Get(profile_id)) {
    ApplyFingerprintLanguages(open);
  }
}

void ProfileContexts::HistoryDidChange(const std::string& profile_id) {
  if (BrowserContext* context = GetContext(profile_id)) {
    ApplyHistory(*context);
    return;
  }
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!profile || !profile->clears_history) {
    return;
  }
  // Opening for something else, it stays open for that.
  if (!waiters_.contains(profile_id) && !closing_.contains(profile_id)) {
    opened_to_clear_history_.insert(profile_id);
  }
  With(profile_id, base::BindOnce(&ProfileContexts::OpenedToClear,
                                  weak_factory_.GetWeakPtr(), profile_id));
}

void ProfileContexts::OpenedToClear(const std::string& profile_id,
                                    Profile* profile) {
  BrowserContext* context = GetContext(profile_id);
  if (!profile || !context) {
    opened_to_clear_history_.erase(profile_id);
    return;
  }
  ApplyHistory(*context);
  CloseIfOpenedToClearHistory(profile_id);
}

void ProfileContexts::ApplyHistory(BrowserContext& context) {
  const std::string profile_id = context.id();
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!profile) {
    return;
  }
  if (context.SavesHistory() != profile->saves_history) {
    context.SetSavesHistory(profile->saves_history);
  }
  if (!profile->clears_history ||
      !clearing_history_.insert(profile_id).second) {
    return;
  }
  history_clear_settling_.erase(profile_id);
  model_->ForgetClosedTabs(profile_id);
  context.ClearHistory(base::BindOnce(&ProfileContexts::HistoryCleared,
                                      weak_factory_.GetWeakPtr(), profile_id));
}

void ProfileContexts::HistoryCleared(const std::string& profile_id) {
  clearing_history_.erase(profile_id);
  CloseIfOpenedToClearHistory(profile_id);
  ReopenCachingInMemory(profile_id);

  const int settling = ++next_settling_;
  history_clear_settling_[profile_id] = settling;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ProfileContexts::HistoryClearSettled,
                     weak_factory_.GetWeakPtr(), profile_id, settling),
      kHistoryClearSettles);
}

void ProfileContexts::HistoryClearSettled(const std::string& profile_id,
                                          int settling) {
  const auto found = history_clear_settling_.find(profile_id);
  if (found == history_clear_settling_.end() || found->second != settling) {
    return;
  }
  history_clear_settling_.erase(found);
  model_->UpdateProfile(profile_id, [](model::BrowserProfile& profile) {
    profile.clears_history = false;
  });
}

// A closed profile opened only to clear closes again, its memory and its
// proxy's relay with it, once it has both opened and cleared, unless
// something has come to use it meanwhile. What asks for it as it closes
// waits, and opens it again.
void ProfileContexts::CloseIfOpenedToClearHistory(
    const std::string& profile_id) {
  if (!opened_to_clear_history_.contains(profile_id) ||
      clearing_history_.contains(profile_id) || !GetContext(profile_id)) {
    return;
  }
  opened_to_clear_history_.erase(profile_id);
  // A page of it opened meanwhile, Chrome's extensions page among them.
  if (delegate_->HasPages(profile_id)) {
    return;
  }
  Close(profile_id);
}

// A profile turned off while open was still writing its caches to disk, and
// only a new context stops that: its pages start again, as for a new
// fingerprint, the selected one at once. A download it is running would end,
// so then the caches stay until the profile next opens, which removes them
// (the vault's Prepare).
void ProfileContexts::ReopenCachingInMemory(const std::string& profile_id) {
  BrowserContext* context = GetContext(profile_id);
  const model::BrowserProfile* profile = model_->FindProfile(profile_id);
  if (!caching_on_disk_.contains(profile_id) || !context || !profile ||
      profile->saves_history || context->HasRunningDownloads()) {
    return;
  }
  const std::optional<std::string> selected = model_->active_tab_id();
  const bool shows_one =
      selected && model_->ProfileIdOfTab(*selected) == profile_id;
  delegate_->ReleasePages(profile_id);
  Close(profile_id);
  if (shows_one) {
    delegate_->ShowAgain(*selected);
  }
}

void ProfileContexts::Close(const std::string& profile_id) {
  const auto found = open_.find(profile_id);
  if (found == open_.end()) {
    return;
  }
  std::unique_ptr<BrowserContext> context = std::move(found->second);
  open_.erase(found);
  caching_on_disk_.erase(profile_id);
  closing_.insert(profile_id);
  Profile* profile = context->profile();
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive = context->TakeKeepAlive();
  context.reset();
  network_->Stop(profile_id);
  // A lock that comes meanwhile waits for the engine's answer.
  auto work = base::MakeRefCounted<Work>();
  under_way_.insert(work);
  CloseProfile(profile, std::move(keep_alive),
               base::BindOnce(&ProfileContexts::Closed,
                              weak_factory_.GetWeakPtr(), profile_id, work));
}

// Closed, a clear it ran is on disk, and no longer owed.
void ProfileContexts::Closed(const std::string& profile_id,
                             scoped_refptr<Work> work,
                             bool closed) {
  under_way_.erase(work);
  closing_.erase(profile_id);
  if (work->locked) {
    std::move(work->lock).Run(closed);
    return;
  }
  if (closed) {
    history_clear_settling_.erase(profile_id);
    model_->UpdateProfile(profile_id, [](model::BrowserProfile& profile) {
      profile.clears_history = false;
    });
  }
  // What asked for it as it closed opens it again.
  std::vector<base::OnceCallback<void(Profile*)>> waiting;
  if (const auto found = waiters_.find(profile_id); found != waiters_.end()) {
    waiting = std::move(found->second);
    waiters_.erase(found);
  }
  for (base::OnceCallback<void(Profile*)>& body : waiting) {
    With(profile_id, std::move(body));
  }
}

void ProfileContexts::Delete(const std::string& profile_id,
                             base::OnceCallback<void(bool)> done) {
  With(profile_id,
       base::BindOnce(&ProfileContexts::DeleteOpened,
                      weak_factory_.GetWeakPtr(), profile_id, std::move(done)));
}

void ProfileContexts::DeleteOpened(const std::string& profile_id,
                                   base::OnceCallback<void(bool)> done,
                                   Profile* profile) {
  if (!profile) {
    std::move(done).Run(false);
    return;
  }
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive;
  if (const auto open = open_.find(profile_id); open != open_.end()) {
    keep_alive = open->second->TakeKeepAlive();
    open_.erase(open);
  }
  caching_on_disk_.erase(profile_id);
  clearing_history_.erase(profile_id);
  opened_to_clear_history_.erase(profile_id);
  history_clear_settling_.erase(profile_id);
  network_->Forget(profile_id);
  // A lock that comes meanwhile waits for the engine's answer.
  auto work = base::MakeRefCounted<Work>();
  under_way_.insert(work);
  DeleteProfile(
      profile, std::move(keep_alive),
      base::BindOnce(&ProfileContexts::Deleted, weak_factory_.GetWeakPtr(),
                     std::move(work), std::move(done)));
}

void ProfileContexts::Deleted(scoped_refptr<Work> work,
                              base::OnceCallback<void(bool)> done,
                              bool deleted) {
  under_way_.erase(work);
  if (work->locked) {
    std::move(work->lock).Run(deleted);
    std::move(done).Run(false);
    return;
  }
  std::move(done).Run(deleted);
}

void ProfileContexts::CloseForLock(
    base::OnceCallback<void(bool every_closed)> closed) {
  auto closing = base::MakeRefCounted<Closing>(
      base::BindOnce(&LetBackgroundWorkRun, std::move(closed)));
  std::map<std::string, std::unique_ptr<BrowserContext>> open =
      std::exchange(open_, {});
  for (auto& [id, context] : open) {
    Profile* profile = context->profile();
    std::unique_ptr<ScopedProfileKeepAlive> keep_alive =
        context->TakeKeepAlive();
    context.reset();
    network_->Stop(id);
    CloseProfile(profile, std::move(keep_alive), closing->Add());
  }
  // A clear cut short here is still owed, at the next open.
  clearing_history_.clear();
  opened_to_clear_history_.clear();
  caching_on_disk_.clear();
  // Each open, close, or delete under way answers the first lock that came:
  // one still under way at a later lock is that first lock's to wait for,
  // whose detach the later one's waits behind.
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
