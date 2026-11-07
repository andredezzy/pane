// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_RENDERER_PANE_FINGERPRINT_H_
#define PANE_RENDERER_PANE_FINGERPRINT_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/platform/platform_export.h"

class SkPixmap;

namespace blink {

class StaticBitmapImage;

// The fingerprint of the profile this renderer serves. The browser
// passes it in --pane-fingerprint (pane/browser/fingerprints.h); a renderer
// never serves two profiles, so there is one per process. Blink reports
// these values natively, in every frame and worker, instead of Chrome's own.
class PLATFORM_EXPORT PaneFingerprint {
 public:
  struct Screen {
    int width = 0;
    int height = 0;
    int avail_width = 0;
    int avail_height = 0;
    int avail_left = 0;
    int avail_top = 0;
    int color_depth = 24;
  };

  // Null when the profile presents Chrome's own values. Any thread.
  static const PaneFingerprint* Get();

  // navigator.platform.
  std::string platform;
  unsigned hardware_concurrency = 0;
  float device_memory = 0;
  Screen screen;
  double device_pixel_ratio = 1;
  // WEBGL_debug_renderer_info's unmasked vendor and renderer.
  std::optional<std::string> webgl_vendor;
  std::optional<std::string> webgl_renderer;
  bool canvas_noise = false;
  bool audio_noise = false;
  uint32_t noise_seed = 0;
  // An IANA time zone, or empty for the system's.
  std::string timezone;
  // Intl's default locale.
  std::string locale;
  bool hide_speech_voices = false;

  // Adds the canvas noise to `pixmap`, unpremultiplied RGBA read at (`x`, `y`)
  // of a canvas `canvas_width` wide: ±1 on each color channel of each opaque
  // pixel, keyed by its position in the canvas, so any read of a pixel gets
  // the same noise. A no-op without canvas noise or for another format.
  void NoiseCanvasPixels(const SkPixmap& pixmap, int x, int y,
                         int canvas_width) const;

  // `image` with the canvas noise, for the canvas exports (toDataURL, toBlob,
  // convertToBlob); `image` itself without canvas noise.
  scoped_refptr<StaticBitmapImage> NoiseCanvasImage(
      scoped_refptr<StaticBitmapImage> image) const;

  // Adds the audio noise to channel `channel` of an OfflineAudioContext's
  // rendered buffer. A no-op without audio noise.
  void NoiseAudioChannel(base::span<float> samples, unsigned channel) const;
};

}  // namespace blink

#endif  // PANE_RENDERER_PANE_FINGERPRINT_H_
