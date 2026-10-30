# Chromium's AppleScript dictionary stays

Pane keeps the AppleScript dictionary its engine brings (`scripting.sdef`, `NSAppleScriptEnabled`), unchanged from Chromium's and Aside's. Launchers and clipboard tools read the browser in front this way, `URL of active tab of front window`, and for Pane it answers right: one window, whose active tab, its URL and title, is the tab Pane shows, in whichever profile. Chromium lists a window per NSWindow, from the Browser activated last (`browsercrapplication+applescript.mm`), and the profile Pane shows is that Browser.

What it does not answer for: a window's tabs are the shown profile's open ones, its hidden Browser's, not the sidebar's every tab, so a "search all tabs" tool sees fewer. Executing JavaScript stays off: Chromium allows it only with the "Allow JavaScript from Apple Events" preference, which Pane has no menu for.

The research comparing Pane with Aside and Dia raised it: Dia wrote a dictionary of its own (profiles, tabs, focus), and Pane's, shipped as Chromium's over Pane's own windows, was unverified.

What was rejected:

- **Dropping the dictionary:** the tools that ask for the front tab would lose Pane, which answers them right.
- **A dictionary of Pane's own now:** its tabs and profiles would list the sidebar's, but it is a scripting surface to design and keep; worth it when a tool needs every tab.

How it is checked:

- **The applescript scenario** runs the dictionary's commands in the app against itself, so no Apple Events permission is asked: with each of three tabs in two profiles shown, the front window's active tab is that tab, there is one window, and its tabs are that profile's.
