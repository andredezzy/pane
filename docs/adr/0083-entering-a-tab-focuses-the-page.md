# Entering a tab gives its page the keys

The tab entered gives its page the keys, as Chrome's tab switch does, whether it is entered by a click in the sidebar, by the switcher, as a slept tab wakes (ADR 0041), or coming back from Settings or Extensions. Before, entering a tab selected the address bar and focused it (the user's report): the selected page's view leaves the window with the keys when another tab comes forward, and AppKit gave them to the next view that takes them, the address field, which selects its text on focus. `focusEnteredTab` in `app/Pane/Browser/BrowserController.swift` now hands them to the page.

A new tab with no page still focuses its address field, to type in. It is marked before its page shows (`focusesAddress`), which can come a turn later, as its profile opens, so the page does not take the keys from it.

`tab-focus` clicks the page, then another tab in the sidebar. The address field had the keys before; the page has them now, and after the switcher and a trip to Settings too. A new tab focuses its address field.
