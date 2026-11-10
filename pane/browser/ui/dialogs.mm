// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <utility>

#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/spellchecker/spellcheck_custom_dictionary.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/spellcheck/browser/spellcheck_platform.h"
#include "components/spellcheck/common/spellcheck_features.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/apple/url_conversions.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "pane/browser/app_callback.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/extensions/extension_menu.h"
#import "pane/browser/ui/dialogs_internal.h"
#import "pane/public/PaneBrowserWindow.h"
#import "pane/public/PaneWebContents.h"
#include "third_party/blink/public/common/context_menu_data/edit_flags.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "url/gurl.h"

namespace {

NSURL* _Nullable ToNSURL(const GURL& url) {
  return url.is_valid() ? net::NSURLWithGURL(url) : nil;
}

}  // namespace

// MARK: - PaneContextMenu

@implementation PaneContextMenu {
  base::WeakPtr<content::WebContents> _contents;
  content::GlobalRenderFrameHostId _frame;
  int _x;
  int _y;
  GURL _link;
  GURL _source;
  std::u16string _misspelledWord;
  std::u16string _suggestedFileName;
  std::string _frameCharset;
  std::unique_ptr<pane::ExtensionMenu> _extensionMenu;
}

@synthesize misspelledWord = _misspelledWordString;

- (instancetype)initWithParams:(const content::ContextMenuParams&)params
                         frame:(content::RenderFrameHost&)frame {
  if ((self = [super init])) {
    _contents = content::WebContents::FromRenderFrameHost(&frame)->GetWeakPtr();
    _frame = frame.GetGlobalId();
    _x = params.x;
    _y = params.y;
    _link = params.link_url;
    _source = params.src_url;
    _misspelledWord = params.misspelled_word;
    _suggestedFileName = params.suggested_filename;
    _frameCharset = params.frame_charset;
    _location = NSMakePoint(params.x, params.y);
    _linkURL = ToNSURL(params.link_url);
    switch (params.media_type) {
      case blink::mojom::ContextMenuDataMediaType::kNone:
        _mediaType = PaneContextMenuMediaTypeNone;
        break;
      case blink::mojom::ContextMenuDataMediaType::kImage:
        _mediaType = PaneContextMenuMediaTypeImage;
        break;
      case blink::mojom::ContextMenuDataMediaType::kVideo:
        _mediaType = PaneContextMenuMediaTypeVideo;
        break;
      case blink::mojom::ContextMenuDataMediaType::kAudio:
        _mediaType = PaneContextMenuMediaTypeAudio;
        break;
      case blink::mojom::ContextMenuDataMediaType::kCanvas:
        _mediaType = PaneContextMenuMediaTypeCanvas;
        break;
      default:
        _mediaType = PaneContextMenuMediaTypeOther;
        break;
    }
    _sourceURL = ToNSURL(params.src_url);
    _selectionText = base::SysUTF16ToNSString(params.selection_text);
    _editable = params.is_editable;
    _canUndo =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanUndo) != 0;
    _canRedo =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanRedo) != 0;
    _canCut =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanCut) != 0;
    _canCopy =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanCopy) != 0;
    _canPaste =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanPaste) != 0;
    _canDelete =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanDelete) != 0;
    _canSelectAll = (params.edit_flags &
                     blink::ContextMenuDataEditFlags::kCanSelectAll) != 0;
    _misspelledWordString = base::SysUTF16ToNSString(params.misspelled_word);
    NSMutableArray<NSString*>* suggestions = [NSMutableArray array];
    for (const std::u16string& suggestion : params.dictionary_suggestions) {
      [suggestions addObject:base::SysUTF16ToNSString(suggestion)];
    }
    _dictionarySuggestions = [suggestions copy];
    _extensionMenu =
        pane::ExtensionMenu::ForPage(frame.GetBrowserContext(), params);
    _extensionItems = _extensionMenu->Items();
  }
  return self;
}

- (content::RenderFrameHost*)frameHost {
  return content::RenderFrameHost::FromID(_frame);
}

- (void)edit:(void (content::WebContents::*)())action
        name:(std::string_view)name {
  PANE_TRACE("call", name);
  if (_contents) {
    (_contents.get()->*action)();
  }
}

- (void)undo {
  [self edit:&content::WebContents::Undo name:"PaneContextMenu.undo"];
}

- (void)redo {
  [self edit:&content::WebContents::Redo name:"PaneContextMenu.redo"];
}

- (void)cut {
  [self edit:&content::WebContents::Cut name:"PaneContextMenu.cut"];
}

- (void)copySelection {
  [self edit:&content::WebContents::Copy name:"PaneContextMenu.copy"];
}

- (void)paste {
  [self edit:&content::WebContents::Paste name:"PaneContextMenu.paste"];
}

- (void)deleteSelection {
  [self edit:&content::WebContents::Delete name:"PaneContextMenu.delete"];
}

- (void)selectAll {
  [self edit:&content::WebContents::SelectAll name:"PaneContextMenu.selectAll"];
}

- (void)replaceMisspellingWith:(NSString*)suggestion {
  PANE_TRACE(
      "call", "PaneContextMenu.replaceMisspelling",
      base::DictValue().Set("suggestion", base::SysNSStringToUTF8(suggestion)));
  if (_contents) {
    _contents->ReplaceMisspelling(base::SysNSStringToUTF16(suggestion));
  }
}

// As Chrome's SpellingMenuObserver::AddToDictionary: the profile's custom
// dictionary, and the platform spellchecker the page checks with.
- (void)addToDictionary {
  PANE_TRACE("call", "PaneContextMenu.addToDictionary",
             base::DictValue().Set("word", base::UTF16ToUTF8(_misspelledWord)));
  if (!_contents || _misspelledWord.empty()) {
    return;
  }
  SpellcheckService* spellcheck =
      SpellcheckServiceFactory::GetForContext(_contents->GetBrowserContext());
  if (!spellcheck) {
    return;
  }
  spellcheck->GetCustomDictionary()->AddWord(
      base::UTF16ToUTF8(_misspelledWord));
#if BUILDFLAG(USE_BROWSER_SPELLCHECKER)
  if (spellcheck::UseBrowserSpellChecker()) {
    spellcheck_platform::AddWord(spellcheck->platform_spell_checker(),
                                 _misspelledWord);
  }
#endif
}

- (void)copyImage {
  PANE_TRACE("call", "PaneContextMenu.copyImage");
  if (content::RenderFrameHost* frame = [self frameHost]) {
    frame->CopyImageAt(_x, _y);
  }
}

- (void)copyLinkAddress {
  PANE_TRACE("call", "PaneContextMenu.copyLinkAddress");
  if (_link.is_valid()) {
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteText(base::UTF8ToUTF16(_link.spec()));
  }
}

- (void)copySourceAddress {
  PANE_TRACE("call", "PaneContextMenu.copySourceAddress");
  if (_source.is_valid()) {
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
        .WriteText(base::UTF8ToUTF16(_source.spec()));
  }
}

- (void)saveLinkAs {
  PANE_TRACE("call", "PaneContextMenu.saveLinkAs");
  [self saveURL:_link];
}

- (void)saveSourceAs {
  PANE_TRACE("call", "PaneContextMenu.saveSourceAs");
  [self saveURL:_source];
}

// As Chrome's RenderViewContextMenu::ExecSaveLinkAs: a download that asks
// where to save, in the system's save panel.
- (void)saveURL:(const GURL&)url {
  content::RenderFrameHost* frame = [self frameHost];
  if (!frame || !url.is_valid()) {
    return;
  }
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("pane_context_menu_save_as", R"(
        semantics {
          sender: "Pane context menu"
          description: "Saves a link, image, video, or audio to a file."
          trigger:
            "The user picks 'Save link as...' or 'Save image as...' in a "
            "page's context menu."
          data: "None."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "This feature cannot be disabled by settings."
          policy_exception_justification: "Not implemented."
        })");
  std::unique_ptr<download::DownloadUrlParameters> params =
      frame->CreateDownloadUrlParameters(url, traffic_annotation);
  params->set_referrer(frame->GetLastCommittedURL());
  params->set_referrer_encoding(_frameCharset);
  params->set_suggested_name(_suggestedFileName);
  params->set_prompt(true);
  params->set_download_source(download::DownloadSource::CONTEXT_MENU);
  frame->GetBrowserContext()->GetDownloadManager()->DownloadUrl(
      std::move(params));
}

- (void)inspectElement {
  PANE_TRACE("call", "PaneContextMenu.inspectElement");
  if (content::RenderFrameHost* frame = [self frameHost]) {
    DevToolsWindow::InspectElement(frame, _x, _y);
  }
}

- (void)runExtensionItem:(PaneExtensionMenuItem*)item {
  PANE_TRACE(
      "call", "PaneContextMenu.runExtensionItem",
      base::DictValue().Set("title", base::SysNSStringToUTF8(item.title)));
  if (_contents) {
    _extensionMenu->Run(item, _contents.get(), [self frameHost]);
  }
}

@end
