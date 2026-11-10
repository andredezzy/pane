// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/permission_prompt.h"

#include <utility>
#include <variant>

#include "base/containers/fixed_flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/permissions/permission_request.h"
#include "components/permissions/permission_request_manager.h"
#include "components/permissions/permission_uma_constants.h"
#include "components/permissions/request_type.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace pane {

// static
void PermissionPrompt::Install(content::WebContents* web_contents) {
  if (auto* requests = permissions::PermissionRequestManager::FromWebContents(
          web_contents)) {
    requests->set_view_factory_for_testing(
        base::BindRepeating(&PermissionPrompt::Create));
  }
}

// static
std::u16string PermissionPrompt::Describe(std::string_view permission) {
  static constexpr auto kLines =
      base::MakeFixedFlatMap<std::string_view, std::u16string_view>({
          {"geolocation", u"Know your location"},
          {"geolocation_approximate", u"Know your location"},
          {"notifications", u"Show notifications"},
          {"media_stream_camera", u"Use your camera"},
          {"media_stream_mic", u"Use your microphone"},
          {"camera_pan_tilt_zoom", u"Move your camera"},
          {"clipboard", u"See text and images copied to the clipboard"},
          {"midi_sysex", u"Control and reprogram your MIDI devices"},
          {"storage_access", u"Use cookies and site data in this context"},
          {"top_level_storage_access",
           u"Use cookies and site data in this context"},
          {"window_management", u"Manage windows on all your displays"},
          {"local_fonts", u"Use your computer’s fonts"},
          {"idle_detection", u"Know when you’re actively using this device"},
          {"multiple_downloads", u"Download multiple files"},
          {"protected_media_identifier", u"Play protected content"},
          {"ar_session", u"Use your device’s sensors for virtual reality"},
          {"vr_session", u"Use your device’s sensors for virtual reality"},
          {"hand_tracking", u"Track your hands"},
          {"keyboard_lock", u"Use your keyboard"},
          {"pointer_lock", u"Use your mouse pointer"},
      });
  if (const auto found = kLines.find(permission); found != kLines.end()) {
    return base::StrCat({u"• ", found->second});
  }
  std::string words(permission);
  base::ReplaceChars(words, "_", " ", &words);
  return base::StrCat({u"• ", base::UTF8ToUTF16(words)});
}

// static
std::unique_ptr<permissions::PermissionPrompt> PermissionPrompt::Create(
    content::WebContents* web_contents,
    Delegate* delegate) {
  // As Chrome's factory does where no location bar shows, and Pane's never
  // does.
  PageDialogs* dialogs = PageDialogs::Get();
  if (delegate->ShouldDropCurrentRequestIfCannotShowQuietly() || !dialogs) {
    return nullptr;
  }
  base::ListValue traced;
  PageDialogContent content;
  const GURL origin = delegate->GetRequestingOrigin();
  content.title = base::StrCat({HostOf(origin), u" wants to"});
  for (const auto& request : delegate->Requests()) {
    const std::string_view key =
        permissions::PermissionKeyForRequestType(request->request_type());
    content.message.push_back(Describe(key));
    traced.Append(key);
  }
  content.buttons = {u"Allow", u"Allow This Time", u"Don’t Allow"};
  TraceDialog("callback", "PaneWebContentsDelegate.requestPermission",
              web_contents,
              base::DictValue()
                  .Set("permissions", std::move(traced))
                  .Set("origin", origin.spec()));

  auto prompt = base::WrapUnique(new PermissionPrompt());
  prompt->id_ = dialogs->Ask(
      web_contents, PageDialogs::Kind::kPermission, std::move(content),
      base::BindOnce(
          [](base::WeakPtr<Delegate> delegate, PermissionPrompt* prompt,
             std::optional<size_t> button,
             const std::vector<std::u16string>& texts) {
            // Answered: Chrome destroys the prompt as it decides, which has
            // nothing left to take back.
            prompt->id_.reset();
            if (!delegate) {
              return;
            }
            const PromptOptions options = std::monostate();
            if (button == 0) {
              TraceDialog("call", "PanePermissionRequest.allow", nullptr);
              delegate->Accept(options);
            } else if (button == 1) {
              TraceDialog("call", "PanePermissionRequest.allowThisTime",
                          nullptr);
              delegate->AcceptThisTime(options);
            } else if (button == 2) {
              TraceDialog("call", "PanePermissionRequest.deny", nullptr);
              delegate->Deny(options);
            } else {
              // Closed without a choice: the site may ask again.
              TraceDialog("call", "PanePermissionRequest.dismiss", nullptr);
              delegate->Dismiss(options);
            }
          },
          delegate->GetWeakPtr(), prompt.get()),
      base::BindOnce([](PermissionPrompt* prompt) { prompt->id_.reset(); },
                     prompt.get()));
  return prompt;
}

PermissionPrompt::PermissionPrompt() = default;

// Chrome destroys the prompt once the request is decided (the dialog
// answered it, or Chrome resolved it otherwise); unanswered, it leaves the
// window.
PermissionPrompt::~PermissionPrompt() {
  if (PageDialogs* dialogs = PageDialogs::Get(); dialogs && id_) {
    dialogs->Withdraw(*id_);
  }
}

bool PermissionPrompt::UpdateAnchor() {
  return true;
}

permissions::PermissionPrompt::TabSwitchingBehavior
PermissionPrompt::GetTabSwitchingBehavior() {
  // The window shows a tab's dialogs only while the tab is selected.
  return kKeepPromptAlive;
}

permissions::PermissionPromptDisposition
PermissionPrompt::GetPromptDisposition() const {
  return permissions::PermissionPromptDisposition::CUSTOM_MODAL_DIALOG;
}

bool PermissionPrompt::IsAskPrompt() const {
  return true;
}

std::optional<gfx::Rect> PermissionPrompt::GetViewBoundsInScreen() const {
  return std::nullopt;
}

std::vector<permissions::ElementAnchoredBubbleVariant>
PermissionPrompt::GetPromptVariants() const {
  return {};
}

std::optional<permissions::feature_params::PermissionElementPromptPosition>
PermissionPrompt::GetPromptPosition() const {
  return std::nullopt;
}

}  // namespace pane
