// Copyright 2026 Pane. Licensed under GPL-3.0-only.
//
// Adapted from Ardia (https://github.com/mantrakp04/ardia),
// engine/ardiacore/src/ardia/core/ac_modal_dialog_host.h, Copyright 2026 Mantra Patel,
// licensed under the Apache License 2.0 (native/third_party/ardia). Changed
// for Pane: names and namespace, and nothing else.

#ifndef PANE_BROWSER_UI_MODAL_DIALOG_HOST_H_
#define PANE_BROWSER_UI_MODAL_DIALOG_HOST_H_

#include "base/observer_list.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

@class NSView;
@class NSWindow;

namespace content {
class WebContents;
}

namespace pane {

class WindowHost;

gfx::Rect PageRectInHostView(NSView* host_view, content::WebContents* contents);
gfx::Point DialogPositionInHostView(NSView* host_view,
                                    const gfx::Rect& page,
                                    const gfx::Size& size);

bool WindowIsForeground(NSWindow* window);
void DialogHostObserverAdded();
void DialogHostObserverRemoved();

class TabDialogHost : public web_modal::WebContentsModalDialogHost,
                        public content::WebContentsUserData<TabDialogHost> {
 public:
  TabDialogHost(const TabDialogHost&) = delete;
  TabDialogHost& operator=(const TabDialogHost&) = delete;
  ~TabDialogHost() override;

  gfx::NativeView GetHostView() const override;
  gfx::Point GetDialogPosition(const gfx::Size& size) override;
  bool ShouldActivateDialog() const override;
  gfx::Size GetMaximumDialogSize() override;
  void AddObserver(web_modal::ModalDialogHostObserver* observer) override;
  void RemoveObserver(web_modal::ModalDialogHostObserver* observer) override;
  void NotifyPositionRequiresUpdate() override;

 private:
  friend class content::WebContentsUserData<TabDialogHost>;
  explicit TabDialogHost(content::WebContents* contents);

  WindowHost* window_host() const;

  base::ObserverList<web_modal::ModalDialogHostObserver> observers_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}

#endif
