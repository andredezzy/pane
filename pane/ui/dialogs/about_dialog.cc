// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/about_dialog.h"

#include <memory>
#include <utility>

#include "base/strings/strcat.h"
#include "cc/paint/paint_flags.h"
#include "chrome/common/chrome_version.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/squircle.h"
#include "pane/ui/design/system_words.h"
#include "pane/ui/design/text.h"
#include "pane/ui/design/text_button.h"
#include "pane/ui/view_ids.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// The content, inside the card's 24 of padding.
constexpr int kContentWidth = AboutDialog::kWidth - 2 * 24;
// The icon, and the spacing of the card's column.
constexpr int kIconSize = 64;
constexpr int kSpacing = 16;
// The name and the version, 4 apart, the copyright 8 further down; the
// buttons' row 8 further down than the column's spacing.
constexpr TextStyle kName{18, weight::kSemibold, 24};
constexpr TextStyle kVersion{14, weight::kRegular, 20};
constexpr TextStyle kCopyright{12, weight::kRegular, 16};
constexpr int kLineSpacing = 4;
constexpr int kCopyrightTop = 8;
constexpr int kButtonsTop = 8;

// The App icon's body in its image, as macOS draws an app's icon on its
// 1024-pixel canvas (packaging/icon/make-icons.swift): 824 pixels square in
// its middle, its corners 185.4 pixels.
constexpr float kBodyInset = 100.0f / 1024;
constexpr float kBodyRadius = 185.4f / 1024;

}  // namespace

// The sky, scaled to 64 points from the taskbar's image; under a contrast
// theme its colors stay, edged where its body ends (Paper 11.15 A).
class AboutDialog::Icon : public views::View {
  METADATA_HEADER(Icon, views::View)

 public:
  Icon() { GetViewAccessibility().SetIsIgnored(true); }

  void SetImage(const gfx::ImageSkia& image) {
    image_ = image;
    SchedulePaint();
  }
  const gfx::ImageSkia& image() const { return image_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    if (!image_.isNull()) {
      canvas->DrawImageInt(image_, 0, 0, image_.width(), image_.height(), 0, 0,
                           width(), height(), /*filter=*/true);
    }
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    if (const SkColor edge = colors->GetColor(kPaneColorForcedEdge);
        SkColorGetA(edge)) {
      gfx::RectF body(GetLocalBounds());
      body.Inset(kBodyInset * width());
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1);
      flags.setColor(edge);
      canvas->DrawPath(SquircleStrokePath(body, kBodyRadius * width(), 1),
                       flags);
    }
  }

 private:
  gfx::ImageSkia image_;
};

BEGIN_METADATA(AboutDialog, Icon)
END_METADATA

AboutDialog::AboutDialog(const std::u16string& version,
                         base::RepeatingClosure ok) {
  icon_ = AddChildView(std::make_unique<Icon>());
  name_ = AddChildView(MakeLabel(u"Pane", kName, kPaneColorForeground));
  version_ = AddChildView(MakeLabel(base::StrCat({u"Version ", version}),
                                    kVersion, kPaneColorDialogMessage));
  // The engine's line, as the files' properties show it.
  copyright_ = AddChildView(MakeLabel(std::u16string(u"" COPYRIGHT_STRING),
                                      kCopyright, kPaneColorMutedLifted));
  copyright_->SetMultiLine(true);
  for (views::Label* label : {name_.get(), version_.get(), copyright_.get()}) {
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // Read in the dialog, which holds the keys.
    ExposeToScreenReaders(*label);
  }
  ok_ = AddChildView(std::make_unique<KeyedButton>(
      std::move(ok), u"OK", std::u16string(Words().return_key),
      KeyedButton::Role::kPrimary));
  ok_->SetID(kViewIdAboutOk);
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::SetIcon(const gfx::ImageSkia& icon) {
  icon_->SetImage(icon);
}

std::vector<std::u16string> AboutDialog::Lines() const {
  return {std::u16string(name_->GetText()), std::u16string(version_->GetText()),
          std::u16string(copyright_->GetText())};
}

bool AboutDialog::has_icon() const {
  return !icon_->image().isNull();
}

int AboutDialog::HeightForWidth(int width) const {
  return kIconSize + kSpacing + kName.line_height + kLineSpacing +
         kVersion.line_height + kLineSpacing + kCopyrightTop +
         copyright_->GetHeightForWidth(width) + kSpacing + kButtonsTop +
         ok_->GetPreferredSize().height();
}

gfx::Size AboutDialog::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  const int width = available_size.width().is_bounded()
                        ? available_size.width().value()
                        : kContentWidth;
  return gfx::Size(width, HeightForWidth(width));
}

void AboutDialog::Layout(PassKey) {
  icon_->SetBounds(0, 0, kIconSize, kIconSize);
  int y = kIconSize + kSpacing;
  name_->SetBounds(0, y, width(), kName.line_height);
  y += kName.line_height + kLineSpacing;
  version_->SetBounds(0, y, width(), kVersion.line_height);
  y += kVersion.line_height + kLineSpacing + kCopyrightTop;
  const int copyright = copyright_->GetHeightForWidth(width());
  copyright_->SetBounds(0, y, width(), copyright);
  y += copyright + kSpacing + kButtonsTop;
  const gfx::Size ok = ok_->GetPreferredSize();
  ok_->SetBounds(width() - ok.width(), y, ok.width(), ok.height());
}

BEGIN_METADATA(AboutDialog)
END_METADATA

}  // namespace pane
