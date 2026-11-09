// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/profile_form.h"

namespace pane::model {

Fingerprint FingerprintPreset(Platform platform) {
  Fingerprint fingerprint;
  fingerprint.platform = platform;
  fingerprint.language = "en-US";
  fingerprint.languages = {"en-US", "en"};
  fingerprint.hardware_concurrency = 8;
  fingerprint.device_memory = 8;
  fingerprint.max_touch_points = 0;
  switch (platform) {
    case Platform::kWindows:
      fingerprint.user_agent =
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
          "(KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36";
      fingerprint.screen = {.width = 1920, .height = 1080, .color_depth = 24};
      fingerprint.webgl = Fingerprint::WebGL{
          .vendor = "Google Inc. (NVIDIA)",
          .renderer =
              "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 "
              "vs_5_0 ps_5_0, D3D11)"};
      break;
    case Platform::kMacOS:
      fingerprint.user_agent =
          "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 "
          "Safari/537.36";
      fingerprint.screen = {.width = 1440, .height = 900, .color_depth = 30};
      fingerprint.webgl =
          Fingerprint::WebGL{.vendor = "Google Inc. (Apple)",
                             .renderer = "ANGLE (Apple, Apple M1, OpenGL 4.1)"};
      break;
    case Platform::kLinux:
      fingerprint.user_agent =
          "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
          "Gecko) Chrome/136.0.0.0 Safari/537.36";
      fingerprint.screen = {.width = 1920, .height = 1080, .color_depth = 24};
      fingerprint.webgl = Fingerprint::WebGL{
          .vendor = "Google Inc. (Intel)",
          .renderer =
              "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, "
              "OpenGL 4.6)"};
      fingerprint.hardware_concurrency = 4;
      break;
  }
  return fingerprint;
}

}  // namespace pane::model
