// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/sidebar_content.h"

namespace pane {

SidebarTab::SidebarTab() = default;
SidebarTab::SidebarTab(const SidebarTab&) = default;
SidebarTab& SidebarTab::operator=(const SidebarTab&) = default;
SidebarTab::~SidebarTab() = default;

bool SidebarTab::operator==(const SidebarTab& other) const {
  const bool same_favicon = favicon.isNull()
                                ? other.favicon.isNull()
                                : favicon.BackedBySameObjectAs(other.favicon);
  return id == other.id && title == other.title && same_favicon &&
         selected == other.selected && loaded == other.loaded;
}

SidebarProfile::SidebarProfile() = default;
SidebarProfile::SidebarProfile(const SidebarProfile&) = default;
SidebarProfile& SidebarProfile::operator=(const SidebarProfile&) = default;
SidebarProfile::~SidebarProfile() = default;

bool SidebarProfile::operator==(const SidebarProfile& other) const {
  return id == other.id && name == other.name && color == other.color &&
         shown == other.shown && expanded == other.expanded &&
         proxy == other.proxy && tab_count == other.tab_count &&
         keep_loaded == other.keep_loaded && tabs == other.tabs;
}

SidebarContent::SidebarContent() = default;
SidebarContent::SidebarContent(const SidebarContent&) = default;
SidebarContent& SidebarContent::operator=(const SidebarContent&) = default;
SidebarContent::~SidebarContent() = default;

}  // namespace pane
