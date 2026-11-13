// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/engine_fingerprint.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "pane/model/host_platform.h"

namespace pane::model {

namespace {

base::ListValue StringsToValue(const std::vector<std::string>& strings) {
  base::ListValue list;
  for (const std::string& string : strings) {
    list.Append(string);
  }
  return list;
}

base::ListValue BrandsToValue(const std::vector<UABrand>& brands) {
  base::ListValue list;
  for (const UABrand& brand : brands) {
    list.Append(base::DictValue()
                    .Set("brand", brand.brand)
                    .Set("version", brand.version));
  }
  return list;
}

base::Value MetadataToValue(const UserAgentMetadata& metadata) {
  return base::Value(
      base::DictValue()
          .Set("brands", BrandsToValue(metadata.brands))
          .Set("fullVersionList", BrandsToValue(metadata.full_version_list))
          .Set("fullVersion", metadata.full_version)
          .Set("platform", metadata.platform)
          .Set("platformVersion", metadata.platform_version)
          .Set("architecture", metadata.architecture)
          .Set("bitness", metadata.bitness)
          .Set("model", metadata.model)
          .Set("mobile", metadata.mobile)
          .Set("wow64", metadata.wow64)
          .Set("formFactors", StringsToValue(metadata.form_factors)));
}

// A seed past int's range is written as a double.
base::Value SeedToValue(uint32_t seed) {
  return seed <= static_cast<uint32_t>(std::numeric_limits<int>::max())
             ? base::Value(static_cast<int>(seed))
             : base::Value(static_cast<double>(seed));
}

base::Value RendererToValue(const EngineFingerprint::Renderer& renderer) {
  const EngineFingerprint::Screen& screen = renderer.screen;
  base::DictValue dict =
      base::DictValue()
          .Set("platform", renderer.platform)
          .Set("hardwareConcurrency", renderer.hardware_concurrency)
          .Set("deviceMemory", renderer.device_memory)
          .Set("maxTouchPoints", renderer.max_touch_points)
          .Set("screen", base::DictValue()
                             .Set("width", screen.width)
                             .Set("height", screen.height)
                             .Set("availWidth", screen.avail_width)
                             .Set("availHeight", screen.avail_height)
                             .Set("availLeft", screen.avail_left)
                             .Set("availTop", screen.avail_top)
                             .Set("colorDepth", screen.color_depth))
          .Set("devicePixelRatio", renderer.device_pixel_ratio)
          .Set("canvasNoise", renderer.canvas_noise)
          .Set("audioNoise", renderer.audio_noise)
          .Set("noiseSeed", SeedToValue(renderer.noise_seed))
          .Set("locale", renderer.locale)
          .Set("hideSpeechVoices", renderer.hide_speech_voices);
  // What it has none of goes unsaid, not null.
  if (renderer.webgl_vendor) {
    dict.Set("webglVendor", *renderer.webgl_vendor);
  }
  if (renderer.webgl_renderer) {
    dict.Set("webglRenderer", *renderer.webgl_renderer);
  }
  if (renderer.timezone) {
    dict.Set("timezone", *renderer.timezone);
  }
  return base::Value(std::move(dict));
}

}  // namespace

EngineFingerprint::EngineFingerprint() = default;
EngineFingerprint::EngineFingerprint(const EngineFingerprint&) = default;
EngineFingerprint& EngineFingerprint::operator=(const EngineFingerprint&) =
    default;
EngineFingerprint::EngineFingerprint(EngineFingerprint&&) = default;
EngineFingerprint& EngineFingerprint::operator=(EngineFingerprint&&) = default;
EngineFingerprint::~EngineFingerprint() = default;

// static
EngineFingerprint EngineFingerprint::For(const Fingerprint& fingerprint,
                                         std::string_view profile_id,
                                         std::string_view engine_user_agent,
                                         std::string_view engine_version) {
  Fingerprint reconciled = fingerprint;
  reconciled.user_agent =
      ReconcileChromeVersion(fingerprint.user_agent, engine_user_agent);

  EngineFingerprint engine;
  engine.user_agent = reconciled.user_agent;
  engine.user_agent_metadata = ClientHints(reconciled, engine_version);
  engine.languages = fingerprint.languages.empty()
                         ? std::vector<std::string>{fingerprint.language}
                         : fingerprint.languages;
  engine.renderer = {
      .platform = std::string(NavigatorPlatform(fingerprint.platform)),
      .hardware_concurrency = fingerprint.hardware_concurrency,
      .device_memory = ApproximatedDeviceMemory(fingerprint.device_memory),
      .max_touch_points = fingerprint.max_touch_points,
      .screen = EngineScreen(fingerprint),
      // The claimed computer's display, whatever runs Pane: a Mac's Retina
      // panel, a PC's 1080p one (the presets' screens).
      .device_pixel_ratio =
          fingerprint.platform == Platform::kMacOS ? 2.0 : 1.0,
      .webgl_vendor = fingerprint.webgl
                          ? std::make_optional(fingerprint.webgl->vendor)
                          : std::nullopt,
      .webgl_renderer = fingerprint.webgl
                            ? std::make_optional(fingerprint.webgl->renderer)
                            : std::nullopt,
      .canvas_noise = fingerprint.canvas_noise,
      .audio_noise = fingerprint.audio_noise,
      .noise_seed = NoiseSeed(profile_id),
      .timezone = fingerprint.timezone.empty()
                      ? std::nullopt
                      : std::make_optional(fingerprint.timezone),
      .locale = fingerprint.language,
      // The computer's own voices would contradict another platform's.
      .hide_speech_voices = fingerprint.platform != HostPlatform()};
  return engine;
}

std::string EngineFingerprint::ToJson() const {
  return base::WriteJson(
             base::DictValue()
                 .Set("userAgent", user_agent)
                 .Set("userAgentMetadata", MetadataToValue(user_agent_metadata))
                 .Set("languages", StringsToValue(languages))
                 .Set("renderer", RendererToValue(renderer)))
      .value_or(std::string());
}

EngineFingerprint::Screen EngineScreen(const Fingerprint& fingerprint) {
  int inset = 0;
  switch (fingerprint.platform) {
    case Platform::kWindows:
      inset = 48;
      break;
    case Platform::kMacOS:
      inset = 25;
      break;
    case Platform::kLinux:
      inset = 27;
      break;
  }
  return {.width = fingerprint.screen.width,
          .height = fingerprint.screen.height,
          .avail_width = fingerprint.screen.width,
          .avail_height = fingerprint.screen.height - inset,
          .avail_left = 0,
          .avail_top = fingerprint.platform == Platform::kMacOS ? 25 : 0,
          .color_depth = fingerprint.screen.color_depth};
}

double ApproximatedDeviceMemory(double gigabytes) {
  if (!(gigabytes > 0.25)) {
    return 0.25;
  }
  return std::min(8.0, std::pow(2.0, std::floor(std::log2(gigabytes))));
}

uint32_t NoiseSeed(std::string_view profile_id) {
  // Wraps as a 32-bit int does, read signed at the end.
  uint32_t hash = 0;
  for (const char16_t unit : base::UTF8ToUTF16(profile_id)) {
    hash = hash * 31 + unit;
  }
  return static_cast<uint32_t>(
      std::abs(static_cast<int64_t>(static_cast<int32_t>(hash))));
}

}  // namespace pane::model
