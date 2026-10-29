# An outline control's hover is the foreground at 4%

An outline control's hover fills it with the foreground at 4%, no longer the accent: the user's call, on Settings' Download update. The accent is a solid gray, Secondary's own rest fill, so an outline button under the pointer looked like a secondary button, the heaviest thing in its row, while outline is the quietest control there is. At 4% the fill is lighter than any other hover, and, being the foreground, it is right over any surface: darker on light, lighter on dark, as the press's 6% is (`Palette.outlineHover`, beside `Palette.press`). It replaces ADR 0063's hover, and every outline control takes it: `PaneButtonStyle`'s outline variant and the pop-up buttons that sit beside it (ADR 0065). A press still lays the foreground at 6% over the fill.

The accent at half strength was rejected: it is a solid color, so it reads differently on the window, the card, and a dialog, and on dark it would wash toward the background. So was dropping the fill for a stronger border, which would make the hover a change of outline, and no other control moves its border on hover.

Paper drew it first: its Buttons board has a hover row, the outline at the foreground's 4%, and the pressed outline over the lighter fill. The app was rendered with the hover held on, before and after, in light and dark, over Settings' card.
