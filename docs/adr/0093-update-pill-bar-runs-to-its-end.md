# The update pill's bar runs to its end, and draws back for the dismiss button

While an update downloads, the pill's progress bar runs out to 7 points from the pill's end, as the dot sits 7 from its start: the user's call, on a pill whose bar stopped 24 short, in room kept for a dismiss button that shows only under the pointer. Under the pointer the bar draws back to 32 from the end, so it sits about 14 points from the X's ink, as far as the text sits from the bar; a first try at 24 left 6.5, and the user asked for more room. The bar draws back within room it always keeps, 24 points for itself and 25 for the button, so the text never moves or cuts differently as the pointer comes and goes, and at the narrowest sidebar (200 points) the bar shrinks from 51 points to 26 rather than going.

The bar and the X take turns rather than cross (`Motion.hoverTurn`, the hover's 0.15 s after 0.08 s): the bar draws back at once, and the X fades in once the bar has cleared its place; on the way out the X goes first, and the bar runs out after it. Fading both at once left the X at 20 to 50% over the bar's tip for three frames, each way.

What was rejected:

- **Keeping the bar 24 short at all times:** room for a button that is not there.
- **Taking the button's room from the text under the pointer:** at narrow sidebars the text would cut differently as the pointer came and went.
- **Letting the bar shrink to nothing under the pointer:** at the narrowest sidebar the download's only sign of progress would vanish while the user looks at it.

How it is checked:

- **Paper:** 03.3 Sidebar · Footer draws the downloading pill at rest and under the pointer, and 04.27 draws it at rest, in light and dark.
- **A recording of the pill:** a build whose hover toggled itself every 0.9 s, its X drawn pure red to tell it from the bar, was recorded losslessly at 60 frames a second. It ran in light and dark, at sidebars of 260 and 200.
  - At rest the bar ends 7.0 points from the pill's end, and under the pointer 32.0. It draws back in about 130 ms, and the X starts to show once the bar has passed its place.
  - On the way back the X is at 1% when the bar crosses its place. No frame has the bar under a visible X.
  - The text's right edge stays on the same pixel in every frame.
- **The `updates` and `update-install` scenarios** pass.
