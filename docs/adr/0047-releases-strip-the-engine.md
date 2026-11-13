# A release strips its local symbols, and publishes them beside it

A release drops the local symbols of the engine and of every executable in the bundle, as Chrome's official builds do, while development builds keep them for stack traces. `scripts/dist.sh` packages with `PANE_STRIP=1`, and `scripts/package.sh` then runs `strip -x` on the engine's framework (`Pane Framework`) and its dylibs, the bundle's executable, the volume's watchdog (`pane-volume-watch`, ADR 0113), `chrome_crashpad_handler`, `app_mode_loader`, and each helper's executable. The framework shrinks from about 480 MB to about 270 MB, the exports the app links unchanged (the same `dyld_info -exports`); when this came in, it took the engine from 484 to 269 MB and the dmg from 195 to 161 MB. The prebuilt engine's archive strips the framework and its dylibs the same way (`scripts/publish-engine.sh`, ADR 0049).

The symbols are published, so a crash report from any copy reads to function and line (`docs/building.md`, Reading a crash report). Archiving the engine runs `dsymutil` over the build's objects, which the next build changes (157: about a minute and 10 GB of memory, 2 GB of DWARF, 0.5 GB zipped), made when missing and only from the engine `build.sh` built from these sources, and `publish-engine.sh` uploads it as `PaneCore-<chromium>-<key:12>.dSYM.zip` to the engine's `engine-<key:12>` prerelease, beside the archive, whose `PaneCore-` name the engine archives keep (ADR 0009). A copy's `PaneEngineKey` names its engine (ADR 0044). The bundle's executable is Chromium's loader, whose code is the framework's (ADR 0009), so the framework's dSYM is the release's whole symbols, and `dist.sh` makes no app dSYM.

Before 0.13.1 a crash inside the engine read as addresses only; `engine-95313854dfb3`, 0.13.0's engine, got its symbols then. From 0.13.0 to 0.19.2 the Swift app's executable had a dSYM of its own, which `dist.sh` zipped only when its UUID matched (`Pane-<version>-arm64.dSYM.zip`) and the Release workflow published beside the dmg.

The unstripped binary, with the same UUID, also stays in the engine's `out/Release`.

`strip` warns, for each file, that it will invalidate the code signature, and the bundle is signed again after, so that warning alone is dropped. Everything else `strip` says shows: a wrong path used to exit `package.sh` with no message, and a file that is no Mach-O was skipped in silence. Both now say so.

How it is checked:

- **The dSYM's UUID** is its stripped image's: `docs/building.md` reads a report by checking it (`dwarfdump --uuid`) against the report's `Pane Framework` image before `atos`, and 0.13.0's engine symbols matched the installed 0.13.0's engine, `atos` reading frames to function and line.
