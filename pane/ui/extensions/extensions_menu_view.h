// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_EXTENSIONS_MENU_VIEW_H_
#define PANE_UI_EXTENSIONS_EXTENSIONS_MENU_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "pane/ui/design/dialog.h"
#include "pane/ui/extensions/installed_extensions.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class ExtensionsUi;
// A row of either menu, 32 tall (in the .cc).
class ExtensionsMenuRow;

// The extensions menu, as Aside's (the Swift's ExtensionsMenu; Paper 03.9 and
// 04.6; ADR 0054): 300 wide under the toolbar's puzzle, on the popovers'
// surface (rounded 22, a 10% ring, their shadow). Manage all extensions and
// Visit Chrome Web Store; a rule; then Installed extensions with the shown
// profile's enabled extensions, or No installed extensions. Rows are 32
// tall, a 16-point icon and a 13-point title 8 apart, 8 in, the accent
// filling a row under the pointer (rounded 15) and while its more menu
// shows; an extension's row runs its action and holds its pin (blue while
// pinned) and more, 24 points each. A row without an action only shows.
class ExtensionsMenuView : public views::View {
  METADATA_HEADER(ExtensionsMenuView, views::View)

 public:
  static constexpr int kWidth = 300;

  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    // The shown profile's enabled extensions, by name.
    std::vector<InstalledExtension> extensions;
    // The extension whose more menu shows beside its row.
    std::optional<std::string> more;
  };

  // The menu's surface.
  static DialogCard::Look CardLook();

  explicit ExtensionsMenuView(ExtensionsUi& ui);
  ExtensionsMenuView(const ExtensionsMenuView&) = delete;
  ExtensionsMenuView& operator=(const ExtensionsMenuView&) = delete;
  ~ExtensionsMenuView() override;

  void SetShows(const Shows& shows);
  // An extension's row, in this view: its more menu stands level with it.
  std::optional<gfx::Rect> RowBounds(const std::string& extension_id) const;
  // The rows and their controls, by the Swift's names
  // ("pane.extensionsMenu.pin.<id>").
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  // Where the rule between the groups is.
  int RuleY() const;

  const raw_ref<ExtensionsUi> ui_;
  raw_ptr<ExtensionsMenuRow> manage_ = nullptr;
  raw_ptr<ExtensionsMenuRow> web_store_ = nullptr;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<views::Label> empty_ = nullptr;
  std::vector<raw_ptr<ExtensionsMenuRow>> rows_;
};

// An extension's more menu, as Aside's (the Swift's ExtensionMoreMenu; Paper
// 03.10 and 04.9): 240 wide beside the extensions menu, over its edge by 10,
// level with the row, on the same surface. Its name, then its Web Store
// page; Remove from Pane, and Pin or Unpin for an extension with an action;
// Manage Extension, and Options when it has a page; a rule between each.
class ExtensionMoreMenuView : public views::View {
  METADATA_HEADER(ExtensionMoreMenuView, views::View)

 public:
  static constexpr int kWidth = 240;
  // Over the extensions menu's edge.
  static constexpr int kOverlap = 10;

  explicit ExtensionMoreMenuView(ExtensionsUi& ui);
  ExtensionMoreMenuView(const ExtensionMoreMenuView&) = delete;
  ExtensionMoreMenuView& operator=(const ExtensionMoreMenuView&) = delete;
  ~ExtensionMoreMenuView() override;

  void SetExtension(const InstalledExtension& extension);
  const std::string& extension_id() const { return extension_.id; }
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  // The rules' places, under each group but the last.
  std::vector<int> RuleYs() const;

  const raw_ref<ExtensionsUi> ui_;
  InstalledExtension extension_;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<ExtensionsMenuRow> store_page_ = nullptr;
  raw_ptr<ExtensionsMenuRow> remove_ = nullptr;
  raw_ptr<ExtensionsMenuRow> pin_ = nullptr;
  raw_ptr<ExtensionsMenuRow> manage_ = nullptr;
  raw_ptr<ExtensionsMenuRow> options_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_EXTENSIONS_MENU_VIEW_H_
