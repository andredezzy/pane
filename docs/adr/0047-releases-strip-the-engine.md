# A release strips its local symbols, and publishes them beside it

A release drops the local symbols of the engine and of every executable in the bundle, which took PaneCore from 484 to 269 MB and the dmg from 195 to 161 MB, while development builds keep them for stack traces. `scripts/dist.sh` packages with `PANE_STRIP=1`, and `scripts/package.sh` then runs `strip -x` on PaneCore and the framework's dylibs, and since 0.13.0 on the app's executable, `chrome_crashpad_handler`, `app_mode_loader`, and each helper's executable too. The exports the app links are unchanged (the same `dyld_info -exports`). The prebuilt engine's archive strips PaneCore and its dylibs the same way (ADR 0049).

The symbols are published, so a crash report from any copy reads to function and line (`docs/building.md`, Reading a crash report):

- **The app's**, since 0.13.0: `dist.sh` zips the dSYM of the executable in the dmg, `Pane-<version>-arm64.dSYM.zip`, only when its UUID matches, and the Release workflow publishes it beside the dmg.
- **PaneCore's**, since 0.13.1: archiving the engine (`PANE_ENGINE_ARCHIVE=1 package.sh`) runs `dsymutil` over the build's objects, which the next build changes (157: about a minute and 10 GB of memory, 2 GB of DWARF, 0.5 GB zipped), and `publish-engine.sh` uploads `PaneCore-<chromium>-<key:12>.dSYM.zip` to the engine's `engine-<key:12>` prerelease. A copy's `PaneEngineKey` names its engine (ADR 0044). Before, a crash inside the engine read as addresses only; `engine-95313854dfb3`, 0.13.0's engine, got its symbols then.

The unstripped binary, with the same UUID, also stays in the engine's `out/Release`.

`strip` warns, for each file, that it will invalidate the code signature, and the bundle is signed again after, so that warning alone is dropped. Everything else `strip` says shows: a wrong path used to exit `package.sh` with no message, and a file that is no Mach-O was skipped in silence. Both now say so.

How it is checked:

- **Each dSYM's UUID** is its stripped image's: `dist.sh` compares the app's with `dwarfdump --uuid`, and 0.13.0's PaneCore symbols matched the installed 0.13.0's PaneCore, `atos` reading frames to function and line.
