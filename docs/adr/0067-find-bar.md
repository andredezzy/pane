# The find bar takes Aside's design

The find bar takes Aside's design, at the user's request, measured at 2× from the user's screenshot, without Aside's blue line between the field and the chevrons (the user's call). Paper drew it first: 03.3 Find in page, light and dark, and 02.5's three states (ADR 0058).

The bar is 357 by 54 points, at the same top right, on the popover's fill. Aside rounds it as a circle of 8, which is 14.7 as a squircle; the bar's squircle was 14 at first and is 15 now, on the radius scale (ADR 0057). Its soft shadow (black 25%, 40% in dark; radius 6, 6 down) matched Aside's across its edge and spread, within a pixel or two. It has no border, but in dark a hairline ring, as the popovers have, so it stands off the page.

Inside, the field starts 16 points in, at 13, a point under Aside's 14, as Settings took it (ADR 0065). It shows no placeholder, as Aside's shows none, though one stays, unseen, for VoiceOver. Then come the count (12, red when nothing matches) and the chevrons, 4 apart (18 points, Lucide's stroke), and last close (10 points, a 2.5 stroke), each in a 32-point circle with the accent's fill on hover. The chevrons and close land on Aside's to the pixel. Disabled is 40%, as Aside's gray, from a style of their own, since the plain style dimmed a disabled label a second time; that style is now `IconButtonStyle`, the icon buttons' one style (ADR 0070). The caret is the text's color, not the system's blue.

The Electron app's bar is gone: rounded-lg with a border and shadow-md, a 176-point field, and arrows. `mediumShadow`, `arrowUp`, and `arrowDown` had no other use, and went with it.
