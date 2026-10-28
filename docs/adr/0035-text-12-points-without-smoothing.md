# UI text is drawn without font smoothing, and the sidebar's and toolbar's is 12 points

The app draws its text without font smoothing, as the Electron app's UI did, and the sidebar's and the toolbar's text is 12 points, the Electron app's size, by the user's call. Smoothing is off through `AppleFontSmoothing` 0 in the defaults the app registers (`app/Pane/main.swift`), the counterpart of the Electron UI's `-webkit-font-smoothing: antialiased`: with smoothing on, SwiftUI text was visibly heavier. Web pages render the same either way, since Chromium draws them.

At 12 points are the tab titles, New tab, New, Settings, the profile names, the sidebar's title, the address, and the profile chip; rendered, a cap is 17 pixels at 2×. Their rows keep Aside's spacing, 32 points on a 36-point pitch with 16-point favicons (ADR 0060). Paper follows: 1,633 texts, the type scale, and the captions.

The size came down in two steps. Taking Aside's sizes had set this text at 14, as Aside's sidebar measured (a 20-pixel cap height at 2×; ADR 0060), and the user found it far too large. That is a point over macOS's own sidebar size, since Finder's, Safari's, and Mail's sidebars and the menu bar are 13, so the text went to 13: a cap of 18 pixels at 2×, against the menu bar's 19 in the user's screenshot. After 13, the user's call was 12.
