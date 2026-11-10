// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_PUBLIC_PANEDIALOGS_H_
#define PANE_PUBLIC_PANEDIALOGS_H_

#import <AppKit/AppKit.h>

#import "PaneExport.h"
#import "PaneExtensions.h"

// A page's right-click menu, handed to the app instead of Chrome's own. A
// page's dialogs, permission requests, sign-in, external apps, and
// unresponsive pages show in Pane's Views window (pane/ui/dialogs), and file
// choosers are the system's own panels (ADR 0021).

NS_ASSUME_NONNULL_BEGIN

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
@property(readonly) PaneContextMenuMediaType mediaType;
@property(readonly, copy, nullable) NSURL* sourceURL;
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
// The items extensions add for what was clicked, in Chrome's order and
// grouping.
@property(readonly, copy) NSArray<PaneExtensionMenuItem*>* extensionItems;

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
// (the system's save panel).
- (void)saveLinkAs;
- (void)saveSourceAs;
// Opens DevTools on the element under the pointer.
- (void)inspectElement;
// Runs one of extensionItems (onClicked in the extension).
- (void)runExtensionItem:(PaneExtensionMenuItem*)item
    NS_SWIFT_NAME(runExtensionItem(_:));

@end

NS_ASSUME_NONNULL_END

#endif  // PANE_PUBLIC_PANEDIALOGS_H_
