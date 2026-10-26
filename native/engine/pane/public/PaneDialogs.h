// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEDIALOGS_H_
#define PANE_PUBLIC_PANEDIALOGS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"

// What Chrome would show in its own dialogs and bubbles, handed to the app
// instead. Each request reaches the app through a PaneWebContentsDelegate
// method and is answered once, through its own methods; answering twice, or
// after the engine dismissed it, does nothing. A request the app never
// answers stays pending until the engine dismisses it (the page navigated or
// the tab closed), which the delegate hears of too.

NS_ASSUME_NONNULL_BEGIN

// MARK: - JavaScript dialogs

typedef NS_ENUM(NSInteger, PaneJavaScriptDialogType) {
  PaneJavaScriptDialogTypeAlert,
  PaneJavaScriptDialogTypeConfirm,
  PaneJavaScriptDialogTypePrompt,
  // "Leave site?" / "Reload site?" (beforeunload).
  PaneJavaScriptDialogTypeBeforeUnload,
  // "Confirm form resubmission" on reloading a POST.
  PaneJavaScriptDialogTypeFormResubmission,
} NS_SWIFT_NAME(JavaScriptDialogType);

// alert(), confirm(), prompt(), beforeunload, and form resubmission.
PANE_EXPORT
NS_SWIFT_NAME(JavaScriptDialog)
NS_SWIFT_UI_ACTOR
@interface PaneJavaScriptDialog : NSObject

@property(readonly) PaneJavaScriptDialogType type;
@property(readonly, copy) NSString* message;
// prompt()'s default text.
@property(readonly, copy) NSString* defaultPromptText;
// The origin of the frame that asked, for "<origin> says".
@property(readonly, copy, nullable) NSURL* originURL;
// beforeunload: whether the page is reloading rather than leaving.
@property(readonly, getter=isReload) BOOL reload;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

// OK (alert, confirm), the entered text (prompt), Leave/Reload
// (beforeunload), Continue (form resubmission).
- (void)acceptWithText:(nullable NSString*)text
    NS_SWIFT_NAME(accept(text:));
- (void)cancel;

@end

// MARK: - Permissions

// A site asks for one or more permissions at once (camera and microphone
// come together). Names are Chrome's permission keys: "geolocation",
// "notifications", "media_stream_camera", "media_stream_mic",
// "clipboard", "midi_sysex", "storage_access", …
PANE_EXPORT
NS_SWIFT_NAME(PermissionRequest)
NS_SWIFT_UI_ACTOR
@interface PanePermissionRequest : NSObject

@property(readonly, copy) NSArray<NSString*>* permissions;
@property(readonly, copy) NSURL* originURL;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)allow;
// Allows until the site is closed (Chrome's "Allow this time").
- (void)allowThisTime;
- (void)deny;
// Closed without a choice; the site may ask again later.
- (void)dismiss;

@end

// MARK: - HTTP and proxy authentication

PANE_EXPORT
NS_SWIFT_NAME(AuthenticationChallenge)
NS_SWIFT_UI_ACTOR
@interface PaneAuthenticationChallenge : NSObject

// The server (or proxy), as "scheme://host:port".
@property(readonly, copy) NSString* host;
@property(readonly, copy) NSString* realm;
// "basic", "digest", …
@property(readonly, copy) NSString* scheme;
// A proxy asks, not the site. (Not `isProxy`, which NSObject defines.)
@property(readonly, getter=isForProxy) BOOL forProxy;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)provideUsername:(NSString*)username
               password:(NSString*)password
    NS_SWIFT_NAME(provide(username:password:));
- (void)cancel;

@end

// MARK: - Context menu

typedef NS_ENUM(NSInteger, PaneContextMenuMediaType) {
  PaneContextMenuMediaTypeNone,
  PaneContextMenuMediaTypeImage,
  PaneContextMenuMediaTypeVideo,
  PaneContextMenuMediaTypeAudio,
  PaneContextMenuMediaTypeCanvas,
  PaneContextMenuMediaTypeOther,
} NS_SWIFT_NAME(ContextMenuMediaType);

// A right-click (or Control-click) in a page: what was under the pointer, and
// the actions on it. The app builds the menu; its items call these methods.
PANE_EXPORT
NS_SWIFT_NAME(ContextMenu)
NS_SWIFT_UI_ACTOR
@interface PaneContextMenu : NSObject

// Where, in the page view's coordinates (origin at the top left).
@property(readonly) NSPoint location;
@property(readonly, copy, nullable) NSURL* linkURL;
@property(readonly, copy) NSString* linkText;
@property(readonly) PaneContextMenuMediaType mediaType;
@property(readonly, copy, nullable) NSURL* sourceURL;
@property(readonly) BOOL hasImageContents;
@property(readonly, copy) NSString* selectionText;
@property(readonly, getter=isEditable) BOOL editable;
@property(readonly) BOOL canUndo;
@property(readonly) BOOL canRedo;
@property(readonly) BOOL canCut;
@property(readonly) BOOL canCopy;
@property(readonly) BOOL canPaste;
@property(readonly) BOOL canDelete;
@property(readonly) BOOL canSelectAll;
// The misspelled word under the pointer, if any, and the spellchecker's
// suggestions for it.
@property(readonly, copy) NSString* misspelledWord;
@property(readonly, copy) NSArray<NSString*>* dictionarySuggestions;

- (instancetype)init NS_UNAVAILABLE;

- (void)undo;
- (void)redo;
- (void)cut;
// Not `copy`, which NSObject defines.
- (void)copySelection;
- (void)paste;
- (void)deleteSelection;
- (void)selectAll;
- (void)replaceMisspellingWith:(NSString*)suggestion
    NS_SWIFT_NAME(replaceMisspelling(with:));
// Adds the misspelled word to the user's spelling dictionary.
- (void)addToDictionary;
- (void)copyImage;
- (void)copyLinkAddress;
// The image's, video's, or audio's own address.
- (void)copySourceAddress;
// Download the link, or the image, video, or audio, asking where to save it
// (through the window's file chooser).
- (void)saveLinkAs;
- (void)saveSourceAs;
// Opens DevTools on the element under the pointer.
- (void)inspectElement;

@end

// MARK: - File choosers

typedef NS_ENUM(NSInteger, PaneFileChooserMode) {
  PaneFileChooserModeOpen,
  PaneFileChooserModeOpenMultiple,
  PaneFileChooserModeOpenFolder,
  PaneFileChooserModeSave,
} NS_SWIFT_NAME(FileChooserMode);

// Every file dialog Chrome would show for a window: <input type=file>,
// showOpenFilePicker(), showSaveFilePicker(), Save As for a download.
PANE_EXPORT
NS_SWIFT_NAME(FileChooser)
NS_SWIFT_UI_ACTOR
@interface PaneFileChooser : NSObject

@property(readonly) PaneFileChooserMode mode;
@property(readonly, copy) NSString* title;
// File name extensions, without the dot. Empty: any file.
@property(readonly, copy) NSArray<NSString*>* allowedFileExtensions;
// Where to start, and for saving the suggested file name.
@property(readonly, copy, nullable) NSURL* directoryURL;
@property(readonly, copy) NSString* defaultFileName;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)chooseURLs:(NSArray<NSURL*>*)URLs NS_SWIFT_NAME(choose(_:));
- (void)cancel;

@end

// MARK: - External protocols

// A page opens a link another app handles (mailto:, zoommtg:, …).
PANE_EXPORT
NS_SWIFT_NAME(ExternalProtocolRequest)
NS_SWIFT_UI_ACTOR
@interface PaneExternalProtocolRequest : NSObject

@property(readonly, copy) NSURL* URL;
// The app macOS would open it with.
@property(readonly, copy) NSString* applicationName;
@property(readonly, copy, nullable) NSURL* originURL;
@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)open;
- (void)cancel;

@end

// MARK: - Unresponsive pages

// A page stopped responding. The app may wait or end it; the engine
// dismisses the request if the page recovers.
PANE_EXPORT
NS_SWIFT_NAME(UnresponsivePage)
NS_SWIFT_UI_ACTOR
@interface PaneUnresponsivePage : NSObject

@property(readonly, getter=isAnswered) BOOL answered;

- (instancetype)init NS_UNAVAILABLE;

- (void)wait;
// Ends the page's renderer: the tab shows nothing until reloaded.
- (void)terminate;

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEDIALOGS_H_
