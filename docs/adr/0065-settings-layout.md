# Settings take Aside's layout

Settings are laid out as Aside's Appearance page, at the user's request: a title, then each section as a card of rows, each row with its title, a muted description, and its control at the end. Aside's page was measured at 2× from the user's screenshot and set at Pane's type scale, a point under Aside's 14. Paper drew it first (06.1 Settings, light and dark; ADR 0058).

The page has "Settings" at 22, then each section's muted label at 13 over a card. Cards are rounded 22 with a 1-point border, and their rows are at least 56 points tall, with a hairline between. A row's title is 13 and its description 12, muted, and its control sits 10 points from the card's edge. The version is the Updates card's last row.

The presets became pop-up buttons, as Aside's are: 32 points, with a 1-point border and no fill (ADR 0063), showing the value (Theme's with its icon) and up and down chevrons, and opening the system's menu with the current option checked. They are `PaneMenuButton`s, the app's one menu trigger (ADR 0070), and VoiceOver reads their option as their value (ADR 0071). The PIN and update buttons are the outline buttons, small.

The Electron app's caps labels and the hairlines between sections are gone. Extensions left Settings for a page of their own (ADR 0062), and Security follows Memory & disk.
