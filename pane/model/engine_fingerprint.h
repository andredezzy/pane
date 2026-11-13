// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_ENGINE_FINGERPRINT_H_
#define PANE_MODEL_ENGINE_FINGERPRINT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pane/model/client_hints.h"
#include "pane/model/profile.h"

namespace pane::model {

// What the engine applies for a fingerprinted profile (the engine's
// Fingerprints, set for each profile): the User-Agent, its Client Hints, and
// the languages the browser side presents, and the values each of the
// profile's renderers reports (the Electron app's fingerprint-preload.ts,
// which spoofed the same surfaces from script).
struct EngineFingerprint {
  struct Screen {
    friend bool operator==(const Screen&, const Screen&) = default;

    int width = 0;
    int height = 0;
    int avail_width = 0;
    int avail_height = 0;
    int avail_left = 0;
    int avail_top = 0;
    int color_depth = 0;
  };

  // What Blink reports in every frame and worker of the profile.
  struct Renderer {
    friend bool operator==(const Renderer&, const Renderer&) = default;

    std::string platform;
    int hardware_concurrency = 0;
    double device_memory = 0;
    int max_touch_points = 0;
    Screen screen;
    double device_pixel_ratio = 0;
    std::optional<std::string> webgl_vendor;
    std::optional<std::string> webgl_renderer;
    bool canvas_noise = false;
    bool audio_noise = false;
    // Seeds the canvas and audio noise, so it is stable per profile.
    uint32_t noise_seed = 0;
    std::optional<std::string> timezone;
    // The default locale of Intl.
    std::string locale;
    // The host's own voices would contradict another platform
    // (HostPlatform).
    bool hide_speech_voices = false;
  };

  EngineFingerprint();
  EngineFingerprint(const EngineFingerprint&);
  EngineFingerprint& operator=(const EngineFingerprint&);
  EngineFingerprint(EngineFingerprint&&);
  EngineFingerprint& operator=(EngineFingerprint&&);
  ~EngineFingerprint();

  // `engine_user_agent` and `engine_version` are the engine's own (its
  // User-Agent and Chromium version): the claimed Chrome version always
  // follows the engine.
  static EngineFingerprint For(const Fingerprint& fingerprint,
                               std::string_view profile_id,
                               std::string_view engine_user_agent,
                               std::string_view engine_version);

  // The JSON the engine reads, keys sorted so equal fingerprints compare equal
  // as text.
  std::string ToJson() const;

  friend bool operator==(const EngineFingerprint&,
                         const EngineFingerprint&) = default;

  std::string user_agent;
  UserAgentMetadata user_agent_metadata;
  std::vector<std::string> languages;
  Renderer renderer;
};

// The available area leaves room for the OS's own bars: the taskbar on
// Windows, the menu bar on macOS, the panel on Linux. A full-screen available
// area, or the host's menu bar inset, is a tell.
EngineFingerprint::Screen EngineScreen(const Fingerprint& fingerprint);

// Chrome reports device memory as a power of two from 0.25 to 8 GB
// (ApproximatedDeviceMemory); any other value is a value Chrome never
// reports.
double ApproximatedDeviceMemory(double gigabytes);

// The Electron app's seed (a Java-style hash of the profile ID's UTF-16 code
// units, made positive), so a migrated profile keeps its canvas and audio
// results.
uint32_t NoiseSeed(std::string_view profile_id);

}  // namespace pane::model

#endif  // PANE_MODEL_ENGINE_FINGERPRINT_H_
