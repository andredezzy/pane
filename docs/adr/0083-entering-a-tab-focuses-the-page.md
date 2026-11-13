# Entering a tab gives its page the keys

The tab entered gives its page the keys, as Chrome's tab switch does, whether it is entered by a click in the sidebar, by the switcher, as a slept tab wakes (ADR 0041), or coming back from Settings: the window gives them to the page as it shows it (`Window::ShowTab`, ADR 0011). A sad tab takes them in its dead page's place and gives them back as the page returns (ADR 0027). The rule came from the user's report against the Swift app, where entering a tab selected the address bar and focused it: the selected page's view left the window with the keys when another tab came forward, and AppKit gave them to the next view that took them, the address field, which selects its text on focus.

A new tab with no page still focuses its address field, to type in. It is marked before its page shows (`UiApp`'s `focuses_address_`), which can come a turn later, as its profile opens, so the page does not take the keys from it.

`tab-focus` clicks the page, then another tab in the sidebar. The address field had the keys before; the page has them now, and after the switcher and a trip to Settings too. A new tab focuses its address field.
