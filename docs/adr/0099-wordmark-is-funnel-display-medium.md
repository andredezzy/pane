# Pane's wordmark is set in Funnel Display Medium

Pane's name, wherever it stands as the brand (beside the icon, in the installer's headline, on the site), is set in Funnel Display Medium (500), tracked −3%. It is the user's pick from three rounds drawn in Paper (01 · Brand: 01.12, 01.13, 01.14): eight families of every kind, fourteen modern sans in the vein of the one the user liked, and fifteen serifs. Funnel Display at SemiBold was the only one the user liked in the first round; Medium, a step lighter, was the one kept from the second. Its open, geometric shapes read as modern without being cold, and they sit well beside the window icon's white frame.

In the lockup, the name's capitals stand almost as tall as the icon's body (the user's call: at first the name stood taller than the icon), 0.26 of the type size from it. A headline pairs Regular (400) with the Medium name, as "Install **Pane**" does. Paper's 01.11 Wordmark board draws both, on light and on dark. Funnel Display is under the SIL Open Font License, so the app, the dmg, and the site can carry it.

The interface keeps the system font, as Finder's labels and macOS's controls do. The wordmark is the brand's alone.

What was rejected:

- **The first round's others:** Fraunces, Instrument Serif, Bricolage Grotesque, Instrument Sans, Fredoka, Young Serif, and Hanken Grotesk.
- **Funnel Display SemiBold:** kept from the first round, then dropped for Medium in the second.
- **The second round's twelve others:** Sora, Outfit, Plus Jakarta Sans, Manrope, Gabarito, Host Grotesk, Red Hat Display, Urbanist, Onest, Unbounded, Geologica, and Familjen Grotesk.
- **Serifs:** Playfair Display, DM Serif Display, Bodoni Moda, Libre Caslon Display, Cormorant Garamond, EB Garamond, Newsreader, Source Serif 4, Literata, Spectral, Gloock, Hedvig Letters Serif, Roboto Serif, Brygada 1918, and Zilla Slab. The user kept the sans.
- **Funnel Display for the interface:** the app's text stays the system's, as every Mac app's does.

How it is checked:

- **Paper:** 01.11 Wordmark and the installer's window (08.1) set the name in Funnel Display Medium.
- **The dmg:** its background draws the headline from the font file (ADR 0100).
- **Each implementation:** a surface that sets the wordmark renders it from the font file itself, never a fallback, and is compared with 01.11 on screen.
