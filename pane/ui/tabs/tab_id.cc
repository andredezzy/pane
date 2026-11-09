// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/tabs/tab_id.h"

#include <map>
#include <utility>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/uuid.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace pane {

namespace {

std::map<std::string, raw_ptr<content::WebContents>>& Live() {
  static base::NoDestructor<
      std::map<std::string, raw_ptr<content::WebContents>>>
      live;
  return *live;
}

class TabId : public content::WebContentsUserData<TabId> {
 public:
  ~TabId() override { Live().erase(id_); }

  const std::string& id() const { return id_; }

 private:
  friend WebContentsUserData;

  explicit TabId(content::WebContents* contents)
      : TabId(contents, NewTabId()) {}
  TabId(content::WebContents* contents, std::string id)
      : WebContentsUserData(*contents), id_(std::move(id)) {
    Live()[id_] = contents;
  }

  const std::string id_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabId);

}  // namespace

const std::string& TabIdOf(content::WebContents* contents) {
  TabId::CreateForWebContents(contents);
  return TabId::FromWebContents(contents)->id();
}

std::string NewTabId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

void SetTabId(content::WebContents* contents, std::string id) {
  CHECK(!TabId::FromWebContents(contents));
  TabId::CreateForWebContents(contents, std::move(id));
}

content::WebContents* TabWithId(const std::string& id) {
  auto it = Live().find(id);
  return it == Live().end() ? nullptr : it->second.get();
}

}  // namespace pane
