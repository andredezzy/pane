// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/tabs/favicons.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace pane {
namespace {

// A bitmap of one color, `width` by `height` pixels.
SkBitmap Filled(int width, int height, SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height);
  bitmap.eraseColor(color);
  return bitmap;
}

// The color at the middle, as its decode reads it.
SkColor Middle(const SkBitmap& bitmap) {
  return bitmap.getColor(bitmap.width() / 2, bitmap.height() / 2);
}

class Recorder : public Favicons::Observer {
 public:
  void OnFaviconChanged(const std::string& tab_id) override {
    changed.push_back(tab_id);
  }

  std::vector<std::string> changed;
};

// A favicon saved as a PNG data: URL decodes to the same pixels.
TEST(FaviconsTest, ADataURLDecodesToWhatWasSaved) {
  const std::string data_url = FaviconDataURL(Filled(32, 32, SK_ColorRED));
  EXPECT_TRUE(data_url.starts_with("data:image/png;base64,"));

  const SkBitmap decoded = DecodeFaviconDataURL(data_url);
  EXPECT_EQ(decoded.width(), 32);
  EXPECT_EQ(decoded.height(), 32);
  EXPECT_EQ(Middle(decoded), SK_ColorRED);
}

// A site's large icon is saved at most 32 pixels a side, its shape kept, as
// the Swift's pngDataURL did.
TEST(FaviconsTest, ALargeIconIsSavedAt32PixelsAtMost) {
  const SkBitmap decoded =
      DecodeFaviconDataURL(FaviconDataURL(Filled(180, 120, SK_ColorBLUE)));
  EXPECT_EQ(decoded.width(), 32);
  EXPECT_EQ(decoded.height(), 21);

  const SkBitmap small =
      DecodeFaviconDataURL(FaviconDataURL(Filled(16, 16, SK_ColorGREEN)));
  EXPECT_EQ(small.width(), 16);
}

// What isn't a PNG data: URL decodes to nothing: the row draws the globe.
TEST(FaviconsTest, WhatIsNoPNGDataURLDecodesToNothing) {
  EXPECT_TRUE(DecodeFaviconDataURL("").drawsNothing());
  EXPECT_TRUE(
      DecodeFaviconDataURL("https://example.com/favicon.ico").drawsNothing());
  EXPECT_TRUE(DecodeFaviconDataURL("data:image/png;base64,***").drawsNothing());
  EXPECT_TRUE(
      DecodeFaviconDataURL("data:image/png;base64,aGVsbG8=").drawsNothing());
  EXPECT_TRUE(FaviconDataURL(SkBitmap()).empty());
}

// A row draws 16 points: 32 pixels are its 2× pixels, 16 its 1×.
TEST(FaviconsTest, AnImageIsSixteenPointsAtThePixelsScale) {
  const gfx::ImageSkia retina = FaviconImage(Filled(32, 32, SK_ColorRED));
  EXPECT_EQ(retina.width(), 16);
  EXPECT_TRUE(retina.HasRepresentation(2.0f));

  const gfx::ImageSkia standard = FaviconImage(Filled(16, 16, SK_ColorRED));
  EXPECT_EQ(standard.width(), 16);
  EXPECT_TRUE(standard.HasRepresentation(1.0f));

  EXPECT_TRUE(FaviconImage(SkBitmap()).isNull());
}

// A saved favicon decodes off the UI thread once: the row draws nothing until
// then, hears when it comes, and draws it from then on at once. A tab with no
// favicon draws the placeholder.
TEST(FaviconsTest, ASavedFaviconDecodesOnceAndTheRowHearsOfIt) {
  base::test::TaskEnvironment environment;
  Favicons favicons;
  Recorder recorder;
  favicons.AddObserver(&recorder);
  const std::string data_url = FaviconDataURL(Filled(32, 32, SK_ColorRED));

  EXPECT_TRUE(favicons.Image("tab", data_url).isNull());
  EXPECT_TRUE(favicons.Image("tab", data_url).isNull());
  environment.RunUntilIdle();
  EXPECT_EQ(recorder.changed, std::vector<std::string>{"tab"});

  const gfx::ImageSkia image = favicons.Image("tab", data_url);
  ASSERT_FALSE(image.isNull());
  EXPECT_EQ(Middle(*image.bitmap()), SK_ColorRED);
  EXPECT_TRUE(favicons.Image("none", std::string()).isNull());
  favicons.RemoveObserver(&recorder);
}

// A changed favicon keeps the last one drawn until the new one decodes.
TEST(FaviconsTest, AChangedFaviconKeepsTheLastUntilTheNewOneDecodes) {
  base::test::TaskEnvironment environment;
  Favicons favicons;
  const std::string red = FaviconDataURL(Filled(32, 32, SK_ColorRED));
  const std::string blue = FaviconDataURL(Filled(32, 32, SK_ColorBLUE));
  favicons.Warm({{"tab", red}});
  environment.RunUntilIdle();

  const gfx::ImageSkia last = favicons.Image("tab", blue);
  ASSERT_FALSE(last.isNull());
  EXPECT_EQ(Middle(*last.bitmap()), SK_ColorRED);
  environment.RunUntilIdle();
  EXPECT_EQ(Middle(*favicons.Image("tab", blue).bitmap()), SK_ColorBLUE);
}

// The page's favicon draws as it comes, whatever the tab saved, and is
// encoded off the UI thread for the save; once saved, the row's data URL is
// the one drawn, with nothing decoded.
TEST(FaviconsTest, ThePagesFaviconDrawsAtOnceAndIsEncodedToSave) {
  base::test::TaskEnvironment environment;
  Favicons favicons;
  const gfx::Image page(
      gfx::ImageSkia::CreateFromBitmap(Filled(32, 32, SK_ColorGREEN), 2.0f));

  base::test::TestFuture<const std::string&> encoded;
  favicons.Update("tab", page, encoded.GetCallback());
  ASSERT_FALSE(favicons.Image("tab", "data:image/png;base64,old").isNull());
  EXPECT_EQ(Middle(*favicons.Image("tab", std::string()).bitmap()),
            SK_ColorGREEN);

  const std::string data_url = encoded.Get();
  EXPECT_EQ(Middle(DecodeFaviconDataURL(data_url)), SK_ColorGREEN);
  EXPECT_TRUE(
      favicons.Image("tab", data_url).BackedBySameObjectAs(page.AsImageSkia()));
}

// A favicon that changes again before the first is encoded saves only the
// latest.
TEST(FaviconsTest, OnlyTheLatestOfThePagesFaviconsIsSaved) {
  base::test::TaskEnvironment environment;
  Favicons favicons;
  std::vector<std::string> saved;
  for (SkColor color : {SK_ColorRED, SK_ColorBLUE}) {
    favicons.Update(
        "tab",
        gfx::Image(gfx::ImageSkia::CreateFromBitmap(Filled(16, 16, color), 1)),
        base::BindOnce(
            [](std::vector<std::string>* saved, const std::string& data_url) {
              saved->push_back(data_url);
            },
            &saved));
  }
  environment.RunUntilIdle();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(Middle(DecodeFaviconDataURL(saved[0])), SK_ColorBLUE);
}

}  // namespace
}  // namespace pane
