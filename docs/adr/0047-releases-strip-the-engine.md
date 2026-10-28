# A release strips the engine's local symbols

A release drops the engine's local symbols, which takes PaneCore from 484 to 269 MB and the dmg from 195 to 161 MB, while development builds keep them for stack traces. `engine/scripts/dist.sh` packages with `PANE_STRIP=1`, and `engine/scripts/package.sh` then runs `strip -x` on PaneCore and the framework's dylibs. The exports the app links are unchanged (the same `dyld_info -exports`). The unstripped binary, with the same UUID, stays in the engine's `out/Release` for symbolicating a local release. The prebuilt engine's archive is stripped the same way (ADR 0049).

`strip` warns, for each file, that it will invalidate the code signature, and the bundle is signed again after, so that warning alone is dropped. Everything else `strip` says shows: a wrong path used to exit `package.sh` with no message, and a file that is no Mach-O was skipped in silence. Both now say so.
