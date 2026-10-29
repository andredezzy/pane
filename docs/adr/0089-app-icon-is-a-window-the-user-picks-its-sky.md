# Pane's icon is a window, and the user picks its sky

Pane's icon is a real window: a white enamel frame, two panes, and a mullion, with the sky behind the glass. It was rendered in Magnific with nine skies, and the user kept all nine and asked that Pane's users pick one (Paper: 02.12). The nine are Day, a clear blue sky with no sun, Clouds, Morning, Dusk, Sunset, Twilight, Afterglow, Night, and Moonrise. The user also asked that the icon follow light and dark. Settings › Appearance › App icon offers Automatic, the default, then the nine. Automatic shows Day while the app's appearance is light and Night while it is dark: the theme sets that appearance (ADR 0001), and under system the Mac does.

The choices are tiles, as macOS lays out its own icon styles, because nine photographs cannot be told apart at a menu item's 16 points. Each icon is 56 points over its name. The chosen one is ringed as the profile sheet's swatch is, 2 to 4 points out from the icon's body, on the system's corner (continuous, 0.257 of the side, measured on macOS 27). Choosing is instant, and hover fades (DESIGN.md).

Every sky sits in the one frame: Day's, whitened. The user asked for whiter frames of the same measurements, and the renders' frames differed in tone, thickness, and opening, from a few pixels to a lilac or golden cast. So each render gives only its glass. Day's frame keeps its shading, its luminance lifted 45% toward white and its colour neutral. Each sky's panes are cut 20 pixels in from their outer sides, past the rounded corners' frame, and 3 from the mullion, so what sits by it stays (Sunset's sun, Moonrise's moon). They are stretched into Day's panes.

The bundle's icon is an Icon Composer document, `app/Pane/AppIcon.icon`: Day, with Night as its dark appearance. Under the Mac's dark icon style (macOS 26 and later), Finder, Launchpad, and the Dock while Pane is closed show Night. Each render becomes a full-bleed layer whose frame edge sits 12 points past the canvas, so the system's squircle mask cuts inside the frame. macOS 26 and 27 shrink an icon that does not fill their squircle onto a grey plate. While Pane runs, `DockIcon` sets `NSApp.applicationIconImage` to the chosen sky, and under Automatic it follows `NSApp.effectiveAppearance`. Those images, `AppIcon<Sky>`, are macOS's own drawing of an app whose icon is that sky's window alone, so the running icon matches the bundle's to the pixel. `app/icon/make-icons.swift` makes all of them from `app/icon/renders`. It also makes the document icon (`Icon`) and the README's logo, Day on a light page and Night on a dark one.

What was rejected:

- **An alternate icon API:** AppKit has none (iOS has `setAlternateIconName`).
- **A custom icon on the bundle** (`NSWorkspace.setIcon`): it writes into the signed app, and every update loses it.
- **An asset catalog icon set with transparent corners:** macOS 26 and later jail it on the grey plate.
- **Runtime images exported with `ictool`:** they came out 2.8% brighter than the system's drawing of the same icon, a jump the Dock would show at launch.
- **A pop-up menu of the skies:** its thumbnails were too small to compare.
- **Each render's own frame:** no two matched in tone or measure, and asking Magnific again matches none either.
- **More skies in Automatic:** the user asked for light and dark.
- **The skies the user turned down:** Day with the sun (Day lost it), a room reflected in a midday sky, a crescent moon with a blue glow on the frame, a moon behind clouds, a shooting star, and two oversized full moons.
- **A Dock tile plug-in for now** (`NSDockTilePlugIn`): it would keep the chosen sky in the Dock while Pane is closed. It waits until the Dock can be checked on screen.

How it is checked:

- **Tests:** PaneKit's tests cover the setting's persistence and Automatic's rule.
- **The `app-icon` scenario:** it runs each theme and setting against the window the Dock shows, through the automation snapshot. It also renders Settings' tiles in each theme.
- **Other scenarios:** `theme`, `smoke`, `shortcuts`, `security`, `updates`, and `profile-sheet` pass on that build.
- **The pixel comparisons:** `AppIconDay` against NSWorkspace's drawing of the packaged app shows no pixel apart. Day's icon against Sunset's differs by more than 8 levels only inside the glass, so the frames match.
