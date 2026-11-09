// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROFILE_CONTEXTS_H_
#define PANE_BROWSER_PROFILE_CONTEXTS_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"

class Profile;
class ScopedProfileKeepAlive;

namespace pane {

class AppData;

namespace model {
class BrowserModel;
}  // namespace model

// Each profile's context in the engine (the Swift app's ProfileContexts): a
// Chrome profile, held while open. It opens on first use, and only once the
// volume is attached and the profile's folders, links, and files are up to
// date (the vault's Prepare, ADR 0113 and 0121), the single way a profile
// reaches the engine: its fingerprint set, its caches kept in memory when it
// saves no history, then the engine opens it under its key. A lock closes
// every one, waiting for one that is opening (ADR 0116). On the window's
// sequence.
class ProfileContexts {
 public:
  // `data` and `model` outlive it.
  ProfileContexts(AppData& data, model::BrowserModel& model);
  ProfileContexts(const ProfileContexts&) = delete;
  ProfileContexts& operator=(const ProfileContexts&) = delete;
  // Each profile's hold goes, as Chrome's windows let go of theirs at quit.
  ~ProfileContexts();

  // The profile, while open.
  Profile* Get(const std::string& profile_id) const;

  // Runs `body` with the profile, opening it first if needed; with null when
  // it would not open, so no caller is left half done.
  void With(const std::string& profile_id,
            base::OnceCallback<void(Profile*)> body);

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
  // A profile open in the engine, and its hold.
  struct Open {
    Open();
    Open(Open&&);
    Open& operator=(Open&&);
    ~Open();

    raw_ptr<Profile> profile = nullptr;
    std::unique_ptr<ScopedProfileKeepAlive> keep_alive;
  };

  // What a lock waits for: every close it started, answered.
  class Closing;

  // An open under way: a lock that comes first closes it once it has opened.
  struct Work;

  void Attached(const std::string& profile_id,
                scoped_refptr<Work> work,
                bool attached);
  void Prepared(const std::string& profile_id,
                scoped_refptr<Work> work,
                bool prepared);
  void ProfileOpened(const std::string& profile_id,
                     scoped_refptr<Work> work,
                     Profile* profile);
  void Opened(const std::string& profile_id,
              scoped_refptr<Work> work,
              Profile* profile,
              std::unique_ptr<ScopedProfileKeepAlive> keep_alive);
  void DeleteOpened(const std::string& profile_id,
                    base::OnceCallback<void(bool)> done,
                    Profile* profile);
  void Deleted(scoped_refptr<Work> work,
               base::OnceCallback<void(bool)> done,
               bool deleted);

  const raw_ref<AppData> data_;
  const raw_ref<model::BrowserModel> model_;
  std::map<std::string, Open> open_;
  std::map<std::string, std::vector<base::OnceCallback<void(Profile*)>>>
      waiters_;
  std::set<scoped_refptr<Work>> under_way_;
  base::WeakPtrFactory<ProfileContexts> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_PROFILE_CONTEXTS_H_
