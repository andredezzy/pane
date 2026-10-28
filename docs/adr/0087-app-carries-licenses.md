# The app carries its licenses

The packaged app carries Pane's license, its `NOTICE`, and the license and origin of each project Pane takes code from, because those licenses ask that their text go with every copy, and the dmg carried none of it. It was found answering what `third_party/` is for. `third_party/` holds the license and origin of each such project: Ardia (Apache-2.0, the engine's window layer), Lucide (ISC, the icons), and OpenBSD's bcrypt (ISC and BSD, the PIN hashes). `engine/scripts/package.sh` puts Pane's `LICENSE`, `NOTICE`, and `third_party/` in `Contents/Resources/Licenses`, and `NOTICE` names the source repository, as GPL-3.0 asks of a binary.

The licenses stay apart from the code they cover, in `third_party/` at the root (ADR 0086), because the Ardia code lives in `engine/pane`, where a new file would change the engine key (ADR 0049). `NOTICE` lost its "Pane native" title and a stale path to the icons.
