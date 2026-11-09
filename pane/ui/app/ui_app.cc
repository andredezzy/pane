// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/ui_app.h"

#include <array>
#include <utility>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "crypto/random.h"
#include "pane/automation/automation.h"
#include "pane/browser/profiles.h"
#include "pane/ui/tabs/tab_id.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

UiApp* g_app = nullptr;

// A new window's size, as the Swift app's first one (ADR 0016).
constexpr gfx::Size kWindowSize(1280, 800);

}  // namespace

UiApp::ProfileEntry::ProfileEntry() = default;
UiApp::ProfileEntry::ProfileEntry(ProfileEntry&&) = default;
UiApp::ProfileEntry& UiApp::ProfileEntry::operator=(ProfileEntry&&) = default;
UiApp::ProfileEntry::~ProfileEntry() = default;

// static
void UiApp::Start() {
  CHECK(!g_app);
  g_app = new UiApp();
  // Profiles let go as quitting starts, while the message loop still runs,
  // as Chrome's windows do when they close: they then unload with their
  // processes.
  static base::NoDestructor<base::CallbackListSubscription> terminating(
      browser_shutdown::AddAppTerminatingCallback(
          base::BindOnce([] { delete std::exchange(g_app, nullptr); })));
}

// static
UiApp* UiApp::Get() {
  return g_app;
}

UiApp::UiApp() {
  const gfx::Rect work_area =
      display::Screen::Get()->GetPrimaryDisplay().work_area();
  gfx::Rect bounds(kWindowSize);
  bounds.set_origin(
      work_area.CenterPoint() -
      gfx::Vector2d(kWindowSize.width() / 2, kWindowSize.height() / 2));
  window_ = std::make_unique<Window>(
      bounds, base::BindRepeating(&UiApp::Rows, base::Unretained(this)),
      base::RepeatingClosure());
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  // A scenario's copy takes the front only when the runner allows it
  // (run.ts --foreground): otherwise the app in use keeps it, and its user's
  // keys stay theirs.
  window_->Show(environment->GetVar("PANE_AUTOMATION_DIR").has_value()
                    ? environment->GetVar("PANE_AUTOMATION_FOREGROUND") ==
                          std::optional<std::string>("1")
                    : true);

  if (std::optional<std::string> directory =
          environment->GetVar("PANE_AUTOMATION_DIR");
      directory && !directory->empty()) {
    automation_ = std::make_unique<Automation>(
        base::FilePath::FromUTF8Unsafe(*directory), this);
  }
}

UiApp::~UiApp() {
  automation_.reset();
  window_.reset();
  profiles_.clear();
}

void UiApp::CreateProfile(const std::string& name,
                          base::OnceCallback<void(std::string)> done) {
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::array<uint8_t, 32> key;
  crypto::RandBytes(key);
  OpenProfile(
      id, key,
      base::BindOnce(
          [](base::WeakPtr<UiApp> app, std::string id, std::string name,
             base::OnceCallback<void(std::string)> done, Profile* profile) {
            if (!app || !profile) {
              std::move(done).Run(std::string());
              return;
            }
            ApplyProfileDefaults(profile);
            ProfileEntry entry;
            entry.id = id;
            entry.name = name;
            entry.profile = profile;
            entry.keep_alive = std::make_unique<ScopedProfileKeepAlive>(
                profile, ProfileKeepAliveOrigin::kBrowserWindow);
            app->profiles_.push_back(std::move(entry));
            app->window_->host().BrowserFor(profile);
            std::move(done).Run(id);
          },
          weak_factory_.GetWeakPtr(), id, name, std::move(done)));
}

Profile* UiApp::ProfileWithId(const std::string& id) const {
  for (const ProfileEntry& entry : profiles_) {
    if (entry.id == id) {
      return entry.profile;
    }
  }
  return nullptr;
}

std::vector<SidebarRow> UiApp::Rows(Window& window) const {
  std::vector<SidebarRow> rows;
  for (const ProfileEntry& entry : profiles_) {
    rows.push_back(
        {.profile_id = entry.id, .title = base::UTF8ToUTF16(entry.name)});
    Browser* browser = window.host().ExistingBrowserFor(entry.profile);
    if (!browser) {
      continue;
    }
    TabStripModel* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      std::u16string title = contents->GetTitle();
      if (title.empty()) {
        title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
      }
      rows.push_back({.profile_id = entry.id,
                      .tab_id = TabIdOf(contents),
                      .title = title,
                      .selected = contents == window.selected()});
    }
  }
  return rows;
}

base::DictValue UiApp::Snapshot() const {
  base::ListValue profiles;
  std::string active_profile;
  for (const ProfileEntry& entry : profiles_) {
    base::ListValue tabs;
    if (Browser* browser = window_->host().ExistingBrowserFor(entry.profile)) {
      TabStripModel* model = browser->GetTabStripModel();
      for (int i = 0; i < model->count(); ++i) {
        content::WebContents* contents = model->GetWebContentsAt(i);
        tabs.Append(base::DictValue()
                        .Set("id", TabIdOf(contents))
                        .Set("url", contents->GetVisibleURL().spec())
                        .Set("title", contents->GetTitle())
                        .Set("loading", contents->IsLoading()));
      }
    }
    if (window_->host().active_profile() == entry.profile) {
      active_profile = entry.id;
    }
    profiles.Append(base::DictValue()
                        .Set("id", entry.id)
                        .Set("name", entry.name)
                        .Set("tabs", std::move(tabs)));
  }
  content::WebContents* selected = window_->selected();
  views::Widget* widget = window_->host().widget();
  return base::DictValue()
      .Set("profiles", std::move(profiles))
      .Set("activeTabID",
           selected ? base::Value(TabIdOf(selected)) : base::Value())
      .Set("activeProfileID",
           active_profile.empty() ? base::Value() : base::Value(active_profile))
      .Set("page", "browser")
      .Set("window", base::DictValue()
                         .Set("visible", widget && widget->IsVisible())
                         .Set("key", widget && widget->IsActive())
                         .Set("fullScreen", widget && widget->IsFullscreen()))
      .Set("security", base::DictValue()
                           .Set("locked", false)
                           .Set("data", base::DictValue()
                                            .Set("phase", "open")
                                            .Set("attached", true)
                                            .Set("failure", base::Value())));
}

}  // namespace pane
