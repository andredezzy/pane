# The sidebar and toolbar take Aside's sizes, and the content panel stays Pane's

The sidebar and the toolbar take Aside's sizes, at the user's request (Aside's and Dia's font sizes "and everything else"), while the content panel keeps Pane's own frame, at the user's correction: the panel is Pane's, not Aside's. Aside set the numbers because its layout is Pane's, a sidebar and then a panel with the toolbar inside. Its window was captured alone, measured at 2×, and the capture deleted; Dia was not open, and was not opened. Paper took the sizes first, on every window board (ADR 0058).

The sidebar is 260 points wide, Aside's width, and the resizable sidebar's default (ADR 0061). Rows are 32 points, 4 apart, on Aside's 36-point pitch, and round at 15. The favicon is 16 points, 7 in, and the title starts 10 after it, 41 points from the window's edge (Aside's: 40.5). Collapse all is a 28-point circle.

In the toolbar, back, forward, and reload are 32-point circles. Back and forward are Aside's chevrons, 1.5 points of stroke at 24, and reload is 17 points at the same stroke. The pinned actions and the puzzle are 32-point circles too, and the profile chip is a 28-point capsule, as Aside's toolbar button is. The pinned actions' capsule (ADR 0054) shows only while the pointer is over them or one's popup is open: Aside's has no capsule at rest.

The address at rest shows the domain (without www.), a divider, and the page's title, as Aside's does, and the domain starts 383 points from the window's edge, as Aside's. A click edits the URL: the field stays under the label, its text clear until focused, and the label lets the click through, so the field takes it as before.

The text first took Aside's 14 points too (Aside's titles have a 10-point cap height, and a 14-point title's caps rendered 20 pixels at 2×, as Aside's). It is 12 points now (ADR 0035), and the rows keep Aside's spacing.

The panel was first made flush with the window, as Aside's is, with the traffic lights on the toolbar's line and a hairline under the toolbar. All of that was reverted. The panel keeps its 8-point gutters, its radius of 18, and its ring, and the traffic lights stay at (12, 12) (ADR 0036).
