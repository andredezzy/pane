# The profile sheet holds its settings in cards, and picks a fingerprint's platform by its real logo

The sheet had grown a field at a time from the Electron app's (ADR 0034): a column of switches and fields with no explanation of what each did. Adding Save history (ADR 0119) was the moment to lay it out again. Paper's 05.4 (New) and 05.5 (Edit) draw it, in light and dark, 05.6 and 03.7 the states those leave out, and 05.1 to 05.3, which drew the sheet before it, were redrawn to it. The user approved them on 2026-11-04, after asking for the platform's real logos ("um seletor com a logo real das plataformas") in place of a segmented control of words, and for more vertical padding on the tiles.

What changes (`app/Pane/Views/ProfileSheet.swift`):

- **400 points wide**, from 360, for three tiles and a sentence under each switch. The rest of its frame stays: inset 8 from the window, and a rule under the header and over the footer.
- **The name, group, and color**, at the top, with no card: labels at 12 on 16-point lines (`FormLabel.Size.regular`), fields at 13, swatches 8 apart.
- **Privacy, Fingerprint, and Network**, each a label over a card, as Settings' groups (ADR 0065) at the panel's scale: rounded 15, over a fill of its own (`Palette.sheetCard`). Each switch has a sentence under it that says what its state does, and changes with it (`RowLabels`, shared with Settings).
- **The platform:** three tiles 72 tall, 13 above and below inside their border, each a logo over its name, the chosen one ringed in the foreground as the App icon picker's is (ADR 0089). Choosing is instant; hover fills the others.
- **A warning** when the platform is not the Mac's, on amber, which comes and goes with the layout's move (`Motion.toggle`), though the click that causes it lands at once.
- **One address field:** the proxy's type, host, and port, with `PaneMenuButton`'s `embedded` size for the type, which holds the width of its longest option, so the host does not move when it changes. The separate paste field is gone. A proxy string pasted into the host fills the fields; typed there, it fills them when the field is left, or on Test connection or Save, since a host typed a key at a time must not lose its colons as they come.
- **The body scrolls between two rules, the header's and the footer's, in the border color in both themes.** They were white 5%, the Electron app's, which only showed on dark, so on light the proxy's card cut hard at nothing. 0.16.1 gave the body the sidebar's fade for that (ADR 0061). On seeing it beside the rules on dark, the user gave the rule: a fade is for an edge with nothing to mark it (`DESIGN.md` › Scroll edges). So the rules show on light too, and the body does not fade.
- **The proxy test beside its button**, a dot and words, not on the button: yellow "Testing…", emerald "Connected · 45.12.8.19", or the error on red. An answer is dropped once the fields no longer hold the proxy it tested.

The words the boards leave to the states: "Sites see this Mac." with spoofing off, and "Keeps no pages or downloads." on a profile with nothing to clear.

**The logos** are vectors in the asset catalog, drawn at the boards' sizes (`app/Pane/Assets.xcassets/Platform*.imageset`; `NOTICE` gives their origins): Windows at 22 points in its #0078D4, Apple's at 26, black or white by the theme, and Tux at 26, with a faint edge on dark, where the black back would melt away. Tux's soft shadow is left out: it does not belong at 26 points.

The switch stays the app's one (ADR 0070): the boards' first draft had drawn a lighter one by hand, and they were corrected to Paper's control. `PresetRow`, the spinner (`Motion.spin`, Lucide's `loaderCircle`), and the green button's fill went with what used them.

Rejected:

- **A segmented control of words,** the first draft: the user asked for the logos.
- **One-colour logos** from one icon set, so they would sit with Lucide's: these are other companies' marks, and the point is to recognise them at once.
- **A wider card of switches with the fields beside them:** Settings' layout at 640 points, which a side panel has no room for.

Checked by `tests/scenarios/profile-sheet.ts`, which fills the sheet through the window's accessibility, presses and types as a user does, and renders each state in both themes; those renders were set beside the boards at 2×, where they agree but for the text's half-point baseline.
