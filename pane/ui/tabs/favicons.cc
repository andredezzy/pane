// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/tabs/favicons.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace pane {

namespace {

constexpr std::string_view kPNGDataURLPrefix = "data:image/png;base64,";

// At most kFaviconPixels a side, its shape kept: a site's 180-pixel touch icon
// would otherwise weigh tens of KB of base64 in profiles.json, written at every
// save.
SkBitmap Fitted(const SkBitmap& bitmap) {
  const int longest = std::max(bitmap.width(), bitmap.height());
  if (longest <= kFaviconPixels) {
    return bitmap;
  }
  const double scale = static_cast<double>(kFaviconPixels) / longest;
  const int width =
      std::max(1, static_cast<int>(std::lround(bitmap.width() * scale)));
  const int height =
      std::max(1, static_cast<int>(std::lround(bitmap.height() * scale)));
  return skia::ImageOperations::Resize(
      bitmap, skia::ImageOperations::RESIZE_BEST, width, height);
}

// A copy of `image`'s pixels nearest the size it is saved at, for a thread
// that doesn't share the image's.
SkBitmap BitmapToSave(const gfx::ImageSkia& image) {
  const std::vector<gfx::ImageSkiaRep> reps = image.image_reps();
  const gfx::ImageSkiaRep* best = nullptr;
  for (const gfx::ImageSkiaRep& rep : reps) {
    const int longest = std::max(rep.pixel_width(), rep.pixel_height());
    const int best_longest =
        best ? std::max(best->pixel_width(), best->pixel_height()) : 0;
    // The largest that fits, else the smallest above it.
    if (!best ||
        (longest <= kFaviconPixels
             ? longest > best_longest || best_longest > kFaviconPixels
             : best_longest > kFaviconPixels && longest < best_longest)) {
      best = &rep;
    }
  }
  const SkBitmap& source =
      best ? best->GetBitmap() : image.GetRepresentation(2.0f).GetBitmap();
  SkBitmap copy;
  if (source.drawsNothing() || !copy.tryAllocPixels(source.info()) ||
      !source.readPixels(copy.pixmap())) {
    return SkBitmap();
  }
  copy.setImmutable();
  return copy;
}

}  // namespace

SkBitmap DecodeFaviconDataURL(std::string_view data_url) {
  if (!base::StartsWith(data_url, kPNGDataURLPrefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return SkBitmap();
  }
  const std::optional<std::vector<uint8_t>> png =
      base::Base64Decode(data_url.substr(kPNGDataURLPrefix.size()));
  if (!png) {
    return SkBitmap();
  }
  const SkBitmap bitmap = gfx::PNGCodec::Decode(*png);
  if (bitmap.drawsNothing()) {
    return SkBitmap();
  }
  SkBitmap fitted = Fitted(bitmap);
  fitted.setImmutable();
  return fitted;
}

std::string FaviconDataURL(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    return std::string();
  }
  const std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(Fitted(bitmap),
                                        /*discard_transparency=*/false);
  if (!png) {
    return std::string();
  }
  return base::StrCat({kPNGDataURLPrefix, base::Base64Encode(*png)});
}

gfx::ImageSkia FaviconImage(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    return gfx::ImageSkia();
  }
  const float scale =
      static_cast<float>(std::max(bitmap.width(), bitmap.height())) /
      kFaviconPoints;
  return gfx::ImageSkia::CreateFromBitmap(bitmap, scale);
}

Favicons::Entry::Entry() = default;
Favicons::Entry::Entry(const Entry&) = default;
Favicons::Entry& Favicons::Entry::operator=(const Entry&) = default;
Favicons::Entry::~Entry() = default;

Favicons::Favicons() : entries_(512) {}

Favicons::~Favicons() = default;

void Favicons::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void Favicons::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

gfx::ImageSkia Favicons::Image(const std::string& tab_id,
                               const std::string& data_url) {
  const auto found = entries_.Get(tab_id);
  if (found != entries_.end() && found->second.from_page) {
    return found->second.image;
  }
  if (data_url.empty()) {
    return gfx::ImageSkia();
  }
  if (found != entries_.end() && found->second.data_url == data_url) {
    return found->second.image;
  }
  Decode(tab_id, data_url);
  // While a newer one decodes, the tab's last stays.
  return found != entries_.end() ? found->second.image : gfx::ImageSkia();
}

void Favicons::Warm(
    const std::vector<std::pair<std::string, std::string>>& tabs) {
  for (const auto& [tab_id, data_url] : tabs) {
    const auto found = entries_.Peek(tab_id);
    if (!data_url.empty() &&
        (found == entries_.end() || found->second.data_url != data_url)) {
      Decode(tab_id, data_url);
    }
  }
}

void Favicons::Update(
    const std::string& tab_id,
    const gfx::Image& image,
    base::OnceCallback<void(const std::string& data_url)> encoded) {
  if (image.IsEmpty()) {
    return;
  }
  Entry entry;
  entry.image = image.AsImageSkia();
  entry.from_page = true;
  const SkBitmap bitmap = BitmapToSave(entry.image);
  entries_.Put(tab_id, std::move(entry));
  // A decode under way is older than what the page sent.
  decoding_.erase(tab_id);

  const uint64_t update = ++last_update_;
  updates_[tab_id] = update;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&FaviconDataURL, bitmap),
      base::BindOnce(&Favicons::Encoded, weak_factory_.GetWeakPtr(), tab_id,
                     update, std::move(encoded)));
}

void Favicons::Clear() {
  weak_factory_.InvalidateWeakPtrs();
  entries_.Clear();
  decoding_.clear();
  updates_.clear();
}

void Favicons::Decode(const std::string& tab_id, const std::string& data_url) {
  const auto under_way = decoding_.find(tab_id);
  if (under_way != decoding_.end() && under_way->second == data_url) {
    return;
  }
  decoding_[tab_id] = data_url;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](const std::string& data_url) {
            return DecodeFaviconDataURL(data_url);
          },
          data_url),
      base::BindOnce(&Favicons::Decoded, weak_factory_.GetWeakPtr(), tab_id,
                     data_url));
}

void Favicons::Decoded(const std::string& tab_id,
                       const std::string& data_url,
                       SkBitmap bitmap) {
  const auto under_way = decoding_.find(tab_id);
  // A newer one started, or the page sent its own meanwhile.
  if (under_way == decoding_.end() || under_way->second != data_url) {
    return;
  }
  decoding_.erase(under_way);
  Entry entry;
  entry.data_url = data_url;
  // One that doesn't decode draws the placeholder, and isn't tried again.
  entry.image = FaviconImage(bitmap);
  entries_.Put(tab_id, std::move(entry));
  for (Observer& observer : observers_) {
    observer.OnFaviconChanged(tab_id);
  }
}

void Favicons::Encoded(const std::string& tab_id,
                       uint64_t update,
                       base::OnceCallback<void(const std::string&)> encoded,
                       std::string data_url) {
  const auto latest = updates_.find(tab_id);
  if (latest == updates_.end() || latest->second != update) {
    return;
  }
  updates_.erase(latest);
  if (data_url.empty()) {
    return;
  }
  // From now on the tab's saved data URL is this image's.
  if (const auto found = entries_.Peek(tab_id);
      found != entries_.end() && found->second.from_page) {
    found->second.data_url = data_url;
    found->second.from_page = false;
  }
  std::move(encoded).Run(data_url);
}

}  // namespace pane
