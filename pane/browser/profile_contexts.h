// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILE_CONTEXTS_H_
#define PANE_BROWSER_PROFILE_CONTEXTS_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

class Profile;

namespace pane {

class AppData;
class BrowserContext;
class ProfileNetwork;

namespace model {
class BrowserModel;
}  // namespace model

// Each profile's context in the engine (the Swift app's ProfileContexts and
// ContextClosing, docs/plans/views-migration.md, N3): a Chrome profile, held
// while open (BrowserContext). It opens on first use, and only once the
// volume is attached and the profile's folders, links, and files are up to
// date (the vault's Prepare, ADR 0113 and 0121), the single way a profile
// reaches the engine: its fingerprint set, its caches kept in memory when it
// saves no history, the engine opens it under its key, its history choice
// and a clear it owes reach it, and its proxy goes on before any of its
// pages loads (ADR 0037). A lock closes every one, waiting for one that is
// opening (ADR 0116). On the window's sequence.
class ProfileContexts {
 public:
  // What a profile's close does to the window's pages: a profile turned off
  // closes to keep its caches in memory, its pages made again in the next
  // context, the selected one at once (ADR 0119).
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Every page of the profile goes; its tabs stay.
    virtual void ReleasePages(const std::string& profile_id) = 0;
    // Whether a tab of the profile shows a page.
    virtual bool HasPages(const std::string& profile_id) const = 0;
    // The tab shows again, its page made once its profile opens again.
    virtual void ShowAgain(const std::string& tab_id) = 0;
  };

  // `data`, `model`, `network`, and `delegate` outlive it.
  ProfileContexts(AppData& data,
                  model::BrowserModel& model,
                  ProfileNetwork& network,
                  Delegate& delegate);
  ProfileContexts(const ProfileContexts&) = delete;
  ProfileContexts& operator=(const ProfileContexts&) = delete;
  // Each profile's hold goes, as Chrome's windows let go of theirs at quit.
  ~ProfileContexts();

  // The profile, while open.
  Profile* Get(const std::string& profile_id) const;
  BrowserContext* GetContext(const std::string& profile_id) const;

  // Runs `body` with the profile, opening it first if needed; with null when
  // it would not open, so no caller is left half done. Asked while it closes,
  // it opens again once it has.
  void With(const std::string& profile_id,
            base::OnceCallback<void(Profile*)> body);

  // Save history changed (ADR 0119). Off clears what the profile kept, now,
  // opening it if it is closed, as the sheet says, and closing it again; on
  // waits for it to open. The data's opening runs each clear still owed.
  void HistoryDidChange(const std::string& profile_id);

  // The profile's fingerprint, or Chrome's own values without one, goes to
  // the engine (before the context opens, since its network context takes
  // the User-Agent as it is made), and an open profile's languages follow.
  // Open pages keep their renderer's: the caller makes them again.
  void ApplyFingerprint(const std::string& profile_id);

  // Pane locks while open (ADR 0116): every profile closes in the engine, one
  // still opening once it has opened, and what waited for one gets none.
  // `closed` says whether every one closed, its files with it, which the
  // volume's detach waits for.
  void CloseForLock(base::OnceCallback<void(bool every_closed)> closed);

  // The profile's data goes in the engine (ADR 0020), a profile not open this
  // session opened first, so the data of earlier ones goes too. `done` says
  // whether its folder in the volume goes now: not when it would not open,
  // nor once a lock came first, which waits for the engine's answer and
  // leaves the folder to the next open (ProfileDeletions).
  void Delete(const std::string& profile_id,
              base::OnceCallback<void(bool removes_folder)> done);

 private:
  // What a lock waits for: every close it started, answered.
  class Closing;

  // An open, a close, or a delete under way: a lock that comes first waits
  // for it, and closes a profile that opens.
  struct Work;

  void Open(const std::string& profile_id);
  void Attached(const std::string& profile_id,
                scoped_refptr<Work> work,
                bool attached);
  void Prepared(const std::string& profile_id,
                scoped_refptr<Work> work,
                bool prepared);
  void ProfileOpened(const std::string& profile_id,
                     scoped_refptr<Work> work,
                     Profile* profile);
  void NetworkApplied(const std::string& profile_id,
                      scoped_refptr<Work> work,
                      std::unique_ptr<BrowserContext> context);
  void Opened(const std::string& profile_id,
              scoped_refptr<Work> work,
              std::unique_ptr<BrowserContext> context);
  void DeleteOpened(const std::string& profile_id,
                    base::OnceCallback<void(bool)> done,
                    Profile* profile);
  void Deleted(scoped_refptr<Work> work,
               base::OnceCallback<void(bool)> done,
               bool deleted);

  // The history choice reaches the engine, and a clear still owed runs. Both
  // are made up as the profile opens, whatever a quit, a crash, or a lock
  // cut short.
  void ApplyHistory(BrowserContext& context);
  void OpenedToClear(const std::string& profile_id, Profile* profile);
  // Done in the engine, on disk within its 10-second commit: until then a
  // crash loses the deletion, and the clear stays owed.
  void HistoryCleared(const std::string& profile_id);
  void HistoryClearSettled(const std::string& profile_id, int settling);
  void CloseIfOpenedToClearHistory(const std::string& profile_id);
  void ReopenCachingInMemory(const std::string& profile_id);
  // Closes an open profile in the engine for its history: what asks for it
  // as it closes waits, and opens it again.
  void Close(const std::string& profile_id);
  void Closed(const std::string& profile_id,
              scoped_refptr<Work> work,
              bool closed);

  const raw_ref<AppData> data_;
  const raw_ref<model::BrowserModel> model_;
  const raw_ref<ProfileNetwork> network_;
  const raw_ref<Delegate> delegate_;
  std::map<std::string, std::unique_ptr<BrowserContext>> open_;
  std::map<std::string, std::vector<base::OnceCallback<void(Profile*)>>>
      waiters_;
  std::set<scoped_refptr<Work>> under_way_;
  // Profiles closing for their history, which open again for what waits.
  std::set<std::string> closing_;
  // Closed profiles opened only to clear their history (HistoryDidChange).
  std::set<std::string> opened_to_clear_history_;
  // The profiles whose clear the engine is running, and, once it is done,
  // its wait to reach the disk, by the number of the wait.
  std::set<std::string> clearing_history_;
  std::map<std::string, int> history_clear_settling_;
  int next_settling_ = 0;
  // Open profiles whose caches are on disk: all that save history, and one
  // turned off since it opened, until it opens again.
  std::set<std::string> caching_on_disk_;
  base::WeakPtrFactory<ProfileContexts> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_PROFILE_CONTEXTS_H_
