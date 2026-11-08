// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_UI_APP_H_
#define PANE_UI_APP_UI_APP_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"

class Profile;
class ScopedProfileKeepAlive;

namespace pane {

class Automation;
struct SidebarRow;
class Window;

// Pane's Views UI, as a launch of Chromium's loader starts it
// (StartViewsLaunch): the profiles, each a Chrome profile under its engine
// key, and the window that shows them. For now the move to Views'
// prototype (docs/plans/views-migration.md, Phase 1): profiles live as long
// as the launch, and a scenario makes them through the channel.
class UiApp {
 public:
  struct ProfileEntry {
    ProfileEntry();
    ProfileEntry(ProfileEntry&&);
    ProfileEntry& operator=(ProfileEntry&&);
    ~ProfileEntry();

    std::string id;
    std::string name;
    raw_ptr<Profile> profile = nullptr;
    std::unique_ptr<ScopedProfileKeepAlive> keep_alive;
  };

  // Starts it, once the browser has started.
  static void Start();
  static UiApp* Get();

  UiApp(const UiApp&) = delete;
  UiApp& operator=(const UiApp&) = delete;
  ~UiApp();

  Window* window() { return window_.get(); }
  const std::vector<ProfileEntry>& profiles() const { return profiles_; }

  // A new profile named `name`, with an engine key of its own; `done` gets
  // its id once it opened, or nothing when it couldn't.
  void CreateProfile(const std::string& name,
                     base::OnceCallback<void(std::string)> done);
  Profile* ProfileWithId(const std::string& id) const;

  // What shows, for the channel's replies: the window, its profiles and
  // tabs, and what is selected.
  base::DictValue Snapshot() const;

 private:
  UiApp();

  std::vector<SidebarRow> Rows(Window& window) const;

  std::vector<ProfileEntry> profiles_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<Automation> automation_;
  base::WeakPtrFactory<UiApp> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_APP_UI_APP_H_
