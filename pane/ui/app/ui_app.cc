// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/ui_app.h"

#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
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
#include "pane/ui/window/frame.h"
#include "pane/ui/window/root_view.h"
#include "pane/ui/window/window.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace pane {

namespace {

UiApp* g_app = nullptr;

// The profiles' colors, in the order new profiles take them, until the
// model carries each profile's (ProfileColor's swatches, fixed in both
// appearances).
constexpr SkColor kProfileColors[] = {
    SkColorSetRGB(0x60, 0xA5, 0xFA), SkColorSetRGB(0xFB, 0x71, 0x85),
    SkColorSetRGB(0x34, 0xD3, 0x99), SkColorSetRGB(0xFB, 0xBF, 0x24),
    SkColorSetRGB(0xA7, 0x8B, 0xFA), SkColorSetRGB(0xF9, 0x73, 0x16),
    SkColorSetRGB(0x2D, 0xD4, 0xBF), SkColorSetRGB(0xE8, 0x79, 0xF9),
    SkColorSetRGB(0xA1, 0xA1, 0xAA)};

// The window's controls, as the Swift app's snapshot gave them: x, y, width,
// height from the window's top left.
base::ListValue ControlFrames(views::Widget* widget) {
  base::ListValue frames;
  if (!widget) {
    return frames;
  }
  for (const gfx::Rect& frame : WindowControlFrames(widget)) {
    frames.Append(base::ListValue()
                      .Append(frame.x())
                      .Append(frame.y())
                      .Append(frame.width())
                      .Append(frame.height()));
  }
  return frames;
}

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

  std::vector<GURL> urls;
  for (const base::CommandLine::StringType& arg :
       base::CommandLine::ForCurrentProcess()->GetArgs()) {
    const GURL url(base::FilePath(arg).AsUTF8Unsafe());
    if (url.is_valid()) {
      urls.push_back(url);
    }
  }
  OpenLaunchURLs(std::move(urls));
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

void UiApp::OpenLaunchURLs(std::vector<GURL> urls) {
  if (urls.empty()) {
    return;
  }
  Profile* profile = window_->host().active_profile();
  if (!profile && !profiles_.empty()) {
    profile = profiles_.front().profile;
  }
  if (!profile) {
    CreateProfile("Personal", base::BindOnce(
                                  [](base::WeakPtr<UiApp> app,
                                     std::vector<GURL> urls, std::string id) {
                                    if (app && !id.empty()) {
                                      app->OpenLaunchURLs(std::move(urls));
                                    }
                                  },
                                  weak_factory_.GetWeakPtr(), std::move(urls)));
    return;
  }
  content::WebContents* last = nullptr;
  for (const GURL& url : urls) {
    last = window_->OpenTab(profile, url, /*foreground=*/false);
  }
  if (last) {
    window_->Select(last);
  }
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
  for (size_t place = 0; place < profiles_.size(); ++place) {
    const ProfileEntry& entry = profiles_[place];
    rows.push_back(
        {.profile_id = entry.id,
         .title = base::UTF8ToUTF16(entry.name),
         .selected = entry.profile == window.host().active_profile(),
         .color = kProfileColors[place % std::size(kProfileColors)]});
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
    // ⌘T's tab, before its page: titled as its page will be until it loads.
    const std::optional<Window::PendingTab>& pending = window.pending_tab();
    if (pending && pending->profile == entry.profile) {
      rows.push_back({.profile_id = entry.id,
                      .tab_id = pending->id,
                      .title = base::UTF8ToUTF16(pending->url),
                      .selected = pending->foreground});
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
    const std::optional<Window::PendingTab>& pending = window_->pending_tab();
    if (pending && pending->profile == entry.profile) {
      tabs.Append(base::DictValue()
                      .Set("id", pending->id)
                      .Set("url", pending->url)
                      .Set("title", "")
                      .Set("loading", true));
    }
    if (window_->host().active_profile() == entry.profile) {
      active_profile = entry.id;
    }
    profiles.Append(base::DictValue()
                        .Set("id", entry.id)
                        .Set("name", entry.name)
                        .Set("tabs", std::move(tabs)));
  }
  const std::string selected = window_->selected_id();
  views::Widget* widget = window_->host().widget();
  return base::DictValue()
      .Set("profiles", std::move(profiles))
      .Set("activeTabID",
           selected.empty() ? base::Value() : base::Value(selected))
      .Set("activeProfileID",
           active_profile.empty() ? base::Value() : base::Value(active_profile))
      .Set("page", "browser")
      .Set("address", window_->root().address())
      .Set("focus", window_->root().focus())
      .Set("window", base::DictValue()
                         .Set("trafficLightFrames", ControlFrames(widget))
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
