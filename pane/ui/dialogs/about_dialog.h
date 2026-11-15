// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_ABOUT_DIALOG_H_
#define PANE_UI_DIALOGS_ABOUT_DIALOG_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class KeyedButton;

// About Pane where Pane draws it (Windows, Paper 11.15 A), over the window
// as Exit Pane? is: the App icon's sky at 64 points as the taskbar shows it,
// "Pane", its version, and the copyright, 4 apart, the copyright 8 further
// down; 16 under the icon, and OK (Enter) 16 and 8 under them, at the right.
// The copyright is the engine's (pane/BRANDING, which the files' properties
// show too), the line the Mac's panel shows (scripts/package.sh): one on both
// systems, the user's call (2026-11-14). A screen reader hears a dialog named
// "About Pane" (RootView).
class AboutDialog : public views::View {
  METADATA_HEADER(AboutDialog, views::View)

 public:
  // The dialog's width, its content 24 in.
  static constexpr int kWidth = 400;

  AboutDialog(const std::u16string& version, base::RepeatingClosure ok);
  AboutDialog(const AboutDialog&) = delete;
  AboutDialog& operator=(const AboutDialog&) = delete;
  ~AboutDialog() override;

  // The sky the taskbar shows, at the time it shows.
  void SetIcon(const gfx::ImageSkia& icon);
  // Its lines, top to bottom, for the channel.
  std::vector<std::u16string> Lines() const;
  // Whether it shows an icon.
  bool has_icon() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  class Icon;

  int HeightForWidth(int width) const;

  raw_ptr<Icon> icon_ = nullptr;
  raw_ptr<views::Label> name_ = nullptr;
  raw_ptr<views::Label> version_ = nullptr;
  raw_ptr<views::Label> copyright_ = nullptr;
  raw_ptr<KeyedButton> ok_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_DIALOGS_ABOUT_DIALOG_H_
