# Every Paper board against the Views build

Each board of the Pane file in Paper, 218 on 11 pages, read through Paper's local MCP without opening or changing anything, and each state's board exported at 2× (PNG) against the Views build's render of that state, at 2×, in the same theme, from the candidate's runs on 2026-11-11 (`docs/plans/views-migration.md`, K2b): the final tree's build, light and dark, through the runner on throwaway data, in the background. Where the Swift release's render of the same state exists (`evidence/views-migration/renders/swift*/`, made from 0.19.2's code at B2), the share of the window's pixels whose red, green, or blue differs from it by more than 2/255 is given, with both images drawn into sRGB first (colors in one space). Renders leave the page out, and the Swift's showed its page's area white or black where Views shows the panel's own color, so the page's area is taken as one color in both before the count: what remains around an overlay over that area is its shadow, falling on a different color.

How to read it:

- **Against Paper,** each pair was laid side by side with its difference (`evidence/views-migration/k2b-paper/`, the sheets in K2b's notes) and read for each surface's place, size, color, and state. Paper's boards draw a sample session (profiles named Personal, Work, Acme Corp; an invoices page) and the renders the scenarios' own, so the sidebar's rows and the page differ by design: what is compared is the surface the board is about. Colors were also sampled from both at the same points, where a board and its render share a state (the dark alert's card, `#2B2B2D` on both; Quit Pane?'s, `#1C1C1F`; the page's area, `#1C1C1F`; the scrimmed sidebar, `#050507` and `#050506`).
- **Against the Swift's,** the numbers. Text is ADR 0060's: the Views UI's text 10 to 18% heavier than the Swift's (the user's call, 2026-11-09), the system font's kerning between a digit and punctuation lost (the address's and a version's digits), and no tracking or tabular digits (Paper 10.6): most of each number is that, the rest one of the lines below.
- **What remains** names, for each board, what isn't the board's: the text above; the user's approved changes (Group off the sheet, N1; Chrome's extensions pages, D3, and its page menu, 10.4; Pane's popover kept, 10.5; a Lock dismissing Quit Pane?, K2a); platform differences (Views' overlay scroller, the amber blended in sRGB); and what a scenario sets differently from the Swift's run (its extension, its pointer).

Boards no render answers: the system's menus (the extension's right-click menu, the page's, the zone menu, the menu bar), which macOS draws and whose items the scenarios read; Pane's former extensions pages (09), which D3 replaced; the dmg's window (08), which the migration didn't touch; and the cover, brand, foundations, and components, which are not states.

**Boards out of date, for the user (Paper is read only here):** 04.14 and 04.14.1 draw the Electron order of the page's menu, which ADR 0058's note says is to follow 10.4 and 10.4.1; 09.1 to 09.16.1, 33 boards, draw the page D3 replaced (memory: redraw superseded boards).

## Every board

### 00 · Cover (1 board)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| Cover | — | — | the file's cover, no state |

### 01 · Brand (18 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 01 · Brand — Header | — | — | the page's header |
| 01.1 Logo | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.2 App icon, more lights | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.3 Logo iterations | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.4 Logo directions | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.5 Logo directions, round 2 | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.6 References · App icons | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.7 References · Faces | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.8 References · One soft form | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.9 References · Real objects | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.10 References · Competitors' colours | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.11 Wordmark | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.12 Wordmark type, round 1 | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.13 Wordmark type, round 2 | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.14 Wordmark type, serifs | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.15 App icon, cartoon skies — proposal | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.16 Night and Sunset — subtler options | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |
| 01.17 Twilight — star options | — | — | the brand's work (logo, wordmark, references): no state; the App icon's renders are packaging/icon's |

### 02 · Foundations (8 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 02 · Foundations — Header | — | — | the page's header |
| 02.1 Color · Semantic | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.2 Color · Profile & status | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.3 Type & shape · Typography | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.4 Type & shape · Radii | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.5 Type & shape · Elevation | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.6 Guidelines · Layout & spacing | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |
| 02.7 Guidelines · Principles | — | — | foundations: every color of Paper's tokens is the palette's or the model's (pane/ui/design/palette.cc, pane/model/profile.cc; palette_unittest), radii and curves the design's (squircle_unittest, motion_unittest); text: ADR 0060's notes |

### 03 · Components (12 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 03 · Components — Header | — | — | the page's header |
| 03.1 Sidebar · ProfileHeader | — | — | the page's header |
| 03.2 Sidebar · TabItem | — | — | components, each drawn in the surfaces' renders below |
| 03.3 Sidebar · Footer | — | — | components, each drawn in the surfaces' renders below |
| 03.4 Browser · Toolbar | — | — | components, each drawn in the surfaces' renders below |
| 03.5 Browser · Overlays | — | — | components, each drawn in the surfaces' renders below |
| 03.6 Browser · Lock screen | — | — | components, each drawn in the surfaces' renders below |
| 03.7 UI kit · Buttons | — | — | components, each drawn in the surfaces' renders below |
| 03.8 UI kit · Form controls | — | — | components, each drawn in the surfaces' renders below |
| 03.9 Browser · Extensions | — | — | components, each drawn in the surfaces' renders below |
| 03.10 Browser · Extension popups | — | — | components, each drawn in the surfaces' renders below |
| 03.11 Sidebar · Scroll, resize & reorder | — | — | components, each drawn in the surfaces' renders below |

### 04 · Browsing (67 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 04 · Browsing — Header | — | — | the page's header |
| 04.1 Empty state — Light | `empty-state.png` | 0.14% | the empty state as the board; remains: ADR 0060's text |
| 04.1 Empty state — Dark | `empty-state.png` | 0.13% | the empty state as the board; remains: ADR 0060's text |
| 04.2 Active tab — Light | `surfaces-window-two-profiles.png` | 0.45% | the sidebar's rows, the selected tab, and the toolbar as the board; the board's own sample profiles and page are not the test's; remains: ADR 0060's text |
| 04.2 Active tab — Dark | `surfaces-window-two-profiles.png` | 1.03% | the sidebar's rows, the selected tab, and the toolbar as the board; the board's own sample profiles and page are not the test's; remains: ADR 0060's text |
| 04.3 Find in page — Light | `surfaces-find-matches.png` | 0.45% | the find bar, 357 × 54 at the toolbar's corner, as the board; remains: ADR 0060's text, the count's digits in cells of one width (Paper 10.6) |
| 04.3 Find in page — Dark | `surfaces-find-matches.png` | 1.93% | the find bar, 357 × 54 at the toolbar's corner, as the board; remains: ADR 0060's text, the count's digits in cells of one width (Paper 10.6) |
| 04.4 Tab switcher — Light | `surfaces-switcher-open.png` | 0.63% | the switcher's card, rows, and scrim as the board; remains: ADR 0060's text |
| 04.4 Tab switcher — Dark | `surfaces-switcher-open.png` | 3.10% | the switcher's card, rows, and scrim as the board; remains: ADR 0060's text |
| 04.4.1 Tab switcher · One tab — Light | `switcher-one-tab.png` | no Swift render | one tab's switcher as the board; the row's tint follows the profile's color |
| 04.4.1 Tab switcher · One tab — Dark | `switcher-one-tab.png` | no Swift render | one tab's switcher as the board; the row's tint follows the profile's color |
| 04.5 Quit Pane — Light | `surfaces-quit-confirm.png` | 1.08% | Quit Pane?'s card, icon tile, and keys as the board, its shadow a rect rounded at 0.57 of the radius (ADR 0123), within 2/255; remains: ADR 0060's text, tracking (Paper 10.6) |
| 04.5 Quit Pane — Dark | `surfaces-quit-confirm.png` | 10.01% | Quit Pane?'s card, icon tile, and keys as the board, its shadow a rect rounded at 0.57 of the radius (ADR 0123), within 2/255; remains: ADR 0060's text, tracking (Paper 10.6) |
| 04.6 Extensions menu — Light | `menu-light.png` | 2.23% | the menu, Aside's (ADR 0054), its place and rows as the board; the scenario's local probe extension where the Swift's run used the Web Store's Dark Reader; remains: ADR 0060's text |
| 04.6 Extensions menu — Dark | `menu-dark.png` | 1.98% | the menu, Aside's (ADR 0054), its place and rows as the board; the scenario's local probe extension where the Swift's run used the Web Store's Dark Reader; remains: ADR 0060's text |
| 04.7 Page loaded — Light | `window-page.png` | no Swift render | the window with a page as the board (renders leave the page out) |
| 04.7 Page loaded — Dark | `window-page.png` | no Swift render | the window with a page as the board (renders leave the page out) |
| 04.8 Address focused — Light | `surfaces-toolbar-address-focused.png` | 0.51% | the address focused, its ring and selection, as the board; remains: ADR 0060's text and the digits' kerning (ADR 0060's note) |
| 04.8 Address focused — Dark | `surfaces-toolbar-address-focused.png` | 1.07% | the address focused, its ring and selection, as the board; remains: ADR 0060's text and the digits' kerning (ADR 0060's note) |
| 04.9 Extension more menu — Light | `more-menu.png` | 3.52% | the more menu beside the row, as the board; remains: ADR 0060's text |
| 04.9 Extension more menu — Dark | `more-menu.png` | 3.02% | the more menu beside the row, as the board; remains: ADR 0060's text |
| 04.10 Install extension — Light | `install-popup-light.png` | 3.49% | Dia's 424-point add prompt (ADR 0056) as the board, over the content; remains: ADR 0060's text, tracking (Paper 10.6) |
| 04.10 Install extension — Dark | `install-popup-dark.png` | 3.47% | Dia's 424-point add prompt (ADR 0056) as the board, over the content; remains: ADR 0060's text, tracking (Paper 10.6) |
| 04.11 Remove extension — Light | `remove-popup-light.png` | 33.85% | the remove prompt under its icon, as the board; remains: ADR 0060's text |
| 04.11 Remove extension — Dark | `remove-popup-dark.png` | 2.16% | the remove prompt under its icon, as the board; remains: ADR 0060's text |
| 04.12 Extension popup — Light | `popup-light.png` | no Swift render | Pane's popover kept (the user's answer on Paper 10.5): the render is the popup alone |
| 04.12 Extension popup — Dark | `popup-dark.png` | no Swift render | Pane's popover kept (the user's answer on Paper 10.5): the render is the popup alone |
| 04.13 Extension right-click menu — Light | — | — | the system's menu (NSMenu, ADR 0054): items checked by views-extensions and extension-context-menus; no render |
| 04.13 Extension right-click menu — Dark | — | — | the system's menu (NSMenu, ADR 0054): items checked by views-extensions and extension-context-menus; no render |
| 04.13.1 Extension right-click menu · Site access — Light | — | — | the system's menu: its site-access items checked by extension-context-menus; no render |
| 04.13.1 Extension right-click menu · Site access — Dark | — | — | the system's menu: its site-access items checked by extension-context-menus; no render |
| 04.13.2 Extension right-click menu · Can’t access — Light | — | — | the system's menu: checked by extension-context-menus; no render |
| 04.13.2 Extension right-click menu · Can’t access — Dark | — | — | the system's menu: checked by extension-context-menus; no render |
| 04.14 Page context menu — Light | — | — | Chrome's own menu since 10.4 (U2, approved): ADR 0058's note says 04.14 is to follow 10.4 |
| 04.14 Page context menu — Dark | — | — | Chrome's own menu since 10.4 (U2, approved): ADR 0058's note says 04.14 is to follow 10.4 |
| 04.14.1 Page context menu · Extension items — Light | — | — | Chrome's own menu since 10.4.1 (approved): to follow 10.4.1 |
| 04.14.1 Page context menu · Extension items — Dark | — | — | Chrome's own menu since 10.4.1 (approved): to follow 10.4.1 |
| 04.15 JavaScript alert — Light | `surfaces-dialog-alert.png` | 0.54% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.15 JavaScript alert — Dark | `surfaces-dialog-alert.png` | 6.76% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.16 JavaScript confirm — Light | `surfaces-dialog-confirm.png` | 0.56% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.16 JavaScript confirm — Dark | `surfaces-dialog-confirm.png` | 6.82% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.17 JavaScript prompt — Light | `surfaces-dialog-prompt.png` | 0.60% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.17 JavaScript prompt — Dark | `surfaces-dialog-prompt.png` | 7.51% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.18 Leave site — Light | `surfaces-dialog-leave-site.png` | 0.67% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.18 Leave site — Dark | `surfaces-dialog-leave-site.png` | 7.39% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.19 Form resubmission — Light | `surfaces-dialog-form-resubmission.png` | 0.89% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.19 Form resubmission — Dark | `surfaces-dialog-form-resubmission.png` | 0.89% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.20 Permission request — Light | `surfaces-dialog-permission.png` | 0.68% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.20 Permission request — Dark | `surfaces-dialog-permission.png` | 8.06% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.21 Sign in — Light | `surfaces-dialog-sign-in.png` | 0.69% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.21 Sign in — Dark | `surfaces-dialog-sign-in.png` | 8.47% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.22 Open external app — Light | `surfaces-dialog-external-app.png` | 0.70% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.22 Open external app — Dark | `surfaces-dialog-external-app.png` | 7.40% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.23 Page unresponsive — Light | `surfaces-dialog-hung-page.png` | 0.69% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.23 Page unresponsive — Dark | `surfaces-dialog-hung-page.png` | 7.33% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.24 Page crashed — Light | `surfaces-sad-tab-crashed.png` | 0.66% | the sad tab's lines and Reload as the board; remains: ADR 0060's text |
| 04.24 Page crashed — Dark | `surfaces-sad-tab-crashed.png` | 0.65% | the sad tab's lines and Reload as the board; remains: ADR 0060's text |
| 04.25 Page fullscreen — Light | `surfaces-fullscreen-hint.png` | no Swift render | the hint's pill as the board; the window itself goes to full screen only with the front (fullscreen, deferred) |
| 04.25 Page fullscreen — Dark | `surfaces-fullscreen-hint.png` | no Swift render | the hint's pill as the board; the window itself goes to full screen only with the front (fullscreen, deferred) |
| 04.26 DevTools docked — Light | `surfaces-devtools-docked.png` | no Swift render | the frame around docked DevTools as the board; DevTools' own page, a page, is not in renders |
| 04.26 DevTools docked — Dark | `surfaces-devtools-docked.png` | no Swift render | the frame around docked DevTools as the board; DevTools' own page, a page, is not in renders |
| 04.27 Update downloading — Light | `pill-downloading.png` | no Swift render | the pill, its dot and bar, as the board; remains: ADR 0060's text |
| 04.27 Update downloading — Dark | `pill-downloading.png` | no Swift render | the pill, its dot and bar, as the board; remains: ADR 0060's text |
| 04.28 Update ready — Light | `pill-downloaded.png` | no Swift render | the pill's Restart to update (updates-ready) as the board; Quit and install where Pane can't replace itself (Paper 06.3.1) |
| 04.28 Update ready — Dark | `pill-downloaded.png` | no Swift render | the pill's Restart to update (updates-ready) as the board; Quit and install where Pane can't replace itself (Paper 06.3.1) |
| 04.29 Reload site — Light | `surfaces-dialog-reload-site.png` | 0.68% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |
| 04.29 Reload site — Dark | `surfaces-dialog-reload-site.png` | 7.39% | the card, its place, and its buttons as the board; in dark the card #2B2B2D as Paper's, where the Swift's let 4% of a white page through (#343436); remains: ADR 0060's text |

### 05 · Profiles (23 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 05 · Profiles — Header | — | — | the page's header |
| 05.1 New profile — Light | `profile-sheet-new-light.png` | 12.23% | the sheet as the board, without Group (the user's answer, N1); remains: ADR 0060's text |
| 05.1 New profile — Dark | `profile-sheet-new-dark.png` | 15.26% | the sheet as the board, without Group (the user's answer, N1); remains: ADR 0060's text |
| 05.2 Edit profile · Proxy — Light | `profile-sheet-edit-connected-light.png` | no Swift render | the edit sheet with its proxy connected, as the board; remains: ADR 0060's text |
| 05.2 Edit profile · Proxy — Dark | `profile-sheet-edit-connected-dark.png` | no Swift render | the edit sheet with its proxy connected, as the board; remains: ADR 0060's text |
| 05.3 Edit profile · History off — Light | `profile-sheet-edit-history-off-light.png` | no Swift render | history off, as the board; remains: ADR 0060's text |
| 05.3 Edit profile · History off — Dark | `profile-sheet-edit-history-off-dark.png` | no Swift render | history off, as the board; remains: ADR 0060's text |
| 05.4 Profile sheet · New — Light | `profile-sheet-new-light.png` | 12.23% | the new profile's sheet as the board, without Group (N1) |
| 05.4 Profile sheet · New — Dark | `profile-sheet-new-dark.png` | 15.26% | the new profile's sheet as the board, without Group (N1) |
| 05.5 Profile sheet · Edit, every state — Light | `profile-sheet-edit-connected-light.png` | no Swift render | every state of the edit sheet (connected, the cross-OS warning, a zone) as the board, the sheet scrolled in the window where the board draws it whole; the warning's 10% amber blended in sRGB, 4 to 5/255 from Core Animation's Display P3 (N1) |
| 05.5 Profile sheet · Edit, every state — Dark | `profile-sheet-edit-connected-dark.png` | no Swift render | every state of the edit sheet (connected, the cross-OS warning, a zone) as the board, the sheet scrolled in the window where the board draws it whole; the warning's 10% amber blended in sRGB, 4 to 5/255 from Core Animation's Display P3 (N1) |
| 05.6 Profile sheet · Spoofing off, test failed — Light | `profile-sheet-edit-refused-light.png` | no Swift render | spoofing off and a refused test as the board; a failed test reads Paper's words (N1's note) |
| 05.6 Profile sheet · Spoofing off, test failed — Dark | `profile-sheet-edit-refused-dark.png` | no Swift render | spoofing off and a refused test as the board; a failed test reads Paper's words (N1's note) |
| 05.7 Profile sheet · Time zone menu — Light | — | — | the system's menu (the zone menu, NSMenu): its items checked by profile-sheet; by real clicks, for the front |
| 05.7 Profile sheet · Time zone menu — Dark | — | — | the system's menu (the zone menu, NSMenu): its items checked by profile-sheet; by real clicks, for the front |
| 05.8 Profile sheet · Proxy host missing — Light | `profile-sheet-host-missing-light.png` | 3.22% | the empty host's red ring (field-problem-red-ring) as the board |
| 05.8 Profile sheet · Proxy host missing — Dark | `profile-sheet-host-missing-dark.png` | 6.32% | the empty host's red ring (field-problem-red-ring) as the board |
| 05.9 Profile sheet · Proxy host unreadable — Light | `profile-sheet-host-unreadable-light.png` | 3.26% | the unreadable host's red ring as the board |
| 05.9 Profile sheet · Proxy host unreadable — Dark | `profile-sheet-host-unreadable-dark.png` | 6.35% | the unreadable host's red ring as the board |
| 05.10 Profile sheet · Name missing — Light | `profile-sheet-name-missing-light.png` | 12.38% | the name's red ring as the board, without Group (N1) |
| 05.10 Profile sheet · Name missing — Dark | `profile-sheet-name-missing-dark.png` | 15.41% | the name's red ring as the board, without Group (N1) |
| 05.11 Profile sheet · Port invalid — Light | `profile-sheet-port-invalid-light.png` | 1.28% | the port's red ring as the board; a port outside 1 to 65535 fails closed for every type (N1's note) |
| 05.11 Profile sheet · Port invalid — Dark | `profile-sheet-port-invalid-dark.png` | 4.37% | the port's red ring as the board; a port outside 1 to 65535 fails closed for every type (N1's note) |

### 06 · Settings (11 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 06 · Settings — Header | — | — | the page's header |
| 06.1 Settings — Light | `surfaces-settings-top.png` | 1.40% | Settings' cards and rows as the board; remains: ADR 0060's text |
| 06.1 Settings — Dark | `surfaces-settings-top.png` | 1.38% | Settings' cards and rows as the board; remains: ADR 0060's text |
| 06.2 Settings · App icon — Light | `surfaces-app-icon-picker-automatic.png` | 2.27% | the App icon picker over Settings as the board; remains: ADR 0060's text |
| 06.2 Settings · App icon — Dark | `surfaces-app-icon-picker-automatic.png` | 2.15% | the App icon picker over Settings as the board; remains: ADR 0060's text |
| 06.3 Settings · States — Light | `surfaces-updates-downloading.png` | 1.69% | the Updates row's states (checking, offered, downloading, downloaded) as the board |
| 06.3 Settings · States — Dark | `surfaces-updates-downloading.png` | 1.61% | the Updates row's states (checking, offered, downloading, downloaded) as the board |
| 06.3.1 Settings · Update installs by hand — Light | `updates-by-hand.png` | no Swift render | Quit and install, and its status line, as the board |
| 06.3.1 Settings · Update installs by hand — Dark | `updates-by-hand.png` | no Swift render | Quit and install, and its status line, as the board |
| 06.4 Settings · Lock — Light | `surfaces-settings-pin-set.png` | 1.65% | the PIN rows and Lock after as the board |
| 06.4 Settings · Lock — Dark | `surfaces-settings-pin-set.png` | 4.02% | the PIN rows and Lock after as the board |

### 07 · Lock screen (19 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 07 · Lock screen — Header | — | — | the page's header |
| 07.1 Unlock — Light | `surfaces-lock-update-available.png` | 0.22% | the PIN pad, dots, and the update's status as the board; the board types 2 of a 6-digit PIN where the render has none of 4 (surfaces-lock-typing has 2) |
| 07.1 Unlock — Dark | `surfaces-lock-update-available.png` | 0.21% | the PIN pad, dots, and the update's status as the board; the board types 2 of a 6-digit PIN where the render has none of 4 (surfaces-lock-typing has 2) |
| 07.2 Set up PIN — Light | `surfaces-pin-setup-empty.png` | 0.27% | set up, row for row the board |
| 07.2 Set up PIN — Dark | `surfaces-pin-setup-empty.png` | 0.26% | set up, row for row the board |
| 07.3 Confirm PIN — Light | `surfaces-pin-setup-confirm.png` | 0.21% | confirm, row for row the board |
| 07.3 Confirm PIN — Dark | `surfaces-pin-setup-confirm.png` | 0.20% | confirm, row for row the board |
| 07.4 Change PIN · current PIN — Light | `surfaces-pin-change-current.png` | 0.22% | change, current PIN, row for row the board |
| 07.4 Change PIN · current PIN — Dark | `surfaces-pin-change-current.png` | 0.21% | change, current PIN, row for row the board |
| 07.5 Encrypting data — Light | `lock-data-move.png` | no Swift render | the data's move (07.5), its line and bar, as the board |
| 07.5 Encrypting data — Dark | `lock-data-move.png` | no Swift render | the data's move (07.5), its line and bar, as the board |
| 07.6 Unreadable key — Light | `lock-unreadable-key.png` | 0.29% | the unreadable key's red lines as the board |
| 07.6 Unreadable key — Dark | `lock-unreadable-key.png` | 0.28% | the unreadable key's red lines as the board |
| 07.7 Keychain denied — Light | `lock-keychain-denied.png` | no Swift render | the Keychain's red lines as the board |
| 07.7 Keychain denied — Dark | `lock-keychain-denied.png` | no Swift render | the Keychain's red lines as the board |
| 07.8 Data won’t open — Light | `data-volume-07.8-light.png` | 0.29% | the volume's red lines as the board (data-volume, on the package) |
| 07.8 Data won’t open — Dark | `data-volume-07.8-dark.png` | 0.28% | the volume's red lines as the board (data-volume, on the package) |
| 07.9 Lock Pane · menu — Light | — | — | the menu bar's Pane menu (NSMenu): items, keys, and Lock Pane dimmed without a PIN checked by views-menus |
| 07.9 Lock Pane · menu — Dark | — | — | the menu bar's Pane menu (NSMenu): items, keys, and Lock Pane dimmed without a PIN checked by views-menus |

### 08 · Installer (4 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 08 · Installer — Header | — | — | the page's header |
| 08.1 Installer window | — | — | the dmg's window: background.tiff and VolumeIcon.icns byte for byte 0.19.2's, dist.sh's window the same but for D2's paths; not Pane's UI, no render |
| 08.2 Arrow options | — | — | the dmg's window: background.tiff and VolumeIcon.icns byte for byte 0.19.2's, dist.sh's window the same but for D2's paths; not Pane's UI, no render |
| 08.3 Installer window, retired | — | — | the dmg's window: background.tiff and VolumeIcon.icns byte for byte 0.19.2's, dist.sh's window the same but for D2's paths; not Pane's UI, no render |

### 09 · Extensions (35 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 09 · Extensions — Header | — | — | the page's header |
| 09 · Extensions — Proposals header | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.1 Extensions — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.1 Extensions — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.2 Search — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.2 Search — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.3 No extensions — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.3 No extensions — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.4 Remove extension — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.4 Remove extension — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.5 No profiles — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.5 No profiles — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.6 No results — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.6 No results — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.7 No icon — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.7 No icon — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.8 Keyboard shortcuts · Entry — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.8 Keyboard shortcuts · Entry — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.9 Keyboard shortcuts — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.9 Keyboard shortcuts — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.10 Keyboard shortcuts · Recording — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.10 Keyboard shortcuts · Recording — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.11 Keyboard shortcuts · Pane’s own key — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.11 Keyboard shortcuts · Pane’s own key — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.12 Keyboard shortcuts · Field states — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.12 Keyboard shortcuts · Field states — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.13 Extension details — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.13 Extension details — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.14 Extension details · Site access — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.14 Extension details · Site access — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.15 Extension card · Version in Details — Before / After | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.16 Extension details · Site list — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.16 Extension details · Site list — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.16.1 Add a site — Light | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |
| 09.16.1 Add a site — Dark | — | — | Pane's own extensions page, which D3 replaced with Chrome's (10.1 to 10.3); ADR 0058's note says so; the board draws a page that went |

### 10 · Views migration (20 boards)

| Board | Views render | Views vs the Swift's | What remains |
|---|---|---:|---|
| 10 · Views migration — Proposals header | — | — | the page's header |
| 10.1 Extensions page · chrome://extensions — Light | `page-list-light.png` | no Swift render | Chrome's extensions page in the profile's tab, as approved (D3) |
| 10.1 Note · What differs — Light | — | — | a note beside the board, no state |
| 10.1 Extensions page · chrome://extensions — Dark | `page-list-dark.png` | no Swift render | Chrome's extensions page in the profile's tab, as approved (D3) |
| 10.1 Note · What differs — Dark | — | — | a note beside the board, no state |
| 10.2 Extension details · chrome://extensions/?id= — Light | `page-details-light.png` | no Swift render | Chrome's details page in the tab, as approved (D3) |
| 10.2 Note · What differs — Light | — | — | a note beside the board, no state |
| 10.2 Extension details · chrome://extensions/?id= — Dark | `page-details-dark.png` | no Swift render | Chrome's details page in the tab, as approved (D3) |
| 10.2 Note · What differs — Dark | — | — | a note beside the board, no state |
| 10.3 Keyboard shortcuts · chrome://extensions/shortcuts — Light | `page-shortcuts-light.png` | no Swift render | Chrome's shortcuts page in the tab, as approved (D3) |
| 10.3 Note · What differs — Light | — | — | a note beside the board, no state |
| 10.3 Keyboard shortcuts · chrome://extensions/shortcuts — Dark | `page-shortcuts-dark.png` | no Swift render | Chrome's shortcuts page in the tab, as approved (D3) |
| 10.3 Note · What differs — Dark | — | — | a note beside the board, no state |
| 10.4 Context menu · Page and link — Light | — | — | Chrome's own page menu (NSMenu): items checked by context-menu and views-browsing |
| 10.4 Context menu · Page and link — Dark | — | — | Chrome's own page menu (NSMenu): items checked by context-menu and views-browsing |
| 10.4.1 Context menu · Selected text and a field — Light | — | — | Chrome's own menu on a selection and a field: checked by context-menu and views-browsing |
| 10.4.1 Context menu · Selected text and a field — Dark | — | — | Chrome's own menu on a selection and a field: checked by context-menu and views-browsing |
| 10.5 Extension popup · NSPopover to ExtensionPopup — Light | `popup-light.png` | no Swift render | Pane's popover kept, the answer on this board |
| 10.5 Extension popup · NSPopover to ExtensionPopup — Dark | `popup-dark.png` | no Swift render | Pane's popover kept, the answer on this board |
| 10.6 · Text without tracking or tabular digits | — | — | the 9 texts drawn without tracking or tabular digits, as the user chose on this board (ADR 0060's note); in the surfaces' renders |
