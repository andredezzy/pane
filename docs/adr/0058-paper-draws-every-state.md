# Every design is drawn in Paper and approved before it is built, and Paper draws every state the app shows, reviewed against the app

> **On the Views line** (`docs/plans/views-migration.md`, What Chrome asks for): the page's right-click menu is Chrome's own `RenderViewContextMenu`, still the Mac's NSMenu, as the user approved on Paper 10.4 and 10.4.1; 04.14 and 04.14.1 draw `PageContextMenu`'s Electron order and are to follow it. While a screen reader runs, Chrome adds Google's image descriptions to every menu (a submenu, or a check item once on, its consent Chrome's own dialog), which no board draws: Pane takes it off as it takes off Google's spelling service, so a VoiceOver user's menu is everyone's, as the Swift's was (K4, 2026-11-12; `context-menu` checks it with Chrome's screen reader mode on).

> **On the Views line** (`docs/plans/views-migration.md`, X4 and X5): the Extensions page, its Details, and its Keyboard shortcuts dialog are Chrome's own pages in the profile's tab, as 10.1 to 10.3 draw them; 09 · Extensions' boards draw the page that went.

The Pane file in Paper, the design tool the user works in, is the design source: it draws every state the app shows, light and dark, and is checked against the app, so neither drifts unseen. It first documented the Electron app's design, which the native UI reproduces (ADR 0034). Every design, any change to what Pane shows, is drawn there first, as a new board or a change to the board it touches, and shown to the user; it is implemented only once the user has approved it in Paper, and then matches the board (`AGENTS.md` › Design). Quit Pane?, the extensions menu, the extension prompts, and the dmg's window were designed so (ADRs 0053, 0054, 0056, 0046). A change made in the app is carried to every board that shows it. Where the two differ, Paper wins, unless Paper is the one out of date.

The Browsing page draws every state the window can show (the user's request), each light and dark, numbered after 04.6. The states come from the code (`RootView`'s overlays, `DialogPresenter`'s requests, `PageContextMenu`, `SadTabView`, `ExclusiveAccessHint`, and `UpdatePill`), in the app's own texts: the address focused; the extensions menu and an extension's more menu; adding and removing an extension; an extension's popup; a pinned action's right-click menu; the page's context menu; the JavaScript alert, confirm, and prompt; Leave site; form resubmission; a permission request; sign-in; opening an external app; an unresponsive page; a crashed page; page fullscreen with its Esc hint; docked DevTools; and the update pill downloading. They derive from 04.7 Page loaded. Only the file chooser, the system's own panel, is left out.

What macOS draws, Paper draws as macOS does. The page's sheets were copied from a real `NSAlert` sheet, captured in a test app's own window: 260 points wide, the title in bold, and capsule buttons, Cancel at #DADADA and the default at #3478F6. Those boards, 04.15 to 04.23, are what the app's own dialogs draw now (ADR 0021). The right-click menu must be the Mac's own (the user's word), as the app's always was: the page's is an `NSMenu` (`PageContextMenu`), and the sidebar's SwiftUI `.contextMenu` is one too. 04.13 drew a flat card; it now copies the `NSMenu` in the user's Aside screenshot, measured at 2×: 286 points wide, radius 13, with 5 points above and below; 24-point rows, the text 16 points in; 11-point separators, their line 16 points in from each side; a half-point outline with a 1-point light rim inside it; and glass, a blur, over the page. Its text stays Inter, since SF Pro is not available to Paper.

Reviews keep the two together, each at the user's request: rendering each state from the app beside Paper's, the whole file (its pages in number order, the app's screens after the divider, every flow page with its header, on its grid), and every board against the code (2026-10-29). Most of what they found was Paper stale, and Paper was fixed: foundations that still described the Electron app, and states only the app showed.

What they settled on Paper's side, where the app had drifted from details it drew first:

- **Fields start their text 13 points in,** as CSS's `px-3` inside a 1-pixel border does; SwiftUI's border sat over the padding, so the app's started at 12.
- **Borders don't sit over their padding:** the profile chip, the switcher, the extensions menus and popups, pop-up buttons, Settings rows, extension cards, the proxy box, and the address have their content 1 point further in, as CSS's border-box puts it.
- **Text has its line box:** Settings, the extension cards and prompts, Quit Pane?, the lock screen, the sad tab, the full-screen hint, and App icon.
- **Form labels** are 11 on an 11-point line, 8 above their field; they sat 5 points low.
- **The focus ring** sits outside the border, not as the border.
- **The add-extension dialog** has Dia's own surface, not Quit Pane?'s.
- **Small values:** the Extensions page's search is 13 points, on Pane's scale, not 14; a disabled extension dims its icon with its text, to 60%; the sidebar header's inset is 8; the update pill's text is 12, as the sidebar's (ADR 0060); and "Macos" is "macOS".

Two values were settled on the dark side:

- **Popover shadows:** 40% on dark, as Paper's dark boards drew them, and 14% on light.
- **The address's rule:** #858587 on light and #606068 on dark.

What macOS draws stays as macOS draws it: the system checkbox, a system alert's centered text, and a context menu's destructive item in the menu's normal color. The extension right-click menu's missing items, which waited on the engine (ADR 0054), are built: the extension's own items and its site access (ADR 0096).
