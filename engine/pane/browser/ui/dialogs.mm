// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/browser/ui/dialogs_internal.h"

#include <utility>

#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/spellchecker/spellcheck_custom_dictionary.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "components/spellcheck/browser/spellcheck_platform.h"
#include "components/spellcheck/common/spellcheck_features.h"
#include "components/spellcheck/spellcheck_buildflags.h"
#include "components/download/public/common/download_url_parameters.h"
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
#include "third_party/blink/public/common/context_menu_data/edit_flags.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "url/gurl.h"

#import "pane/public/PaneBrowserWindow.h"
#import "pane/public/PaneWebContents.h"

namespace {

NSURL* _Nullable ToNSURL(const GURL& url) {
  return url.is_valid() ? net::NSURLWithGURL(url) : nil;
}

// Tells the tab's delegate that the engine took `request` back.
void NotifyDismissed(PaneWebContents* tab, id request, std::string_view kind) {
  PANE_TRACE("callback", "PaneWebContentsDelegate.didDismissRequest",
             base::DictValue().Set("kind", kind));
  id<PaneWebContentsDelegate> delegate = tab.delegate;
  if ([delegate respondsToSelector:@selector(webContents:
                                       didDismissRequest:)]) {
    pane::ScopedAppCallback callback;
    [delegate webContents:tab didDismissRequest:request];
  }
}

}  // namespace

// MARK: - PaneJavaScriptDialog

@implementation PaneJavaScriptDialog {
  __weak PaneWebContents* _tab;
  base::OnceCallback<void(bool, const std::u16string&)> _completion;
}

@synthesize type = _type;
@synthesize message = _message;
@synthesize defaultPromptText = _defaultPromptText;
@synthesize originURL = _originURL;
@synthesize reload = _reload;
@synthesize answered = _answered;

- (instancetype)initWithType:(PaneJavaScriptDialogType)type
                     message:(const std::u16string&)message
           defaultPromptText:(const std::u16string&)defaultPromptText
                   originURL:(const GURL&)originURL
                      reload:(BOOL)reload
                         tab:(PaneWebContents*)tab
                  completion:
                      (base::OnceCallback<void(bool, const std::u16string&)>)
                          completion {
  if ((self = [super init])) {
    _type = type;
    _message = base::SysUTF16ToNSString(message);
    _defaultPromptText = base::SysUTF16ToNSString(defaultPromptText);
    _originURL = ToNSURL(originURL);
    _reload = reload;
    _tab = tab;
    _completion = std::move(completion);
  }
  return self;
}

- (void)acceptWithText:(NSString*)text {
  PANE_TRACE("call", "PaneJavaScriptDialog.accept",
             base::DictValue().Set("text",
                                   base::SysNSStringToUTF8(text ? text : @"")));
  [self finish:true text:base::SysNSStringToUTF16(text ? text : @"")];
}

- (void)cancel {
  PANE_TRACE("call", "PaneJavaScriptDialog.cancel");
  [self finish:false text:std::u16string()];
}

- (void)withdrawAccepting:(BOOL)accept text:(const std::u16string&)text {
  if (_answered) {
    return;
  }
  [self finish:accept text:text];
  NotifyDismissed(_tab, self, "javaScriptDialog");
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _completion.Reset();
  NotifyDismissed(_tab, self, "javaScriptDialog");
}

- (void)finish:(bool)accepted text:(const std::u16string&)text {
  if (_answered) {
    return;
  }
  _answered = YES;
  if (_completion) {
    std::move(_completion).Run(accepted, text);
  }
}

@end

// MARK: - PanePermissionRequest

@implementation PanePermissionRequest {
  __weak PaneWebContents* _tab;
  base::OnceCallback<void(int)> _answer;
}

@synthesize permissions = _permissions;
@synthesize originURL = _originURL;
@synthesize answered = _answered;

- (instancetype)initWithPermissions:(NSArray<NSString*>*)permissions
                          originURL:(const GURL&)originURL
                                tab:(PaneWebContents*)tab
                             answer:(base::OnceCallback<void(int)>)answer {
  if ((self = [super init])) {
    _permissions = [permissions copy];
    _originURL = ToNSURL(originURL) ?: [NSURL URLWithString:@"about:blank"];
    _tab = tab;
    _answer = std::move(answer);
  }
  return self;
}

- (void)allow {
  [self answer:0 name:"PanePermissionRequest.allow"];
}

- (void)allowThisTime {
  [self answer:1 name:"PanePermissionRequest.allowThisTime"];
}

- (void)deny {
  [self answer:2 name:"PanePermissionRequest.deny"];
}

- (void)dismiss {
  [self answer:3 name:"PanePermissionRequest.dismiss"];
}

- (void)answer:(int)choice name:(std::string_view)name {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", name);
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(choice);
  }
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _answer.Reset();
  NotifyDismissed(_tab, self, "permission");
}

@end

// MARK: - PaneAuthenticationChallenge

@implementation PaneAuthenticationChallenge {
  __weak PaneWebContents* _tab;
  base::OnceCallback<void(
      std::optional<std::pair<std::u16string, std::u16string>>)>
      _answer;
}

@synthesize host = _host;
@synthesize realm = _realm;
@synthesize scheme = _scheme;
@synthesize forProxy = _forProxy;
@synthesize answered = _answered;

- (instancetype)
    initWithHost:(NSString*)host
           realm:(NSString*)realm
          scheme:(NSString*)scheme
           proxy:(BOOL)proxy
             tab:(PaneWebContents*)tab
          answer:(base::OnceCallback<
                     void(std::optional<std::pair<std::u16string,
                                                  std::u16string>>)>)answer {
  if ((self = [super init])) {
    _host = [host copy];
    _realm = [realm copy];
    _scheme = [scheme copy];
    _forProxy = proxy;
    _tab = tab;
    _answer = std::move(answer);
  }
  return self;
}

- (void)provideUsername:(NSString*)username password:(NSString*)password {
  if (_answered) {
    return;
  }
  // The password is never traced.
  PANE_TRACE(
      "call", "PaneAuthenticationChallenge.provide",
      base::DictValue().Set("username", base::SysNSStringToUTF8(username)));
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(std::make_pair(base::SysNSStringToUTF16(username),
                                          base::SysNSStringToUTF16(password)));
  }
}

- (void)cancel {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", "PaneAuthenticationChallenge.cancel");
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(std::nullopt);
  }
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _answer.Reset();
  NotifyDismissed(_tab, self, "authentication");
}

@end

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
}

@synthesize location = _location;
@synthesize linkURL = _linkURL;
@synthesize linkText = _linkText;
@synthesize mediaType = _mediaType;
@synthesize sourceURL = _sourceURL;
@synthesize hasImageContents = _hasImageContents;
@synthesize selectionText = _selectionText;
@synthesize editable = _editable;
@synthesize canUndo = _canUndo;
@synthesize canRedo = _canRedo;
@synthesize canCut = _canCut;
@synthesize canCopy = _canCopy;
@synthesize canPaste = _canPaste;
@synthesize canDelete = _canDelete;
@synthesize canSelectAll = _canSelectAll;
@synthesize misspelledWord = _misspelledWordString;
@synthesize dictionarySuggestions = _dictionarySuggestions;

- (instancetype)initWithParams:(const content::ContextMenuParams&)params
                         frame:(content::RenderFrameHost&)frame {
  if ((self = [super init])) {
    _contents =
        content::WebContents::FromRenderFrameHost(&frame)->GetWeakPtr();
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
    _linkText = base::SysUTF16ToNSString(params.link_text);
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
    _hasImageContents = params.has_image_contents;
    _selectionText = base::SysUTF16ToNSString(params.selection_text);
    _editable = params.is_editable;
    _canUndo = (params.edit_flags & blink::ContextMenuDataEditFlags::kCanUndo) != 0;
    _canRedo = (params.edit_flags & blink::ContextMenuDataEditFlags::kCanRedo) != 0;
    _canCut = (params.edit_flags & blink::ContextMenuDataEditFlags::kCanCut) != 0;
    _canCopy = (params.edit_flags & blink::ContextMenuDataEditFlags::kCanCopy) != 0;
    _canPaste =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanPaste) != 0;
    _canDelete =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanDelete) != 0;
    _canSelectAll =
        (params.edit_flags & blink::ContextMenuDataEditFlags::kCanSelectAll) !=
        0;
    _misspelledWordString = base::SysUTF16ToNSString(params.misspelled_word);
    NSMutableArray<NSString*>* suggestions = [NSMutableArray array];
    for (const std::u16string& suggestion : params.dictionary_suggestions) {
      [suggestions addObject:base::SysUTF16ToNSString(suggestion)];
    }
    _dictionarySuggestions = [suggestions copy];
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
  [self edit:&content::WebContents::SelectAll
        name:"PaneContextMenu.selectAll"];
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
// where to save, which reaches the app as the window's file chooser.
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

@end

// MARK: - PaneFileChooser

@implementation PaneFileChooser {
  __weak PaneBrowserWindow* _window;
  base::OnceCallback<void(std::optional<std::vector<base::FilePath>>)> _answer;
}

@synthesize mode = _mode;
@synthesize title = _title;
@synthesize allowedFileExtensions = _allowedFileExtensions;
@synthesize directoryURL = _directoryURL;
@synthesize defaultFileName = _defaultFileName;
@synthesize answered = _answered;

- (instancetype)
    initWithMode:(PaneFileChooserMode)mode
           title:(NSString*)title
      extensions:(NSArray<NSString*>*)extensions
       directory:(NSURL*)directory
     defaultName:(NSString*)defaultName
          window:(PaneBrowserWindow*)window
          answer:(base::OnceCallback<
                     void(std::optional<std::vector<base::FilePath>>)>)answer {
  if ((self = [super init])) {
    _mode = mode;
    _title = [title copy];
    _allowedFileExtensions = [extensions copy];
    _directoryURL = [directory copy];
    _defaultFileName = [defaultName copy];
    _window = window;
    _answer = std::move(answer);
  }
  return self;
}

- (void)chooseURLs:(NSArray<NSURL*>*)URLs {
  if (_answered) {
    return;
  }
  std::vector<base::FilePath> paths;
  base::ListValue traced;
  for (NSURL* url in URLs) {
    if (url.fileURL) {
      paths.emplace_back(base::SysNSStringToUTF8(url.path));
      traced.Append(base::SysNSStringToUTF8(url.path));
    }
  }
  PANE_TRACE("call", "PaneFileChooser.choose",
             base::DictValue().Set("paths", std::move(traced)));
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(paths.empty()
                               ? std::nullopt
                               : std::make_optional(std::move(paths)));
  }
}

- (void)cancel {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", "PaneFileChooser.cancel");
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(std::nullopt);
  }
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _answer.Reset();
  PANE_TRACE("callback", "PaneBrowserWindowDelegate.didDismissFileChooser");
  PaneBrowserWindow* window = _window;
  id<PaneBrowserWindowDelegate> delegate = window.delegate;
  if ([delegate respondsToSelector:@selector(browserWindow:
                                       didDismissFileChooser:)]) {
    [delegate browserWindow:window didDismissFileChooser:self];
  }
}

@end

// MARK: - PaneExternalProtocolRequest

@implementation PaneExternalProtocolRequest {
  __weak PaneWebContents* _tab;
  base::OnceCallback<void(bool)> _answer;
}

@synthesize URL = _URL;
@synthesize applicationName = _applicationName;
@synthesize originURL = _originURL;
@synthesize answered = _answered;

- (instancetype)initWithURL:(const GURL&)URL
            applicationName:(const std::u16string&)applicationName
                  originURL:(const GURL&)originURL
                        tab:(PaneWebContents*)tab
                     answer:(base::OnceCallback<void(bool)>)answer {
  if ((self = [super init])) {
    _URL = ToNSURL(URL) ?: [NSURL URLWithString:@"about:blank"];
    _applicationName = base::SysUTF16ToNSString(applicationName);
    _originURL = ToNSURL(originURL);
    _tab = tab;
    _answer = std::move(answer);
  }
  return self;
}

- (void)open {
  [self answer:true name:"PaneExternalProtocolRequest.open"];
}

- (void)cancel {
  [self answer:false name:"PaneExternalProtocolRequest.cancel"];
}

- (void)answer:(bool)open name:(std::string_view)name {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", name);
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(open);
  }
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _answer.Reset();
  NotifyDismissed(_tab, self, "externalProtocol");
}

@end

// MARK: - PaneUnresponsivePage

@implementation PaneUnresponsivePage {
  __weak PaneWebContents* _tab;
  base::OnceCallback<void(bool)> _answer;
}

@synthesize answered = _answered;

- (instancetype)initWithTab:(PaneWebContents*)tab
                     answer:(base::OnceCallback<void(bool)>)answer {
  if ((self = [super init])) {
    _tab = tab;
    _answer = std::move(answer);
  }
  return self;
}

- (void)wait {
  [self answer:false name:"PaneUnresponsivePage.wait"];
}

- (void)terminate {
  [self answer:true name:"PaneUnresponsivePage.terminate"];
}

- (void)answer:(bool)terminate name:(std::string_view)name {
  if (_answered) {
    return;
  }
  PANE_TRACE("call", name);
  _answered = YES;
  if (_answer) {
    std::move(_answer).Run(terminate);
  }
}

- (void)withdraw {
  if (_answered) {
    return;
  }
  _answered = YES;
  _answer.Reset();
  NotifyDismissed(_tab, self, "unresponsive");
}

@end
