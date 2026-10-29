# Outline controls have no fill

> **Its hover is replaced by [ADR 0091](0091-outline-hover-is-foreground-4.md)**: an outline control's hover is the foreground at 4%, no longer the accent. The rest stands.

An outline control is its border alone, over whatever it sits on, with the accent's fill on hover as before: the user's request for the outline buttons, and the user's call for the Extensions page's search. That is every outline button (`PaneButtonStyle`'s outline variant, ADR 0070): Settings' PIN and update buttons, the sad tab's Reload, the profile sheet's, and the Extensions page's Options, Remove, and Update all. Settings' presets, outline buttons when this was decided, are pop-up buttons now, with a border and no fill (ADR 0065). The Extensions page's profile select, which sits beside the page's buttons, has no fill either. Paper follows: 90 buttons and selects.

Text fields keep their fill, except the Extensions page's search: its border alone, over the page, in light and dark. With no fill, the field's padding would let a click through, so a click anywhere inside the border focuses the field. Paper's six Extensions boards follow.
