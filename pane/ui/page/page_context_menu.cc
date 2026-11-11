// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/page_context_menu.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/tab_contents/chrome_web_contents_menu_helper.h"
#include "components/renderer_context_menu/render_view_context_menu_base.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "pane/browser/engine_trace.h"
#include "pane/ui/app/app_events.h"
#include "pane/ui/interaction_log.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/models/menu_model.h"
#include "ui/gfx/text_utils.h"

namespace pane {

namespace {

// What Pane can't support (the user's answers on Paper 10.4 and 10.4.1).
constexpr int kHidden[] = {
    // Pane opens a link in a new tab only, turns windows into tabs, and has
    // no incognito: a profile is apart already.
    IDC_CONTENT_CONTEXT_OPENLINKNEWWINDOW,
    IDC_CONTENT_CONTEXT_OPENLINKSPLITVIEW,
    IDC_CONTENT_CONTEXT_OPENLINKOFFTHERECORD,
    // Open Link as: Chrome opens the profile itself (SwitchToProfile), with
    // none of what Pane opens a profile with, and names it as it knows it
    // (Person 2), never by Pane's name.
    IDC_CONTENT_CONTEXT_OPENLINKINPROFILE,
    // What needs Chrome's own browser window: Lens, on the page and on an
    // image, Reading Mode, a QR code, and Gemini.
    IDC_CONTENT_CONTEXT_LENS_REGION_SEARCH,
    IDC_CONTENT_CONTEXT_WEB_REGION_SEARCH,
    IDC_CONTENT_CONTEXT_LENS_OVERLAY,
    IDC_CONTENT_CONTEXT_SEARCHLENSFORIMAGE,
    IDC_CONTENT_CONTEXT_SEARCHWEBFORIMAGE,
    IDC_CONTENT_CONTEXT_OPEN_IN_READING_MODE,
    IDC_CONTENT_CONTEXT_GENERATE_QR_CODE,
    IDC_CONTENT_CONTEXT_GLIC,
    IDC_CONTENT_CONTEXT_GLICSHAREIMAGE,
    IDC_CONTENT_CONTEXT_GLIC_SMART_SUGGESTION,
    // Chrome's settings pages, and Google's spelling service.
    IDC_CONTENT_CONTEXT_LANGUAGE_SETTINGS,
    IDC_CONTENT_CONTEXT_SPELLING_TOGGLE,
    // The password manager, which Pane turns off, though the menu doesn't
    // read that.
    IDC_CONTENT_CONTEXT_SHOWALLSAVEDPASSWORDS,
    IDC_CONTENT_CONTEXT_AUTOFILL_FALLBACK_PASSWORDS_IMPORT_PASSWORDS,
};
// The other profiles, one each, when Chrome lists them on their own.
constexpr int kOpenLinkInProfiles = 100;

// An item's title as the menu shows it: Chrome's carries Windows' mnemonics
// ("Open Link in New &Tab"), which the Mac's menu leaves out, as Views'.
std::u16string ShownTitle(const ui::MenuModel& model, size_t index) {
  return gfx::RemoveAccelerator(model.GetLabelAt(index));
}

// A menu's item at `index` as shown: a separator, or what doesn't show.
bool Shows(const ui::MenuModel& model, size_t index) {
  return model.GetTypeAt(index) != ui::MenuModel::TYPE_SEPARATOR &&
         model.IsVisibleAt(index);
}

// The item titled `title` in `model` or a submenu of it: the submenu that
// holds it (none for `model` itself), its index there, and its command.
struct Found {
  raw_ptr<ui::MenuModel> submenu = nullptr;
  size_t index = 0;
  bool enabled = false;
};

std::optional<Found> Find(const ui::MenuModel& model,
                          ui::MenuModel* submenu,
                          std::u16string_view title) {
  for (size_t index = 0; index < model.GetItemCount(); ++index) {
    if (!Shows(model, index)) {
      continue;
    }
    if (ShownTitle(model, index) == title) {
      return Found{.submenu = submenu,
                   .index = index,
                   .enabled = model.IsEnabledAt(index)};
    }
    if (ui::MenuModel* items = model.GetSubmenuModelAt(index)) {
      if (std::optional<Found> found = Find(*items, items, title)) {
        return found;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

PageContextMenus::PageContextMenus() = default;

PageContextMenus::~PageContextMenus() = default;

void PageContextMenus::Show(content::RenderFrameHost& frame,
                            const content::ContextMenuParams& params) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(&frame);
  // The renderer asks again for each right-click it took while the menu
  // was on its way (ChromeWebContentsViewDelegateViewsMac::ShowMenu).
  if (!contents || contents->IsShowingContextMenu()) {
    return;
  }
  const base::TimeTicks asked = base::TimeTicks::Now();
  // Timed only where the platform's menu shows.
  const std::optional<uint64_t> interval =
      MayTakeFront() ? std::optional<uint64_t>(BeginInteractionInterval(
                           Interaction::kShowContextMenu))
                     : std::nullopt;
  // As Chrome's: a tab not yet active takes the focus first, and Paste's
  // items follow the clipboard's kinds, read before the menu is built.
  if (tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(contents);
      tab && !tab->IsActivated()) {
    contents->Focus();
  }
  std::optional<ui::DataTransferEndpoint> data_dst;
  if (params.page_url.is_valid()) {
    data_dst.emplace(
        params.page_url,
        ui::DataTransferEndpointOptions{
            .notify_if_restricted = false,
            .off_the_record = contents->GetBrowserContext()->IsOffTheRecord(),
        });
  }
  ui::Clipboard::GetForCurrentThread()->ReadAvailableTypes(
      ui::ClipboardBuffer::kCopyPaste, data_dst,
      base::BindOnce(
          &PageContextMenus::TypesRead, weak_factory_.GetWeakPtr(),
          frame.GetGlobalId(),
          AddContextMenuParamsPropertiesFromPreferences(contents, params),
          data_dst, asked, interval));
}

void PageContextMenus::TypesRead(
    content::GlobalRenderFrameHostId frame,
    content::ContextMenuParams params,
    std::optional<ui::DataTransferEndpoint> data_dst,
    base::TimeTicks asked,
    std::optional<uint64_t> interval,
    std::vector<std::u16string> types) {
  ui::Clipboard::GetForCurrentThread()->GetAllAvailableFormats(
      ui::ClipboardBuffer::kCopyPaste, data_dst,
      base::BindOnce(&PageContextMenus::FormatsRead, weak_factory_.GetWeakPtr(),
                     frame, std::move(params), asked, interval,
                     !types.empty()));
}

void PageContextMenus::FormatsRead(
    content::GlobalRenderFrameHostId frame_id,
    content::ContextMenuParams params,
    base::TimeTicks asked,
    std::optional<uint64_t> interval,
    bool paste,
    base::flat_set<ui::ClipboardFormatType> formats) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(frame_id);
  // One menu at a time: the system's runs its own loop, in which another
  // right-click's menu, on its way already, mustn't replace it.
  if (!frame || running_) {
    return;
  }
  // How long the menu takes to make, the extensions' items with it, as the
  // Swift's engine timed it (madeMs), and from the right-click (readyMs).
  base::ElapsedTimer made;
  std::unique_ptr<RenderViewContextMenuBase> menu =
      Make(*frame, params, paste,
           formats.contains(ui::ClipboardFormatType::PlainTextType()));
  menu->Init();
  Filter(*menu);
  PANE_TRACE(
      "callback", "PaneWebContentsDelegate.showContextMenu",
      base::DictValue()
          .Set("madeMs", made.Elapsed().InMillisecondsF())
          .Set("readyMs", (base::TimeTicks::Now() - asked).InMillisecondsF())
          .Set("x", params.x)
          .Set("y", params.y)
          .Set("link", params.link_url.spec())
          .Set("editable", params.is_editable)
          .Set("misspelled", base::UTF16ToUTF8(params.misspelled_word))
          .Set("spellcheck", params.spellcheck_enabled)
          .Set("suggestions",
               static_cast<int>(params.dictionary_suggestions.size())));
  Release();
  menu_ = std::move(menu);
  page_ = content::WebContents::FromRenderFrameHost(frame)->GetWeakPtr();
  ++shown_;
  if (!interval) {
    return;
  }
  // The platform's menu, in its own loop: when it returns, it has closed.
  base::WeakPtr<PageContextMenus> self = weak_factory_.GetWeakPtr();
  RenderViewContextMenuBase* shown = menu_.get();
  running_ = true;
  Run(*shown, base::BindOnce(
                  [](uint64_t interval) {
                    EndInteractionInterval(Interaction::kShowContextMenu,
                                           interval);
                  },
                  *interval));
  if (self && menu_.get() == shown) {
    running_ = false;
    // Let go a turn later: the platform's menu tells its model it closed on
    // a task of its own (SimpleMenuModel::MenuWillClose), whose MenuClosed
    // lets the page show its next menu (SetShowingContextMenu(false)), as
    // Chrome's delegate keeps its menu until the next.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<PageContextMenus> menus, int serial) {
                         if (menus && menus->shown_ == serial) {
                           menus->Release();
                         }
                       },
                       self, shown_));
  }
}

// Chrome's own items, Pane's few off: off the model before it shows, where
// each would otherwise be a stock item.
void PageContextMenus::Filter(RenderViewContextMenuBase& menu) {
  for (const int command : kHidden) {
    menu.RemoveMenuItem(command);
  }
  for (int command = IDC_OPEN_LINK_IN_PROFILE_FIRST;
       command < IDC_OPEN_LINK_IN_PROFILE_FIRST + kOpenLinkInProfiles;
       ++command) {
    menu.RemoveMenuItem(command);
  }
  menu.RemoveAdjacentSeparators();
  // A separator left first, where a hidden item led.
  const ui::MenuModel& model = menu.menu_model();
  if (model.GetItemCount() > 1 &&
      model.GetTypeAt(0) == ui::MenuModel::TYPE_SEPARATOR) {
    menu.RemoveSeparatorBeforeMenuItem(model.GetCommandIdAt(1));
  }
}

const ui::MenuModel* PageContextMenus::model() const {
  return menu_ && page_ ? &menu_->menu_model() : nullptr;
}

bool PageContextMenus::Choose(std::u16string_view title) {
  if (!menu_ || !page_) {
    Release();
    return false;
  }
  const ui::MenuModel& model = menu_->menu_model();
  const std::optional<Found> found = Find(model, nullptr, title);
  if (!found || !found->enabled) {
    return false;
  }
  // The platform's menu closes as an item is chosen in it.
  if (running_) {
    menu_->Cancel();
  }
  // As a click: the menu's own item runs its command, a submenu's runs
  // through that submenu's model, whose delegate may be another's (Writing
  // Direction's, Speech's).
  if (found->submenu) {
    found->submenu->ActivatedAt(found->index);
  } else {
    menu_->ExecuteCommand(model.GetCommandIdAt(found->index), 0);
  }
  // Let go once its command has run; the platform's menu, as its loop ends.
  if (!running_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&PageContextMenus::Release, weak_factory_.GetWeakPtr()));
  }
  return true;
}

void PageContextMenus::Release() {
  if (!running_) {
    menu_.reset();
  }
}

// static
base::ListValue PageContextMenus::Describe(const ui::MenuModel& model) {
  return Describe(model, 0, model.GetItemCount());
}

base::ListValue PageContextMenus::Describe(const ui::MenuModel& model,
                                           size_t begin,
                                           size_t end) {
  base::ListValue items;
  for (size_t index = begin; index < std::min(end, model.GetItemCount());
       ++index) {
    if (!Shows(model, index)) {
      continue;
    }
    base::DictValue item =
        base::DictValue()
            .Set("title", base::UTF16ToUTF8(ShownTitle(model, index)))
            .Set("command", model.GetCommandIdAt(index))
            .Set("enabled", model.IsEnabledAt(index))
            .Set("checked", model.IsItemCheckedAt(index));
    if (ui::MenuModel* submenu = model.GetSubmenuModelAt(index)) {
      item.Set("submenu", Describe(*submenu));
    }
    items.Append(std::move(item));
  }
  return items;
}

}  // namespace pane
