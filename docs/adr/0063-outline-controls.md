# Outline controls have no fill, and their hover is the foreground at 4%

> **On the Views line** (`docs/plans/views-migration.md`, H6): Settings' pop-up buttons are `MenuButton`s (`pane/ui/design/menu_button.cc`) and its PIN and update buttons outline `TextButton`s at h-8, each its border alone with the foreground's 4% under the pointer.

> **On the Views line** (`docs/plans/views-migration.md`, X4): the Extensions page's search and outline buttons went with Pane's page; Chrome's page (ADR 0062) draws its own.

An outline control is its border alone, over whatever it sits on, and the pointer over it lays the foreground at 4% (`Palette.outlineHover`, beside `Palette.press`). No fill was the user's request for the outline buttons and the user's call for the Extensions page's search; the hover was the user's call, on Settings' Download update. It covers every outline button (`PaneButtonStyle`'s outline variant, ADR 0070): Settings' PIN and update buttons, the sad tab's Reload, the profile sheet's, and the Extensions page's Keyboard shortcuts (ADR 0098), Details (ADR 0062), Options, Remove, and Visit Chrome Web Store, with Details' Remove. Update all was one until extensions updated themselves (ADR 0062). The pop-up buttons beside them (`PaneMenuButton`) take the same: Settings' presets, outline buttons when this was decided and pop-up buttons since (ADR 0065), and the Extensions page's profile select. A press still lays the foreground at 6% over the fill.

The hover was first the accent's fill. The accent is a solid gray, Secondary's own rest fill, so an outline button under the pointer looked like a secondary button, the heaviest thing in its row, while outline is the quietest control there is. At 4% the fill is lighter than any other hover, and, being the foreground, it is right over any surface: darker on light, lighter on dark, as the press's 6% is.

Text fields keep their fill, except the Extensions page's search: its border alone, over the page, in light and dark. With no fill, the field's padding would let a click through, so a click anywhere inside the border focuses the field.

What was rejected:

- **The accent at half strength for the hover:** a solid color, it reads differently on the window, the card, and a dialog, and on dark it would wash toward the background.
- **A stronger border instead of a hover fill:** the hover would be a change of outline, and no other control moves its border on hover.

How it is checked:

- **Paper:** 90 buttons and selects have no fill, and the six Extensions boards draw the search's border alone. Paper drew the hover first: its Buttons board has a hover row, the outline at the foreground's 4%, and the pressed outline over the lighter fill.
- **Renders:** the app was rendered with the hover held on, before and after, in light and dark, over Settings' card.
