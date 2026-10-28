// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/web_store.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "pane/browser/engine_trace.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace pane {

namespace {

constexpr char kWebStoreHost[] = "chromewebstore.google.com";

// Runs once per document, in an isolated world: it shares the store's DOM,
// none of its scripts. The store marks the item's install and remove buttons
// by name (jsname wQO0od and ajZLRd, the two buttons its install controller
// routes clicks to); the server renders the install button before its
// script hydrates it, so the controller's routing (its jsaction) names it
// too. Only those buttons' text and labels change. The store redraws a
// label when the item installs or uninstalls, and draws new buttons as it
// navigates between items without loading a document: a MutationObserver,
// whose callbacks run before the next frame, renames them as they come.
// The script answers whether the page had painted, and how many buttons it
// found, as it started: the evidence that no frame showed Chrome.
constexpr char16_t kRenameScript[] =
    uR"js(
(() => {
  if (globalThis.paneWebStoreButtons) {
    return null;
  }
  globalThis.paneWebStoreButtons = true;

  const BUTTONS = [
    'button[jsname="wQO0od"]',
    'button[jsname="ajZLRd"]',
    '[jsaction*="(wQO0od)"] button',
    '[jsaction*="(ajZLRd)"] button',
  ].join(",");
  // "Add to Chrome", "Ajouter à Google Chrome", "Chrome に追加".
  const NAME = /(?:Google[\s ]+)?Chrome/g;

  const rename = (button) => {
    const walker = document.createTreeWalker(button, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      if (node.data.includes("Chrome")) {
        node.data = node.data.replace(NAME, "Pane");
      }
    }
    for (const attribute of ["aria-label", "title"]) {
      const value = button.getAttribute(attribute);
      if (value && value.includes("Chrome")) {
        button.setAttribute(attribute, value.replace(NAME, "Pane"));
      }
    }
  };

  const visit = (node) => {
    const element =
        node.nodeType === Node.ELEMENT_NODE ? node : node.parentElement;
    if (!element) {
      return;
    }
    const button = element.closest(BUTTONS);
    if (button) {
      rename(button);
    } else if (node.nodeType === Node.ELEMENT_NODE) {
      element.querySelectorAll(BUTTONS).forEach(rename);
    }
  };

  const painted =
      performance.getEntriesByName("first-contentful-paint").length > 0;
  const found = document.querySelectorAll(BUTTONS).length;
  if (document.documentElement) {
    visit(document.documentElement);
  }

  new MutationObserver((records) => {
    for (const record of records) {
      visit(record.target);
      record.addedNodes.forEach(visit);
    }
  }).observe(document, {
    subtree: true,
    childList: true,
    characterData: true,
    attributes: true,
    attributeFilter: ["jsname", "jsaction", "aria-label", "title"],
  });

  return { paintedBeforeScript: painted, buttonsBeforeScript: found };
})()
)js";

}  // namespace

void RenameWebStoreButtons(content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument() || navigation_handle->IsErrorPage()) {
    return;
  }
  const GURL& url = navigation_handle->GetURL();
  if (!url.SchemeIs(url::kHttpsScheme) || url.host() != kWebStoreHost) {
    return;
  }
  // A document back from the back-forward cache, or a prerendered one, may
  // have it already; the script runs once per document either way.
  navigation_handle->GetRenderFrameHost()->ExecuteJavaScriptInIsolatedWorld(
      kRenameScript, base::BindOnce([](base::Value result) {
        if (IsTracing() && result.is_dict()) {
          Trace("engine", "WebStore.renameButtons",
                std::move(result).TakeDict());
        }
      }),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

}  // namespace pane
