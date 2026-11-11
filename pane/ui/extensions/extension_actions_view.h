// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_EXTENSION_ACTIONS_VIEW_H_
#define PANE_UI_EXTENSIONS_EXTENSION_ACTIONS_VIEW_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "pane/ui/extensions/installed_extensions.h"
#include "ui/views/view.h"

namespace pane {

class ExtensionsUi;
class IconButton;

// The shown profile's extensions in the toolbar (the Swift's
// ExtensionActionsView, ADR 0054, Paper 03.9 and 04.6), in the place the
// toolbar keeps for them (H4): its pinned actions, then the puzzle, 4 apart.
// As Aside's, the pinned actions sit in a capsule, 32-point circles 4 apart
// and flush inside its 1-point border, which shows only while the pointer is
// over them (fading as a hover does) or one's popup shows. An action is
// lit while its popup shows and while its right-click menu does; the puzzle,
// while the menu shows. Each reads its action's title, else its name.
class ExtensionActionsView : public views::View {
  METADATA_HEADER(ExtensionActionsView, views::View)

 public:
  // As tall as the capsule, the puzzle centered beside it.
  static constexpr int kHeight = 34;

  struct Shows {
    Shows();
    Shows(const Shows&);
    Shows& operator=(const Shows&);
    ~Shows();

    // The pinned actions, in the order of their names (the Swift's).
    std::vector<InstalledExtension> pinned;
    // The extension whose popup shows, and whose right-click menu does.
    std::optional<std::string> popup;
    std::optional<std::string> menu_action;
    // The puzzle's menu shows.
    bool menu_open = false;
  };

  explicit ExtensionActionsView(ExtensionsUi& ui);
  ExtensionActionsView(const ExtensionActionsView&) = delete;
  ExtensionActionsView& operator=(const ExtensionActionsView&) = delete;
  ~ExtensionActionsView() override;

  void SetShows(const Shows& shows);
  views::View* puzzle();
  // A pinned action's button, if it shows.
  views::View* ActionButtonFor(const std::string& extension_id);
  // The controls, by the names the channel reads them by: the puzzle
  // ("pane.extensions"), the capsule, and each action
  // ("pane.extension.<id>").
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  class ActionButton;
  class Capsule;

  const raw_ref<ExtensionsUi> ui_;
  raw_ptr<Capsule> capsule_ = nullptr;
  raw_ptr<IconButton> puzzle_ = nullptr;
  std::vector<raw_ptr<ActionButton>> buttons_;
  bool menu_open_ = false;
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_EXTENSION_ACTIONS_VIEW_H_
