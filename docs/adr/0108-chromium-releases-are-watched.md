# Chromium's releases are watched

A scheduled workflow, `.github/workflows/chromium-watch.yml`, compares Chrome's Mac stable version with `engine/CHROMIUM_VERSION` every six hours and opens an issue, one at a time, when Pane is behind. Pane ships Chromium's security fixes only by moving its engine to a new tag (`engine/UPGRADING.md`), and nothing noticed when Chrome moved: on 2026-10-30 Pane was on 157.0.8103.15, Google's Beta, while Mac stable was 157.0.8103.26, with 73 security fixes over two updates, 5 of them critical. Dia and Brave ship such fixes within one or two days. The research into how Aside, Dia, and other Chromium browsers ship them found it.

`engine/UPGRADING.md` now gives Chrome's cadence since 153 (a milestone every two weeks, security updates every week) and says to follow the Mac stable channel. The issue says whether Pane's patches apply to the new tag: `engine/scripts/preflight.sh` fetches the files they touch at that tag from Chromium's gitiles and applies them, in under a minute, as Brave and ungoogled-chromium check theirs before a sync.

What was rejected:

- **Checking by hand:** it is what let Pane fall behind.
- **Building the new engine in CI:** a Chromium build takes hours and about 100 GB, which a GitHub runner does not have (ADR 0049's prebuilt engines).

How it is checked:

- **A run of the workflow** (workflow_dispatch) logs both versions and opens the issue when they differ.
- **`engine/scripts/preflight.sh 157.0.8103.26`** reports every patch applying; at 156.0.8078.48 it names 0006 and 0007.
