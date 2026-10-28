// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/app_callback.h"
#include "pane/browser/ui/permission_prompt.h"

#include <utility>
#include <variant>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/sys_string_conversions.h"
#include "components/permissions/permission_request.h"
#include "components/permissions/permission_uma_constants.h"
#include "components/permissions/request_type.h"
#include "pane/browser/ui/tab_bridge.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneWebContents.h"

namespace pane {

// static
std::unique_ptr<permissions::PermissionPrompt> PermissionPrompt::Create(
    content::WebContents* web_contents,
    Delegate* delegate) {
  TabBridge* bridge = TabBridge::FromWebContents(web_contents);
  PaneWebContents* tab = bridge ? bridge->tab() : nil;
  id<PaneWebContentsDelegate> app = tab.delegate;
  if (![app respondsToSelector:@selector(webContents:requestPermission:)]) {
    return nullptr;
  }

  NSMutableArray<NSString*>* names = [NSMutableArray array];
  for (const auto& request : delegate->Requests()) {
    [names addObject:@(permissions::PermissionKeyForRequestType(
                         request->request_type()))];
  }

  auto prompt = base::WrapUnique(new PermissionPrompt());
  prompt->request_ = [[PanePermissionRequest alloc]
      initWithPermissions:names
                originURL:delegate->GetRequestingOrigin()
                      tab:tab
                   answer:base::BindOnce(
                              [](base::WeakPtr<Delegate> delegate, int choice) {
                                if (!delegate) {
                                  return;
                                }
                                const PromptOptions options =
                                    std::monostate();
                                switch (choice) {
                                  case 0:
                                    delegate->Accept(options);
                                    break;
                                  case 1:
                                    delegate->AcceptThisTime(options);
                                    break;
                                  case 2:
                                    delegate->Deny(options);
                                    break;
                                  default:
                                    delegate->Dismiss(options);
                                    break;
                                }
                              },
                              delegate->GetWeakPtr())];
  PANE_TRACE_TAB(bridge, "PaneWebContentsDelegate.requestPermission",
                 base::DictValue()
                     .Set("permissions",
                          [names] {
                            base::ListValue traced;
                            for (NSString* name in names) {
                              traced.Append(base::SysNSStringToUTF8(name));
                            }
                            return traced;
                          }())
                     .Set("origin", delegate->GetRequestingOrigin().spec()));
  ScopedAppCallback callback;
  [app webContents:tab requestPermission:prompt->request_];
  return prompt;
}

PermissionPrompt::PermissionPrompt() = default;

// Chrome destroys the prompt once the request is decided (the app answered,
// or Chrome resolved it otherwise); unanswered, the app takes its UI down.
PermissionPrompt::~PermissionPrompt() {
  [request_ withdraw];
}

bool PermissionPrompt::UpdateAnchor() {
  return true;
}

permissions::PermissionPrompt::TabSwitchingBehavior
PermissionPrompt::GetTabSwitchingBehavior() {
  // The app shows a tab's requests only while the tab is shown.
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
