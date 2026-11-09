// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_SIDEBAR_CONTENT_H_
#define PANE_UI_SIDEBAR_SIDEBAR_CONTENT_H_

#include <optional>
#include <string>
#include <vector>

#include "pane/model/browser_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image_skia.h"

namespace pane {

// What the sidebar shows, as values the app gives it (the Swift's SidebarView
// read them off the model): a row redraws only when its own change (ADR
// 0073). Sleeping tabs have no page, so the rows come from the model.

// A tab's row.
struct SidebarTab {
  SidebarTab();
  SidebarTab(const SidebarTab&);
  SidebarTab& operator=(const SidebarTab&);
  ~SidebarTab();

  // Its favicon compares by the image it holds.
  bool operator==(const SidebarTab& other) const;

  std::string id;
  // The page's title: none yet reads "Loading...".
  std::u16string title;
  // Its favicon, or null for the globe.
  gfx::ImageSkia favicon;
  // The tab shown: its fill and text, at once.
  bool selected = false;
  // It has a page. One without (asleep, unloaded, not yet opened) dims.
  bool loaded = false;
};

// A profile's header, and its tabs while it is expanded.
struct SidebarProfile {
  SidebarProfile();
  SidebarProfile(const SidebarProfile&);
  SidebarProfile& operator=(const SidebarProfile&);
  ~SidebarProfile();

  bool operator==(const SidebarProfile& other) const;

  std::string id;
  std::u16string name;
  // Its swatch, the same in both appearances.
  SkColor color = SK_ColorTRANSPARENT;
  // Its page shows: on the browser page only, its bar a 16-point square
  // (ADR 0064).
  bool shown = false;
  bool expanded = false;
  // Its proxy's last test, for the dot.
  std::optional<model::BrowserModel::ProxyStatus> proxy;
  // How many tabs it has, counted while it is collapsed.
  int tab_count = 0;
  // Its menu's Keep loaded.
  bool keep_loaded = false;
  // Its tabs, while it is expanded.
  std::vector<SidebarTab> tabs;
};

struct SidebarContent {
  SidebarContent();
  SidebarContent(const SidebarContent&);
  SidebarContent& operator=(const SidebarContent&);
  ~SidebarContent();

  std::vector<SidebarProfile> profiles;
  // Settings shows: its row is selected.
  bool settings_shown = false;
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_SIDEBAR_CONTENT_H_
