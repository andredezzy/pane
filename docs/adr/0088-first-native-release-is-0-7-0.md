# The first native release is 0.7.0, and v1.0.0 is withdrawn

The first native release is Pane 0.7.0, a minor bump from the Electron app's 0.6.0, and v1.0.0, published as that release earlier on 2026-10-28, is withdrawn: its GitHub release and its tag are deleted (the user's call: Pane is not 1.0.0 yet). This replaces ADR 0051's version; the rest of that record stands, the native app replacing the Electron app, which is retired. 0.7.0 carries the app as `main` has it at the `Release 0.7.0` commit, on the same engine, `engine-0d0a01f8c703`, and the release notes are 1.0.0's with the number changed. Every Electron Pane's update check still finds the native release newer than its own 0.6.0 and offers its dmg (ADR 0002).

Keeping 1.0.0 was rejected with the user's call. So was turning v1.0.0 into a draft or a prerelease: the update check, reading `releases/latest`, would stop seeing it, but the tag would stay public, and the version line would still have to pass 1.0.0 before anyone who installed it was offered an update. A version is never reused: the next release is 0.7.1 or 0.8.0 whatever this one turns out to need.

Whoever installed 1.0.0 is offered nothing until a release passes 1.0.0, since the update check only offers a newer version (ADR 0002): they install 0.7.0 from its dmg over it, and their data stays, under the same bundle ID. 1.0.0's dmg was downloaded once, by the user.

The release was tried before the tag, as users get it: the dmg `engine/scripts/dist.sh` built from the release commit, and its app, out of the dmg, passed `smoke`, `hosted`, `migration`, and `updates`. The Release workflow's tag check (ADR 0044) holds the tag to `app/VERSION`, and `releases/latest` gives v0.7.0 and its `Pane-0.7.0-arm64.dmg`, with no v1.0.0 release or tag left on GitHub.
