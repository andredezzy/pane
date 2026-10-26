# Ardia (reference and adapted code)

Pane's window layer is modeled on Ardia's engine layer, ArdiaCore
(https://github.com/mantrakp04/ardia, `engine/ardiacore/src/ardia/core`),
which runs a native macOS app on Chrome's own framework built from Chromium
156.0.8078.47 — the same architecture as Pane's.

Ardia's own source files are Copyright 2026 Mantra Patel and licensed under
the Apache License, Version 2.0 (`LICENSE.Apache-2.0` here). Apache-2.0 code
may be combined into a GPL-3.0 work, which Pane is.

Files in `native/engine/pane` that adapt Ardia's code say so in their header,
name the Ardia file they come from, and state what changed:

- `browser/ui/browser_window.{h,mm}` — from `ac_browser_window.{h,mm}`
- `browser/ui/modal_dialog_host.{h,mm}` — from `ac_modal_dialog_host.{h,mm}`
- `browser/ui/window_host.{h,mm}`, `browser/ui/web_contents_delegate.{h,mm}`,
  `browser/ui/tab_bridge.{h,mm}` — modeled on `ac_browser.{h,mm}`
- `browser/ui/hooks.h` and patch `0003-pane-hosted-browsers.patch` — the shape
  of Ardia's hooks H1, H2, its viewless-browser guards and
  `chromium-window-hosted.patch`
