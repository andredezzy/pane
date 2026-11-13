# Outline controls have no fill, and their hover is the foreground at 4%

An outline control is its border alone, over whatever it sits on, and the pointer over it lays the foreground at 4% (`kPaneColorOutlineHover`, beside `kPaneColorPress`, `pane/ui/design/palette.cc`). No fill was the user's request for the outline buttons and the user's call for the Extensions page's search; the hover was the user's call, on Settings' Download update. It covers every outline button (`ButtonVariant::kOutline`, `pane/ui/design/controls.h`, ADR 0070): Settings' PIN and update buttons, the sad tab's Reload, and the profile sheet's Cancel and Test connection. The pop-up buttons beside them (`MenuButton`, `pane/ui/design/menu_button.h`) take the same: Settings' presets, outline buttons when this was decided and pop-up buttons since (ADR 0065). A press still lays the foreground at 6% over the fill.

Pane's own Extensions page, whose outline buttons (Keyboard shortcuts, Details, Options, Remove, Visit Chrome Web Store, and Details' Remove) and search took the same, went at 0.20.0, with its profile select: Chrome's own page draws its own controls (ADR 0062). Update all was one until extensions updated themselves.

The hover was first the accent's fill. The accent is a solid gray, Secondary's own rest fill, so an outline button under the pointer looked like a secondary button, the heaviest thing in its row, while outline is the quietest control there is. At 4% the fill is lighter than any other hover, and, being the foreground, it is right over any surface: darker on light, lighter on dark, as the press's 6% is.

Text fields keep their fill. The Extensions page's search was the one without: its border alone, over the page, in light and dark, a click anywhere inside the border focusing it, since with no fill its padding would have let a click through.

What was rejected:

- **The accent at half strength for the hover:** a solid color, it reads differently on the window, the card, and a dialog, and on dark it would wash toward the background.
- **A stronger border instead of a hover fill:** the hover would be a change of outline, and no other control moves its border on hover.

How it is checked:

- **Paper:** 90 buttons and selects have no fill, and the six Extensions boards drew the search's border alone. Paper drew the hover first: its Buttons board has a hover row, the outline at the foreground's 4%, and the pressed outline over the lighter fill.
- **Renders:** the app was rendered with the hover held on, before and after, in light and dark, over Settings' card.
