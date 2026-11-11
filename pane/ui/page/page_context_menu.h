// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_PAGE_CONTEXT_MENU_H_
#define PANE_UI_PAGE_PAGE_CONTEXT_MENU_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/global_routing_id.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/data_transfer_policy/data_transfer_endpoint.h"

class RenderViewContextMenuBase;

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace ui {
class MenuModel;
}

namespace pane {

// The page's right-click menu (U2, Paper 10.4 and 10.4.1): Chrome's own
// RenderViewContextMenu, the system's menu on the Mac, with everything kept
// but what Pane can't support, which a filter takes off its model before it
// shows, with no Chromium patch: a link opened in a new window, in Split
// View, in an incognito window, or as another of Chrome's profiles (Pane's
// profiles open only through Pane, with their proxy, fingerprint, and keys,
// and Chrome knows them by no name of theirs), and what needs Chrome's own
// browser window (Lens, Reading Mode, QR codes), Chrome's language settings,
// and Google's spelling service. Extensions' items are Chrome's, each in its
// slot with its icon.
//
// The menu runs its own loop and takes the keys while it shows, so it shows
// only where Pane may take the front (MayTakeFront): a scenario's copy in the
// background builds it, filtered, and the channel reads it and chooses from
// its model as a click does, as the sidebar's row menus are read (H2).
class PageContextMenus {
 public:
  PageContextMenus();
  PageContextMenus(const PageContextMenus&) = delete;
  PageContextMenus& operator=(const PageContextMenus&) = delete;
  ~PageContextMenus();

  // A right-click in `frame` with `params` (HostedWebContentsDelegate's
  // HandleContextMenu): the menu as Chrome builds it, the clipboard's kinds
  // read first for Paste's items, then shown.
  void Show(content::RenderFrameHost& frame,
            const content::ContextMenuParams& params);
  // The menu shown last, until it closes or an item is chosen.
  const ui::MenuModel* model() const;
  // How many menus have shown.
  int shown() const { return shown_; }
  // Chooses the enabled item titled `title`, in a submenu too, as a click
  // in the menu does; the menu then closes. Whether there was one.
  bool Choose(std::u16string_view title);
  // Each item, but separators and what doesn't show: its title, command,
  // whether it is enabled and checked, and its submenu's items.
  static base::ListValue Describe(const ui::MenuModel& model);

 private:
  // The menu Chrome would build, for the platform's menus.
  static std::unique_ptr<RenderViewContextMenuBase> Make(
      content::RenderFrameHost& frame,
      const content::ContextMenuParams& params,
      bool paste,
      bool paste_and_match_style);
  // Runs `menu` as the platform's menu, `tracking` once it shows; returns
  // once it closes.
  static void Run(RenderViewContextMenuBase& menu, base::OnceClosure tracking);

  void TypesRead(content::GlobalRenderFrameHostId frame,
                 content::ContextMenuParams params,
                 std::optional<ui::DataTransferEndpoint> data_dst,
                 base::TimeTicks asked,
                 std::optional<uint64_t> interval,
                 std::vector<std::u16string> types);
  void FormatsRead(content::GlobalRenderFrameHostId frame,
                   content::ContextMenuParams params,
                   base::TimeTicks asked,
                   std::optional<uint64_t> interval,
                   bool paste,
                   base::flat_set<ui::ClipboardFormatType> formats);
  // Takes off what Pane can't support.
  static void Filter(RenderViewContextMenuBase& menu);
  // The menu shown goes.
  void Release();

  std::unique_ptr<RenderViewContextMenuBase> menu_;
  // The page the menu is for: gone, none of its items runs any more.
  base::WeakPtr<content::WebContents> page_;
  // The menu runs as the platform's, in its own loop.
  bool running_ = false;
  int shown_ = 0;
  base::WeakPtrFactory<PageContextMenus> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_PAGE_PAGE_CONTEXT_MENU_H_
