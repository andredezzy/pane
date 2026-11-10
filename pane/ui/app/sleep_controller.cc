// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/sleep_controller.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/profile_cache.h"
#include "pane/model/browser_model.h"
#include "pane/model/test_hooks.h"

namespace pane {

SleepController::ScaledClock::ScaledClock(double scale)
    : start_(base::Time::Now()), scale_(scale) {}

base::Time SleepController::ScaledClock::Now() const {
  return start_ + (base::Time::Now() - start_) * scale_;
}

// A profile's live pages, for one tick.
class SleepController::ProfileTabs : public model::SleepableTabs {
 public:
  ProfileTabs(const model::BrowserModel& model,
              Delegate& delegate,
              std::string profile_id)
      : model_(model),
        delegate_(delegate),
        profile_id_(std::move(profile_id)) {}

  // model::SleepableTabs:
  std::vector<std::string> LoadedTabIds() const override {
    std::vector<std::string> loaded;
    if (const model::BrowserProfile* profile =
            model_->FindProfile(profile_id_)) {
      for (const model::Tab& tab : profile->tabs) {
        if (delegate_->PageOf(tab.id)) {
          loaded.push_back(tab.id);
        }
      }
    }
    return loaded;
  }

  // Playing sound (Chrome's audio indicator), inspected in DevTools, or in
  // picture in picture, a video's or a document's: in use, though not
  // selected, and a floating player would otherwise vanish as it plays.
  bool IsProtected(const std::string& tab_id) const override {
    content::WebContents* page = delegate_->PageOf(tab_id);
    return page && (page->IsCurrentlyAudible() ||
                    DevToolsWindow::GetInstanceForInspectedWebContents(page) ||
                    page->HasPictureInPictureVideo() ||
                    page->HasPictureInPictureDocument());
  }

  void Sleep(const std::string& tab_id) override {
    delegate_->SleepTab(tab_id);
  }

  void UnloadAll() override { delegate_->UnloadProfile(profile_id_); }

 private:
  const raw_ref<const model::BrowserModel> model_;
  const raw_ref<Delegate> delegate_;
  const std::string profile_id_;
};

// A profile's cache, measured and trimmed by the engine, for as long as the
// profile is open.
class SleepController::ProfileCache : public model::TrimmableCache {
 public:
  explicit ProfileCache(Profile* profile) : profile_(profile->GetWeakPtr()) {}

  bool Holds(const Profile* profile) const { return profile_.get() == profile; }

  base::WeakPtr<model::TrimmableCache> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // model::TrimmableCache:
  void ComputeSize(SizeCallback done) override {
    ComputeCacheSize(profile_.get(), base::BindOnce(
                                         [](SizeCallback done, int64_t bytes) {
                                           std::move(done).Run(bytes);
                                         },
                                         std::move(done)));
  }

  void Trim(TrimCallback done) override {
    TrimCache(profile_.get(),
              base::BindOnce(
                  [](TrimCallback done) { std::move(done).Run(base::ok()); },
                  std::move(done)));
  }

 private:
  base::WeakPtr<Profile> profile_;
  base::WeakPtrFactory<ProfileCache> weak_factory_{this};
};

SleepController::SleepController(const model::BrowserModel& model,
                                 Delegate& delegate)
    : model_(model),
      delegate_(delegate),
      clock_(model::TestTimeScale()),
      scheduler_(
          [&] {
            std::vector<std::string> ids;
            for (const model::BrowserProfile& profile : model.profiles()) {
              ids.push_back(profile.id);
            }
            return ids;
          }(),
          model.active_profile_id(),
          model.active_tab_id(),
          &clock_) {
  timer_.Start(
      FROM_HERE, model::SleepScheduler::kTickInterval / model::TestTimeScale(),
      base::BindRepeating(&SleepController::Tick, base::Unretained(this)));
}

SleepController::~SleepController() = default;

void SleepController::SelectionChanged() {
  scheduler_.ActiveDidChange(model_->active_profile_id(),
                             model_->active_tab_id());
}

void SleepController::Tick() {
  std::vector<std::unique_ptr<ProfileTabs>> tabs;
  std::map<std::string, model::SleepScheduler::Profile> profiles;
  std::map<std::string, std::unique_ptr<ProfileCache>> caches;
  for (const model::BrowserProfile& profile : model_->profiles()) {
    Profile* open = delegate_->ProfileIfOpen(profile.id);
    if (!open) {
      continue;
    }
    // The cache of the profile as it is open now: one closed and opened
    // again since is another.
    std::unique_ptr<ProfileCache>& cache = caches[profile.id];
    if (auto kept = caches_.find(profile.id);
        kept != caches_.end() && kept->second->Holds(open)) {
      cache = std::move(kept->second);
    } else {
      cache = std::make_unique<ProfileCache>(open);
    }
    tabs.push_back(
        std::make_unique<ProfileTabs>(*model_, *delegate_, profile.id));
    profiles[profile.id] = {.tabs = tabs.back().get(),
                            .cache = cache->GetWeakPtr(),
                            .keep_loaded = profile.keep_loaded};
  }
  // A profile no longer open takes its cache with it, and a trim under way
  // there is dropped (the scheduler's next unload of it tries again).
  caches_ = std::move(caches);
  scheduler_.Tick(profiles, model_->settings());
}

}  // namespace pane
