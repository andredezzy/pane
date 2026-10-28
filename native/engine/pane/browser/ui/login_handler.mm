// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/app_callback.h"
#include "pane/browser/ui/login_handler.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/sys_string_conversions.h"
#include "net/base/auth.h"
#include "pane/browser/ui/tab_bridge.h"

#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneWebContents.h"

namespace pane {

namespace {

PaneWebContents* TabFor(content::WebContents* web_contents) {
  TabBridge* bridge =
      web_contents ? TabBridge::FromWebContents(web_contents) : nullptr;
  return bridge ? bridge->tab() : nil;
}

}  // namespace

// static
std::unique_ptr<::LoginHandler> LoginHandler::Create(
    const net::AuthChallengeInfo& auth_info,
    content::WebContents* web_contents,
    content::LoginDelegate::LoginAuthRequiredCallback& callback) {
  if (![TabFor(web_contents).delegate
          respondsToSelector:@selector(webContents:authenticate:)]) {
    return nullptr;
  }
  return base::WrapUnique(
      new LoginHandler(auth_info, web_contents, std::move(callback)));
}

LoginHandler::LoginHandler(
    const net::AuthChallengeInfo& auth_info,
    content::WebContents* web_contents,
    content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback)
    : ::LoginHandler(auth_info,
                     web_contents,
                     std::move(auth_required_callback)) {}

LoginHandler::~LoginHandler() {
  [challenge_ withdraw];
}

bool LoginHandler::BuildViewImpl(const std::u16string& authority,
                                 const std::u16string& explanation,
                                 LoginModelData* login_model_data) {
  PaneWebContents* tab = TabFor(web_contents());
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if (![delegate respondsToSelector:@selector(webContents:authenticate:)]) {
    return false;
  }
  const net::AuthChallengeInfo& info = auth_info();
  challenge_ = [[PaneAuthenticationChallenge alloc]
      initWithHost:base::SysUTF8ToNSString(info.challenger.Serialize())
             realm:base::SysUTF8ToNSString(info.realm)
            scheme:base::SysUTF8ToNSString(info.scheme)
             proxy:info.is_proxy
               tab:tab
            answer:base::BindOnce(
                       [](base::WeakPtr<LoginHandler> handler,
                          std::optional<std::pair<std::u16string,
                                                  std::u16string>>
                              credentials) {
                         if (!handler) {
                           return;
                         }
                         if (credentials) {
                           handler->SetAuth(credentials->first,
                                            credentials->second);
                         } else {
                           handler->CancelAuth(/*notify_others=*/true);
                         }
                       },
                       weak_factory_.GetWeakPtr())];
  PANE_TRACE_TAB(TabBridge::FromWebContents(web_contents()),
                 "PaneWebContentsDelegate.authenticate",
                 base::DictValue()
                     .Set("host", info.challenger.Serialize())
                     .Set("realm", info.realm)
                     .Set("proxy", info.is_proxy));
  ScopedAppCallback callback;
  [delegate webContents:tab authenticate:challenge_];
  return true;
}

// Chrome closes the prompt once credentials are in, or when another
// handler for the same server answered or the request went away.
void LoginHandler::CloseDialog() {
  [challenge_ withdraw];
}

}  // namespace pane
