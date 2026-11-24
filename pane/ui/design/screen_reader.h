// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_SCREEN_READER_H_
#define PANE_UI_DESIGN_SCREEN_READER_H_

#include <string>

namespace ui {
struct AXActionData;
}

namespace views {
class View;
class ViewAccessibility;
}  // namespace views

namespace pane::screen_reader {

// What a screen reader is told of a control's kind and state where VoiceOver
// and Windows' UI Automation read Views' accessibility apart (ADR 0071):
// each system's half gives the data its own screen reader reads right. The
// Mac's keeps what VoiceOver has read since ADR 0071; Windows' was chosen by
// what Narrator says of each of ARIA's roles in Pane's own pages, which reach
// UI Automation by the same mapping as Pane's views (ADR 0071; 2026-11-16,
// commit 490757e).

// A picker (MenuButton), its label its name and its option its value: a pop
// up button on the Mac ("System Theme menu pop up pop up button"); a combo
// box on Windows, since UI Automation gives a button no value ("Theme,
// System, combo box, collapsed").
void SetPicker(views::ViewAccessibility& picker);

// A few choices, one of them chosen (a profile's color and platform, the App
// icon's sky): the group, and each choice at its place, from 1, of `count`.
// On the Mac buttons in a group, the chosen one selected; on Windows a radio
// group and its radio buttons, since UI Automation reads no button's
// selection ("Blue, radio button, selected, 1 of 9").
void SetChoices(views::ViewAccessibility& group);
// A choice, unchosen until SetChosen says otherwise.
void SetChoice(views::ViewAccessibility& choice, int position, int count);
// Whether the choice is the one chosen.
void SetChosen(views::ViewAccessibility& choice, bool chosen);
// The same, from the button's own UpdateAccessibleCheckedState, which Views'
// Button calls at each change of its state, setting a checked state while
// pressed and none otherwise: Windows' radio button takes its check again;
// the Mac's button keeps Views' own.
void KeepChosen(views::ViewAccessibility& choice, bool chosen);

// The sidebar's rows (ADR 0071): its list, a profile's row, and a tab's row,
// each at its place, from 1, of `count` (profiles among the profiles, tabs
// among their profile's). On the Mac buttons, a profile's named by its
// `name` with its `value` ("3 tabs, collapsed, proxy connected"), each
// selected while its page shows. On Windows a tree, as Explorer's own panes
// are read: a profile an item of its first level, expanded or collapsed, a
// tab one of its second, each selected while it shows, since UI Automation
// reads no button's selection ("Work, 1 of 2 level 1, expanded, selected").
// A tree item has no value there, and Narrator reads no tree item's
// description, so a profile's `summary` ("3 tabs, proxy connected") follows
// its name, as Outlook's folders read their unread count ("Work, 3 tabs").
void SetSidebarList(views::ViewAccessibility& list);
void SetProfileRow(views::ViewAccessibility& row,
                   const std::u16string& name,
                   const std::u16string& value,
                   const std::u16string& summary,
                   bool expanded,
                   bool shown);
void SetTabRow(views::ViewAccessibility& row, bool shown);
void SetPlace(views::ViewAccessibility& row, int position, int count);

// A button that shows a page while it is on (Settings): selected on the Mac;
// on Windows a toggle button, on while its page shows ("Settings, toggle
// button, on"), kept from the button's own UpdateAccessibleCheckedState as a
// choice is.
void SetShowing(views::ViewAccessibility& button, bool showing);
void KeepShowing(views::ViewAccessibility& button, bool showing);

// A button that opens a menu (the extensions' puzzle) or perhaps a popup (an
// extension's action), `open` while it shows: selected then on the Mac. On
// Windows the menu's button has its popup and is expanded or collapsed, as
// Chrome's puzzle ("Extensions, button, expanded, has popup"); an action is
// expanded while its popup shows and a plain button otherwise, since an
// action without a popup has none, and what expands has no Invoke in UI
// Automation.
void SetOpens(views::ViewAccessibility& button, bool menu, bool open);

// UI Automation's Expand or Collapse asked of a button that opens something
// (a menu, a popup), which Chromium gives what expands in Invoke's place: a
// press, as a click opens or closes it, when the button is the other way,
// and nothing when it is already so. True when `action` was either and the
// button says it opens something (its popup, or its state), as
// Chrome's app menu button takes both (BrowserAppMenuButton): Views acts on
// neither, so Narrator's primary action, and the runner's press, were
// refused on "Pane ⌄" and the pickers (commit 490757e).
bool PressToExpand(views::View& button, const ui::AXActionData& action);

// A heading at its `level`: VoiceOver says "heading" of any; Narrator only of
// one with a level ("Appearance, heading level 2").
void SetHeading(views::ViewAccessibility& heading, int level);

// A graphic read by its name (the PIN's dots, "2 of 4 digits"): a group on
// the Mac, which VoiceOver reads; an image on Windows, since Narrator walks
// past a group with nothing in it, and its walk can't leave text that holds
// no text of its own ("Unable to move Narrator cursor" at the dots; commit
// 490757e).
void SetIndicator(views::ViewAccessibility& indicator);

// The PIN's field and its dots, the count typed ("2 of 4 digits"): on the
// Mac the dots read on their own, as VoiceOver has read them; on Windows the
// field described by the count and the dots left out, since Narrator,
// entering the PIN's dialog, read on past the field to the dots and stayed
// there, speaking each digit typed rather than "Hidden" (commit f475f04).
void SetPinCount(views::ViewAccessibility& field,
                 views::ViewAccessibility& dots,
                 const std::u16string& count);

// The sidebar's edge, valued at the width (ADR 0071): on the Mac a vertical
// splitter, the bar between the sidebar and the page, where VoiceOver said
// "horizontal" of one with no orientation (Chromium's Mac node for a view
// gives its orientation by patch 0004); a slider on Windows, moving
// sideways, as a slider is by default, since Narrator reads no thumb's
// value, and UI Automation makes a focusable splitter a thumb ("Sidebar
// width, slider at 260").
void SetSplitter(views::ViewAccessibility& edge);

}  // namespace pane::screen_reader

#endif  // PANE_UI_DESIGN_SCREEN_READER_H_
