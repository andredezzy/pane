// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Modeled on Ardia's TabBridge (https://github.com/mantrakp04/ardia,
// engine/ardiacore/src/ardia/core/ac_browser.mm, Copyright 2026 Mantra Patel,
// Apache License 2.0; third_party/ardia).

#include "pane/browser/ui/hosted_tab.h"

#include <utility>

#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/web_store.h"

namespace pane {

HostedTab::HostedTab(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<HostedTab>(*contents) {}

HostedTab::~HostedTab() = default;

HostedTab* HostedTab::GetOrCreate(content::WebContents* contents) {
  CreateForWebContents(contents);
  return FromWebContents(contents);
}

void HostedTab::set_open(WindowOpenDisposition disposition,
                         content::WebContents* source) {
  open_disposition_ = disposition;
  open_source_ = source ? source->GetWeakPtr() : nullptr;
}

std::optional<WindowOpenDisposition> HostedTab::TakeOpenDisposition() {
  return std::exchange(open_disposition_, std::nullopt);
}

int HostedTab::tab_id() const {
  return sessions::SessionTabHelper::IdForTab(&GetWebContents()).id();
}

void HostedTab::TraceCallback(std::string_view name, base::DictValue args) {
  args.Set("tab", tab_id());
  Trace("callback", name, std::move(args));
}

void HostedTab::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  RenameWebStoreButtons(navigation_handle);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(HostedTab);

}  // namespace pane
