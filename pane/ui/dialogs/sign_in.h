// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_SIGN_IN_H_
#define PANE_UI_DIALOGS_SIGN_IN_H_

#include <memory>

#include "chrome/browser/ui/login/http_auth_coordinator.h"

namespace pane {

// HTTP and proxy sign-in in the window's own dialog (ADR 0021): "Sign in",
// who asks, Username and Password, and Sign In or Cancel. Through the seam
// Chromium keeps for it (ADR 0004), Pane's browser client's coordinator
// (CreateHttpAuthCoordinator): a main frame's challenge, once Chrome has put
// a blank page under it (LoginTabHelper), and a subframe's or a resource's,
// each get Pane's LoginHandler, as login_handler_browsertest.cc's fakes do.
// The extensions' webRequest step before it stays Chrome's.
class SignInCoordinator : public HttpAuthCoordinator {
 public:
  SignInCoordinator();
  SignInCoordinator(const SignInCoordinator&) = delete;
  SignInCoordinator& operator=(const SignInCoordinator&) = delete;
  ~SignInCoordinator() override;

  // HttpAuthCoordinator:
  void CreateLoginTabHelper(content::WebContents* web_contents) override;
  std::unique_ptr<content::LoginDelegate> CreateLoginDelegateFromLoginHandler(
      content::WebContents* web_contents,
      const net::AuthChallengeInfo& auth_info,
      const content::GlobalRequestID& request_id,
      const GURL& url,
      scoped_refptr<net::HttpResponseHeaders> response_headers,
      content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback)
      override;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_SIGN_IN_H_
