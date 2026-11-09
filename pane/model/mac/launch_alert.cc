// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/mac/launch_alert.h"

#include <CoreFoundation/CoreFoundation.h>

#include <utility>

#include "base/apple/scoped_cftyperef.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"

namespace pane::model {

namespace {

using ScopedNotification = base::apple::ScopedCFTypeRef<CFUserNotificationRef>;

// The alert, with Pane's icon where the bundle has it; null, said in the log,
// when the system would not show it.
ScopedNotification Notify(const std::string& title,
                          const std::string& message,
                          CFOptionFlags flags,
                          const std::optional<std::string>& default_button,
                          const std::optional<std::string>& alternate_button) {
  const base::apple::ScopedCFTypeRef<CFMutableDictionaryRef> contents(
      CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  const auto set = [&](CFStringRef key, const std::string& value) {
    CFDictionarySetValue(contents.get(), key,
                         base::SysUTF8ToCFStringRef(value).get());
  };
  set(kCFUserNotificationAlertHeaderKey, title);
  set(kCFUserNotificationAlertMessageKey, message);
  if (default_button) {
    set(kCFUserNotificationDefaultButtonTitleKey, *default_button);
  }
  if (alternate_button) {
    set(kCFUserNotificationAlternateButtonTitleKey, *alternate_button);
  }
  if (const base::apple::ScopedCFTypeRef<CFURLRef> icon(CFBundleCopyResourceURL(
          CFBundleGetMainBundle(), CFSTR("AppIcon"), CFSTR("icns"), nullptr));
      icon) {
    CFDictionarySetValue(contents.get(), kCFUserNotificationIconURLKey,
                         icon.get());
  }

  SInt32 error = 0;
  ScopedNotification notification(CFUserNotificationCreate(
      kCFAllocatorDefault, 0, flags | kCFUserNotificationNoteAlertLevel, &error,
      contents.get()));
  if (!notification) {
    LOG(ERROR) << "Could not show the launch alert \"" << title
               << "\": " << error;
  }
  return notification;
}

class SystemNote : public LaunchAlert::Note {
 public:
  explicit SystemNote(ScopedNotification notification)
      : notification_(std::move(notification)) {}
  SystemNote(const SystemNote&) = delete;
  SystemNote& operator=(const SystemNote&) = delete;
  ~SystemNote() override { CFUserNotificationCancel(notification_.get()); }

 private:
  const ScopedNotification notification_;
};

class SystemLaunchAlert : public LaunchAlert {
 public:
  explicit SystemLaunchAlert(bool silent) : silent_(silent) {}
  SystemLaunchAlert(const SystemLaunchAlert&) = delete;
  SystemLaunchAlert& operator=(const SystemLaunchAlert&) = delete;
  ~SystemLaunchAlert() override = default;

  Response Show(const std::string& title,
                const std::string& message,
                const std::string& default_button,
                const std::optional<std::string>& alternate_button,
                base::RepeatingCallback<bool()> dismiss_when) override {
    ScopedNotification notification;
    if (!silent_) {
      notification =
          Notify(title, message, 0, default_button, alternate_button);
    }
    // As a user who presses nothing. The Swift returned at once when the
    // system would not show it, and the migration then asked again at once,
    // in a loop, for as long as the Electron app ran.
    if (!notification) {
      while (dismiss_when && !dismiss_when.Run()) {
        base::PlatformThread::Sleep(base::Seconds(1));
      }
      return Response::kDismissed;
    }

    CFOptionFlags flags = 0;
    while (CFUserNotificationReceiveResponse(notification.get(), 1, &flags) !=
           0) {
      if (dismiss_when && dismiss_when.Run()) {
        CFUserNotificationCancel(notification.get());
        return Response::kDismissed;
      }
    }
    return (flags & 0x3) == kCFUserNotificationAlternateResponse
               ? Response::kAlternateButton
               : Response::kDefaultButton;
  }

  std::unique_ptr<Note> ShowNote(const std::string& title,
                                 const std::string& message) override {
    if (silent_) {
      return nullptr;
    }
    ScopedNotification notification =
        Notify(title, message, kCFUserNotificationNoDefaultButtonFlag,
               std::nullopt, std::nullopt);
    if (!notification) {
      return nullptr;
    }
    return std::make_unique<SystemNote>(std::move(notification));
  }

 private:
  const bool silent_;
};

}  // namespace

// static
std::unique_ptr<LaunchAlert> LaunchAlert::Create(bool silent) {
  return std::make_unique<SystemLaunchAlert>(silent);
}

}  // namespace pane::model
