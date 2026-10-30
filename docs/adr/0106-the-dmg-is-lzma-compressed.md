# The dmg is LZMA-compressed

Pane's dmg is ULMO (LZMA) where it was UDZO (zlib). Every user downloads it, at install and at every update, and LZMA makes it 22% smaller: 145.7 MB against 187.6 MB for 0.12.0, 42 MB less each time, about 6.7 s at 50 Mbit/s. Unpacking costs more: copying the app out of a mounted ULMO dmg took 2.80 s against 0.70 s, which an update spends in the background while staging (ADR 0092). macOS reads ULMO since 10.15, and Pane needs 14. The reverse engineering of Aside's and Dia's bundles found it (2026-10-30). It replaces the format of ADR 0046, whose dmgbuild and window stay.

What was rejected:

- **UDZO, as before:** 42 MB more for every download, to save about 2 s of background work.
- **ULFO (LZFSE):** about as fast as UDZO to unpack, and barely smaller (185.4 MB).

How it is checked:

- **The measurement:** the 0.12.0 dmg converted with hdiutil into each format, attached and its app copied twice (2026-10-30).
- **The next release:** its dmg is ULMO (`hdiutil imageinfo`), and the live update check against it passes: download, staging, the swap, and the reopen.
