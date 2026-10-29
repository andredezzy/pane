# Paper draws every state the app shows, reviewed against the app

The Pane file in Paper, the design tool the user works in, is the design source: it draws every state the app shows, light and dark, and is checked against the app, so neither drifts unseen. It first documented the Electron app's design, which the native UI reproduces (ADR 0034). New UI is designed there first (Quit Pane?, the extensions menu, the extension prompts, the dmg's window: ADRs 0053, 0054, 0056, 0046), and a change made in the app is carried to every board that shows it. Where the two differ, Paper wins, unless Paper is the one out of date.

The Browsing page draws every state the window can show (the user's request), each light and dark, numbered after 04.6. The states come from the code (`RootView`'s overlays, `DialogPresenter`'s requests, `PageContextMenu`, `SadTabView`, `ExclusiveAccessHint`, and `UpdatePill`), in the app's own texts: the address focused; the extensions menu and an extension's more menu; adding and removing an extension; an extension's popup; a pinned action's right-click menu; the page's context menu; the JavaScript alert, confirm, and prompt; Leave site; form resubmission; a permission request; sign-in; opening an external app; an unresponsive page; a crashed page; page fullscreen with its Esc hint; docked DevTools; and the update pill downloading. They derive from 04.7 Page loaded. Only the file chooser, the system's own panel, is left out.

What macOS draws, Paper draws as macOS does. The page's sheets were copied from a real `NSAlert` sheet, captured in a test app's own window: 260 points wide, the title in bold, and capsule buttons, Cancel at #DADADA and the default at #3478F6. Those boards, 04.15 to 04.23, are what the app's own dialogs draw now (ADR 0080). The right-click menu must be the Mac's own (the user's word), as the app's always was: SwiftUI's `.contextMenu` is an `NSMenu`. 04.13 drew a flat card; it now copies the `NSMenu` in the user's Aside screenshot, measured at 2×: 286 points wide, radius 13, with 5 points above and below; 24-point rows, the text 16 points in; 11-point separators, their line 16 points in from each side; a half-point outline with a 1-point light rim inside it; and glass, a blur, over the page. Its text stays Inter, since SF Pro is not available to Paper.

Reviews keep the two together. Rendering each state from the app beside Paper's (the user's request) found Paper stale, and Paper was fixed: the toolbar's puzzle, missing in 03.4, 04.2 to 04.5, and the cover; Settings (06.1), with the Electron app's Browser section and version 0.6.0, and without the profile select, Update all, or each card's Pinned and Enabled switches; 03.9's more menu, still native; and the theme principle, which named Electron's `nativeTheme` there and in `DESIGN.md`. The app's profile sheet, spacing its fields 3 to 6 points wider than Paper's, is the app drifting from the design, and that pass left it as found. A review of the whole file (the user's request) put its pages in number order, the app's screens after the divider, gave every flow page its header, kept every page on its grid, and fixed six boards out of date. After the review fixes, checking every board against the app settled three older differences on Paper's side: fields start their text 13 points in, as CSS's `px-3` inside a 1-pixel border does (SwiftUI's border sat over the padding, so the app's started at 12); the Extensions page's search is 13 points, on Pane's scale, not 14; and a disabled extension dims its icon with its text, to 60%.

A review of every board against the code on 2026-10-29 (the user's request) found the two apart on both sides.

Paper was stale in most:

- **Foundations still described the Electron app:** the panel's and the find bar's shadows, a 60% dialog scrim, a 220-point sidebar, a 480-point Settings column, radius and type rows the app no longer uses, and an updates principle from before Pane installed its own updates.
- **States only the app shows were missing:** the profile's and the tab's right-click menus and the delete alert, Collapse all disabled, hover and pressed states, the PIN flow's steps, the lock screen's and Settings' update states, the Extensions page's empty states, an update ready to restart (04.28), and Reload site? (04.29).

The app had drifted from details Paper drew first:

- **Borders drawn over their padding:** the profile chip, the switcher, the extensions menus and popups, pop-up buttons, Settings rows, extension cards, the proxy box, and the address. Their content is now 1 point further in, as CSS's border-box puts it.
- **Text without its line box:** Settings, the extension cards and prompts, Quit Pane?, the lock screen, the sad tab, the full-screen hint, and App icon.
- **Form labels sat 5 points low:** they are now 11 on an 11-point line, 8 above their field.
- **The focus ring was drawn as the border:** it now sits outside it.
- **The add-extension dialog wore Quit Pane?'s surface:** it now has Dia's own.
- **Small values:** the sidebar header's inset was 10 and is now 8; the update pill's text was 13 and is now 12, as the sidebar's (ADR 0035); and "Macos" is now "macOS".

Two values were settled on the dark side:

- **Popover shadows:** 40% on dark, as Paper's dark boards drew them, and 14% on light.
- **The address's rule:** #858587 on light and #606068 on dark.

What macOS draws stays as macOS draws it: the system checkbox, a system alert's centered text, and a context menu's destructive item in the menu's normal color. The extension right-click menu's missing items wait on the engine (ADR 0054).
