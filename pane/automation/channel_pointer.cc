// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/channel_pointer.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace pane {

namespace {

// Off every screen: where no window is.
constexpr gfx::Point kNowhere(-1'000'000, -1'000'000);

// The system's screen, but for where the pointer is. Installed in its place
// as Chromium's own tests install theirs (SetScreenInstance), with none
// between, so no state of the system's is copied: it keeps the displays and
// their observers, and this asks it for them.
class PointerScreen : public display::Screen {
 public:
  PointerScreen() : system_(display::Screen::SetScreenInstance(nullptr)) {
    CHECK(system_);
    display::Screen::SetScreenInstance(this);
  }

  PointerScreen(const PointerScreen&) = delete;
  PointerScreen& operator=(const PointerScreen&) = delete;

  ~PointerScreen() override {
    if (display::Screen::Get() == this) {
      display::Screen::SetScreenInstance(nullptr);
      display::Screen::SetScreenInstance(system_);
    }
  }

  void MoveTo(const gfx::Point& point) { pointer_ = point; }

  // display::Screen:
  gfx::Point GetCursorScreenPoint() override { return pointer_; }
  bool IsWindowUnderCursor(gfx::NativeWindow window) override {
    return system_->GetLocalProcessWindowAtPoint(pointer_, {}) == window;
  }
  gfx::NativeWindow GetWindowAtScreenPoint(const gfx::Point& point) override {
    return system_->GetWindowAtScreenPoint(point);
  }
  gfx::NativeWindow GetLocalProcessWindowAtPoint(
      const gfx::Point& point,
      const std::set<gfx::NativeWindow>& ignore) override {
    return system_->GetLocalProcessWindowAtPoint(point, ignore);
  }
  int GetNumDisplays() const override { return system_->GetNumDisplays(); }
  const std::vector<display::Display>& GetAllDisplays() const override {
    return system_->GetAllDisplays();
  }
  display::Display GetDisplayNearestWindow(
      gfx::NativeWindow window) const override {
    return system_->GetDisplayNearestWindow(window);
  }
  display::Display GetDisplayNearestView(gfx::NativeView view) const override {
    return system_->GetDisplayNearestView(view);
  }
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point) const override {
    return system_->GetDisplayNearestPoint(point);
  }
  display::Display GetDisplayMatching(
      const gfx::Rect& match_rect) const override {
    return system_->GetDisplayMatching(match_rect);
  }
  display::Display GetPrimaryDisplay() const override {
    return system_->GetPrimaryDisplay();
  }
  void SetDisplayForNewWindows(int64_t display_id) override {
    display::Screen::SetDisplayForNewWindows(display_id);
    system_->SetDisplayForNewWindows(display_id);
  }
  bool IsScreenSaverActive() const override {
    return system_->IsScreenSaverActive();
  }
  base::TimeDelta CalculateIdleTime() const override {
    return system_->CalculateIdleTime();
  }
  void AddObserver(display::DisplayObserver* observer) override {
    system_->AddObserver(observer);
  }
  void RemoveObserver(display::DisplayObserver* observer) override {
    system_->RemoveObserver(observer);
  }
  gfx::Rect ScreenToDIPRectInWindow(
      gfx::NativeWindow window,
      const gfx::Rect& screen_rect) const override {
    return system_->ScreenToDIPRectInWindow(window, screen_rect);
  }
  gfx::Rect DIPToScreenRectInWindow(gfx::NativeWindow window,
                                    const gfx::Rect& dip_rect) const override {
    return system_->DIPToScreenRectInWindow(window, dip_rect);
  }
  std::string GetCurrentWorkspace() override {
    return system_->GetCurrentWorkspace();
  }
  base::ListValue GetGpuExtraInfo(
      const gfx::GpuExtraInfo& gpu_extra_info) override {
    return system_->GetGpuExtraInfo(gpu_extra_info);
  }
  std::optional<float> GetPreferredScaleFactorForWindow(
      gfx::NativeWindow window) const override {
    return system_->GetPreferredScaleFactorForWindow(window);
  }
  std::optional<float> GetPreferredScaleFactorForView(
      gfx::NativeView view) const override {
    return system_->GetPreferredScaleFactorForView(view);
  }
  bool IsHeadless() const override { return system_->IsHeadless(); }

 private:
  const raw_ptr<display::Screen> system_;
  gfx::Point pointer_ = kNowhere;
};

// The channel's while it runs: one at a time.
PointerScreen* g_screen = nullptr;

}  // namespace

ChannelPointer::ChannelPointer() {
  CHECK(!g_screen);
  auto screen = std::make_unique<PointerScreen>();
  g_screen = screen.get();
  screen_ = std::move(screen);
  system_pointer_out_ = KeepSystemPointerOut();
}

ChannelPointer::~ChannelPointer() {
  g_screen = nullptr;
}

// static
void ChannelPointer::MovedTo(views::Widget* widget, int x, int y) {
  if (!g_screen || !widget || !widget->GetRootView()) {
    return;
  }
  gfx::Point point(x, y);
  views::View::ConvertPointToScreen(widget->GetRootView(), &point);
  g_screen->MoveTo(point);
}

}  // namespace pane
