// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/sign_in.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/login/login_handler.h"
#include "chrome/browser/ui/login/login_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "net/base/auth.h"
#include "pane/browser/profile_network.h"
#include "pane/ui/dialogs/page_dialogs.h"

namespace pane {

namespace {

// The challenge, asked in the window's dialog; taken back when Chrome closes
// it (another handler for the same server answered, or the request went) or
// drops it.
class SignIn : public LoginHandler {
 public:
  SignIn(const net::AuthChallengeInfo& auth_info,
         content::WebContents* web_contents,
         content::LoginDelegate::LoginAuthRequiredCallback callback)
      : LoginHandler(auth_info, web_contents, std::move(callback)) {}

  SignIn(const SignIn&) = delete;
  SignIn& operator=(const SignIn&) = delete;
  ~SignIn() override { Withdraw(); }

 protected:
  // LoginHandler:
  bool BuildViewImpl(const std::u16string& authority,
                     const std::u16string& explanation,
                     LoginModelData* login_model_data) override {
    PageDialogs* dialogs = PageDialogs::Get();
    if (!dialogs || !web_contents()) {
      return false;
    }
    const net::AuthChallengeInfo& info = auth_info();
    const std::u16string host = base::UTF8ToUTF16(info.challenger.Serialize());
    PageDialogContent content;
    content.title = u"Sign in";
    content.message = {
        info.is_proxy
            ? base::StrCat(
                  {u"The proxy ", host, u" requires a username and password."})
            : base::StrCat({host, u" requires a username and password."})};
    content.fields = {{.placeholder = u"Username"},
                      {.placeholder = u"Password", .secure = true}};
    content.buttons = {u"Sign In", u"Cancel"};
    TraceDialog("callback", "PaneWebContentsDelegate.authenticate",
                web_contents(),
                base::DictValue()
                    .Set("host", info.challenger.Serialize())
                    .Set("realm", info.realm)
                    .Set("proxy", info.is_proxy));
    // The profile's own proxy signs in with its stored credentials, once,
    // without a word (ADR 0037); asked again at once, they were wrong, and
    // the user is asked instead.
    if (std::optional<net::AuthCredentials> credentials = ProxyCredentials()) {
      // The password is never traced.
      TraceDialog("call", "PaneAuthenticationChallenge.provide", nullptr,
                  base::DictValue().Set(
                      "username", base::UTF16ToUTF8(credentials->username())));
      // On a turn of its own: an answer ends this handler, whose building is
      // still under way.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](base::WeakPtr<SignIn> handler,
                            net::AuthCredentials credentials) {
                           if (handler) {
                             handler->SetAuth(credentials.username(),
                                              credentials.password());
                           }
                         },
                         weak_factory_.GetWeakPtr(), *credentials));
      return true;
    }
    id_ = dialogs->Ask(
        web_contents(), PageDialogs::Kind::kAuthentication, std::move(content),
        base::BindOnce(
            [](base::WeakPtr<SignIn> handler, std::optional<size_t> button,
               const std::vector<std::u16string>& texts) {
              if (!handler) {
                return;
              }
              handler->id_.reset();
              if (button == 0 && texts.size() == 2) {
                // The password is never traced.
                TraceDialog("call", "PaneAuthenticationChallenge.provide",
                            nullptr,
                            base::DictValue().Set("username",
                                                  base::UTF16ToUTF8(texts[0])));
                handler->SetAuth(texts[0], texts[1]);
              } else {
                TraceDialog("call", "PaneAuthenticationChallenge.cancel",
                            nullptr);
                handler->CancelAuth(/*notify_others=*/true);
              }
            },
            weak_factory_.GetWeakPtr()),
        base::BindOnce(
            [](base::WeakPtr<SignIn> handler) {
              if (handler) {
                handler->id_.reset();
              }
            },
            weak_factory_.GetWeakPtr()));
    return true;
  }

  // Chrome closes the prompt once credentials are in, or when another
  // handler for the same server answered or the request went away.
  void CloseDialog() override { Withdraw(); }

 private:
  // The page's profile's proxy credentials, for a challenge of its own
  // proxy's.
  std::optional<net::AuthCredentials> ProxyCredentials() {
    ProfileNetwork* network = ProfileNetwork::Get();
    if (!auth_info().is_proxy || !network || !web_contents()) {
      return std::nullopt;
    }
    const Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext())
            ->GetOriginalProfile();
    return network->CredentialsFor(profile->GetBaseName().AsUTF8Unsafe(),
                                   auth_info());
  }

  void Withdraw() {
    if (PageDialogs* dialogs = PageDialogs::Get(); dialogs && id_) {
      dialogs->Withdraw(*std::exchange(id_, std::nullopt));
    }
  }

  std::optional<PageDialogs::Id> id_;
  base::WeakPtrFactory<SignIn> weak_factory_{this};
};

// A main frame's: Chrome blanks the page that asked, then asks over it.
class SignInTabHelper : public LoginTabHelper {
 public:
  explicit SignInTabHelper(content::WebContents* web_contents)
      : LoginTabHelper(web_contents) {}

 protected:
  // LoginTabHelper:
  std::unique_ptr<LoginHandler> CreateLoginHandler(
      const net::AuthChallengeInfo& auth_info,
      content::WebContents* web_contents,
      content::LoginDelegate::LoginAuthRequiredCallback callback) override {
    return std::make_unique<SignIn>(auth_info, web_contents,
                                    std::move(callback));
  }
};

}  // namespace

SignInCoordinator::SignInCoordinator() = default;
SignInCoordinator::~SignInCoordinator() = default;

void SignInCoordinator::CreateLoginTabHelper(
    content::WebContents* web_contents) {
  if (!LoginTabHelper::FromWebContents(web_contents)) {
    web_contents->SetUserData(LoginTabHelper::UserDataKey(),
                              std::make_unique<SignInTabHelper>(web_contents));
  }
}

std::unique_ptr<content::LoginDelegate>
SignInCoordinator::CreateLoginDelegateFromLoginHandler(
    content::WebContents* web_contents,
    const net::AuthChallengeInfo& auth_info,
    const content::GlobalRequestID& request_id,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback) {
  auto handler = std::make_unique<SignIn>(auth_info, web_contents,
                                          std::move(auth_required_callback));
  handler->ShowLoginPrompt(url);
  return handler;
}

}  // namespace pane
