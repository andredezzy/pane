// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/renderer/pane_fingerprint.h"

#include <algorithm>
#include <utility>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"

namespace blink {

namespace {

constexpr char kFingerprintSwitch[] = "pane-fingerprint";

// The renderer half of the fingerprint Pane builds (model::EngineFingerprint's
// renderer part, pane/model/engine_fingerprint.h).
std::optional<PaneFingerprint> Parse(const std::string& json) {
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!root) {
    LOG(ERROR) << "Pane: --" << kFingerprintSwitch << " is not a JSON object.";
    return std::nullopt;
  }

  PaneFingerprint fingerprint;
  if (const std::string* platform = root->FindString("platform")) {
    fingerprint.platform = *platform;
  }
  fingerprint.hardware_concurrency =
      std::max(1, root->FindInt("hardwareConcurrency").value_or(1));
  fingerprint.device_memory = root->FindDouble("deviceMemory").value_or(8);
  if (const base::DictValue* screen = root->FindDict("screen")) {
    fingerprint.screen.width = screen->FindInt("width").value_or(0);
    fingerprint.screen.height = screen->FindInt("height").value_or(0);
    fingerprint.screen.avail_width = screen->FindInt("availWidth").value_or(0);
    fingerprint.screen.avail_height =
        screen->FindInt("availHeight").value_or(0);
    fingerprint.screen.avail_left = screen->FindInt("availLeft").value_or(0);
    fingerprint.screen.avail_top = screen->FindInt("availTop").value_or(0);
    fingerprint.screen.color_depth = screen->FindInt("colorDepth").value_or(24);
  }
  fingerprint.device_pixel_ratio =
      root->FindDouble("devicePixelRatio").value_or(1);
  if (const std::string* vendor = root->FindString("webglVendor")) {
    fingerprint.webgl_vendor = *vendor;
  }
  if (const std::string* renderer = root->FindString("webglRenderer")) {
    fingerprint.webgl_renderer = *renderer;
  }
  fingerprint.canvas_noise = root->FindBool("canvasNoise").value_or(false);
  fingerprint.audio_noise = root->FindBool("audioNoise").value_or(false);
  fingerprint.noise_seed =
      static_cast<uint32_t>(root->FindDouble("noiseSeed").value_or(0));
  if (const std::string* timezone = root->FindString("timezone")) {
    fingerprint.timezone = *timezone;
  }
  if (const std::string* locale = root->FindString("locale")) {
    fingerprint.locale = *locale;
  }
  fingerprint.hide_speech_voices =
      root->FindBool("hideSpeechVoices").value_or(false);
  return fingerprint;
}

// The Electron app's canvas noise (fingerprint-preload.ts deltaAt): a hash
// of the seed and the channel's position, so a migrated profile draws the
// same canvases.
int CanvasDelta(uint32_t seed, uint32_t index) {
  uint32_t hash = seed ^ index;
  hash = (hash ^ (hash >> 16)) * 0x45d9f3b3u;
  hash = (hash ^ (hash >> 16)) * 0x45d9f3b3u;
  hash ^= hash >> 16;
  return (hash & 0x80000000u) ? 1 : -1;
}

}  // namespace

// static
const PaneFingerprint* PaneFingerprint::Get() {
  static const base::NoDestructor<std::optional<PaneFingerprint>> fingerprint(
      []() -> std::optional<PaneFingerprint> {
        const base::CommandLine& command_line =
            *base::CommandLine::ForCurrentProcess();
        if (!command_line.HasSwitch(kFingerprintSwitch)) {
          return std::nullopt;
        }
        return Parse(command_line.GetSwitchValueASCII(kFingerprintSwitch));
      }());
  return fingerprint->has_value() ? &fingerprint->value() : nullptr;
}

void PaneFingerprint::NoiseCanvasPixels(const SkPixmap& pixmap,
                                        int x,
                                        int y,
                                        int canvas_width) const {
  if (!canvas_noise || pixmap.colorType() != kRGBA_8888_SkColorType ||
      !pixmap.writable_addr()) {
    return;
  }

  // SAFETY: an SkPixmap's pixels span computeByteSize() bytes.
  base::span<uint8_t> pixels = UNSAFE_BUFFERS(base::span(
      static_cast<uint8_t*>(pixmap.writable_addr()), pixmap.computeByteSize()));
  const size_t row_bytes = pixmap.rowBytes();
  const size_t width = static_cast<size_t>(pixmap.width());

  for (int row = 0; row < pixmap.height(); ++row) {
    base::span<uint8_t> line =
        pixels.subspan(static_cast<size_t>(row) * row_bytes, width * 4);
    for (size_t column = 0; column < width; ++column) {
      base::span<uint8_t> pixel = line.subspan(column * 4, 4u);
      // Only opaque pixels: their premultiplied and unpremultiplied values
      // agree, so every read path sees the same noise.
      if (pixel[3] != 255) {
        continue;
      }
      const uint32_t index = static_cast<uint32_t>(
          ((y + row) * canvas_width + (x + static_cast<int>(column))) * 3);
      for (uint32_t channel = 0; channel < 3; ++channel) {
        const int value =
            pixel[channel] + CanvasDelta(noise_seed, index + channel);
        pixel[channel] = static_cast<uint8_t>(std::clamp(value, 0, 255));
      }
    }
  }
}

scoped_refptr<StaticBitmapImage> PaneFingerprint::NoiseCanvasImage(
    scoped_refptr<StaticBitmapImage> image) const {
  if (!canvas_noise || !image) {
    return image;
  }

  sk_sp<SkImage> source = image->PaintImageForCurrentFrame().GetSwSkImage();
  if (!source) {
    return image;
  }

  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(SkImageInfo::Make(
          source->width(), source->height(), kRGBA_8888_SkColorType,
          kUnpremul_SkAlphaType, source->refColorSpace())) ||
      !source->readPixels(bitmap.pixmap(), 0, 0)) {
    return image;
  }

  NoiseCanvasPixels(bitmap.pixmap(), 0, 0, source->width());
  bitmap.setImmutable();
  return UnacceleratedStaticBitmapImage::Create(bitmap.asImage(),
                                                image->Orientation());
}

void PaneFingerprint::NoiseAudioChannel(base::span<float> samples,
                                        unsigned channel) const {
  if (!audio_noise) {
    return;
  }

  // The Electron app's audio noise: a seeded linear congruential sequence,
  // one step per sample.
  uint32_t state = (noise_seed ^ 0x5bd1e995u) + channel * 0x9e3779b9u;
  for (float& sample : samples) {
    state = state * 1664525u + 1013904223u;
    sample = static_cast<float>(
        static_cast<double>(sample) +
        (static_cast<double>(state >> 16) / 65536.0 - 0.5) * 0.0001);
  }
}

}  // namespace blink
