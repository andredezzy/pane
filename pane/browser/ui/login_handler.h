// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_UI_LOGIN_HANDLER_H_
#define PANE_BROWSER_UI_LOGIN_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/login/login_handler.h"

@class PaneAuthenticationChallenge;

namespace pane {

// HTTP and proxy authentication for a Pane tab, asked through the app
// (webContents:authenticate:) instead of Chrome's dialog. Chrome's
// LoginTabHelper or HttpAuthCoordinator owns it.
class LoginHandler : public ::LoginHandler {
 public:
  // nullptr when the tab is not Pane's or the app takes no challenges;
  // `callback` is taken only when a handler is returned.
  static std::unique_ptr<::LoginHandler> Create(
      const net::AuthChallengeInfo& auth_info,
      content::WebContents* web_contents,
      content::LoginDelegate::LoginAuthRequiredCallback& callback);

  LoginHandler(const LoginHandler&) = delete;
  LoginHandler& operator=(const LoginHandler&) = delete;
  ~LoginHandler() override;

 protected:
  // ::LoginHandler:
  bool BuildViewImpl(const std::u16string& authority,
                     const std::u16string& explanation,
                     LoginModelData* login_model_data) override;
  void CloseDialog() override;

 private:
  LoginHandler(
      const net::AuthChallengeInfo& auth_info,
      content::WebContents* web_contents,
      content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback);

  PaneAuthenticationChallenge* __strong challenge_;
  base::WeakPtrFactory<LoginHandler> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_UI_LOGIN_HANDLER_H_
