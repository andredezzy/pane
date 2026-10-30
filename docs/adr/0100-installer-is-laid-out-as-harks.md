# The installer is laid out as Hark's

> Replaces the window of [ADR 0046](0046-the-dmg.md); its build (dmgbuild, UDZO, the window's place) stays.

The dmg's window copies Hark's installer, at the user's request ("identical"), and carries Pane's own icon, wordmark, and arrow. A hazy sky runs from blue at the top to pale gray at the bottom, its colours sampled from the user's screenshot of Hark's. "Install **Pane**" sits above in the wordmark's type, Funnel Display Regular with the name in Medium (ADR 0099), 32 points. "Drag the app onto the Applications folder" sits under it in the system font, 18 points, as Finder's labels are. A hand-drawn arrow runs between Pane and the Applications link. The icons are 128 points, 215 and 518 points in and 284 down, with 16-point labels. The window is 720 × 560 points under Finder's title bar, and the volume, so the title, is "Pane Installer", as Hark's is "Hark Installer". The updater mounts the dmg at a path of its own (`UpdateInstaller`), so the name is the window's alone.

It was designed in Paper first (08 · Installer › 08.1 Installer window), the rule `AGENTS.md` now states. The arrow is A10 of ten drawn in the whole window (08.2 Arrow options). Dia's window, 08.1 until now, is 08.3 Installer window, retired.

`app/dmg/make-background.swift` draws it into `app/dmg/background.tiff`, at 1x and 2x, tagged sRGB. The sky mixes its stops in Oklab, as Paper does, and its haze is Paper's eight soft ellipses. The headline is drawn from `app/dmg/fonts/FunnelDisplay[wght].ttf`, registered for the script alone; it stops if the font does not load, so no fallback is ever drawn. The dmg carries the image, not the font. The image runs 40 points taller than the window, the sky's last colour alone, for a shorter title bar.

What was rejected:

- **A green cartoon mountain behind the icons:** the user's first request, drawn in code and then generated in Paper; the user turned down both and asked for Hark's window as it is.
- **A photograph of a blurred sky:** Paper's gradients come within 3 levels of Hark's sampled colours, with no image to source or license.
- **Funnel Display for the subtitle:** the user's call; it stays in the system font.
- **The other nine arrows:** Hark's own, a straight one, a chevron, three chevrons, a dotted path, an arc, a filled one, one in a disc, and a long hairline (08.2).
- **Keeping "Pane" as the volume name:** Hark's window says "Hark Installer"; nothing reads the name.

How it is checked:

- **Finder:** a test dmg from `dist.sh` (`PANE_SKIP_PACKAGE=1 PANE_DMG_OUT=...`), mounted and opened as a user's would be, its window captured alone, against Paper's 08.1 at 2x: every sampled point of the sky is within 3 levels in sRGB. Finder draws the labels 20.5 points higher than the board first did; the board now draws them where Finder does.
- **Dark mode:** with the Mac set to dark, Finder darkens the title bar alone. It draws the labels dark on the light sky, as in light mode, so they read the same. A dmg has one background for both, as Hark's does.
- **The font:** `make-background.swift` checks that Funnel Display loaded before it draws.
