// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_TABS_FAVICONS_H_
#define PANE_UI_TABS_FAVICONS_H_

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/lru_cache.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image_skia.h"

namespace base {
class SequencedTaskRunner;
}

namespace gfx {
class Image;
}

namespace pane {

// A tab's favicon is stored as a PNG data: URL in profiles.json, at most this
// many pixels a side: the 16 points it draws at, at 2× (ADR 0068).
inline constexpr int kFaviconPixels = 32;
inline constexpr int kFaviconPoints = 16;

// "data:image/png;base64,…" to its bitmap, at most kFaviconPixels a side;
// empty when it doesn't decode.
SkBitmap DecodeFaviconDataURL(std::string_view data_url);
// `bitmap` as a tab saves it: a PNG data: URL at most kFaviconPixels a side,
// scaled down to fit; empty when it won't encode.
std::string FaviconDataURL(const SkBitmap& bitmap);
// What a row draws from a bitmap of at most kFaviconPixels: 16 points, at the
// scale its pixels make.
gfx::ImageSkia FaviconImage(const SkBitmap& bitmap);

// Each tab's favicon as the sidebar (and the switcher) draws it (ADR 0072):
// the image the page's favicon driver sends, drawn as it comes, or the one
// saved with the tab, decoded once off the UI thread. A tab keeps its last
// image while a newer one decodes or encodes, and a row compares its tab's
// data URL with the one decoded, the same string, so at once. The page's
// image is encoded for its tab's save off the UI thread too, and only for it:
// the row never decodes what the page sent. 512 tabs are kept, the least
// recently drawn going first. On the UI thread.
class Favicons {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // `tab_id`'s image came: a decode finished.
    virtual void OnFaviconChanged(const std::string& tab_id) = 0;
  };

  Favicons();
  Favicons(const Favicons&) = delete;
  Favicons& operator=(const Favicons&) = delete;
  ~Favicons();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // What to draw for `tab_id`, whose saved favicon is `data_url`: the page's
  // latest, the decoded one, or the last one while a newer decodes; null for
  // none, which draws the placeholder. A data URL not yet decoded starts its
  // decode, which the observers hear of.
  gfx::ImageSkia Image(const std::string& tab_id, const std::string& data_url);

  // Decodes the tabs' saved favicons, (tab id, data URL) each, ahead of their
  // rows, which show only once their profile expands.
  void Warm(const std::vector<std::pair<std::string, std::string>>& tabs);

  // The page's favicon as it comes: drawn from now on, and `encoded` gets the
  // data URL its tab saves, made off the UI thread, unless a newer one came
  // meanwhile. Empty when it won't encode.
  void Update(const std::string& tab_id,
              const gfx::Image& image,
              base::OnceCallback<void(const std::string& data_url)> encoded);

  // Pane locked (ADR 0116): what the profiles' pages showed leaves memory with
  // them, decodes and encodes under way with it.
  void Clear();

 private:
  struct Entry {
    Entry();
    Entry(const Entry&);
    Entry& operator=(const Entry&);
    ~Entry();

    // What `image` is for: the decoded data URL, or the page's, once encoded.
    std::string data_url;
    gfx::ImageSkia image;
    // The page sent it and its data URL isn't known yet: it's drawn whatever
    // the tab's saved one.
    bool from_page = false;
  };

  void Decode(const std::string& tab_id, const std::string& data_url);
  void Decoded(const std::string& tab_id,
               const std::string& data_url,
               SkBitmap bitmap);
  void Encoded(const std::string& tab_id,
               uint64_t update,
               base::OnceCallback<void(const std::string&)> encoded,
               std::string data_url);

  base::LRUCache<std::string, Entry> entries_;
  // Each decode under way, by tab: a newer one's data URL takes its place.
  std::map<std::string, std::string> decoding_;
  // Each tab's latest page image, numbered, while its encode runs.
  std::map<std::string, uint64_t> updates_;
  uint64_t last_update_ = 0;
  const scoped_refptr<base::SequencedTaskRunner> runner_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<Favicons> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_TABS_FAVICONS_H_
