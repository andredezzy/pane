// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_SIDEBAR_VIEW_H_
#define PANE_UI_SIDEBAR_SIDEBAR_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class FadingScrollView;
class IconButton;
class ProfileRow;
class SidebarButton;
class TabRow;
class ValueMotion;

// The sidebar's column (the Swift's SidebarView): its header in the traffic
// lights' row (the title, Collapse all), then each profile and, while it is
// open, its tabs and New tab, in a list that fades at its edges as it scrolls
// (ADR 0061) and scrolls the selected tab's row into view wherever it was
// chosen (ADR 0084), then its footer (New, a rule, Settings). Rows keep their
// views by profile and tab, so what moves keeps moving (a profile's bar,
// mid-morph), and only a row whose value changed redraws (ADR 0073).
class SidebarView : public views::View {
  METADATA_HEADER(SidebarView, views::View)

 public:
  // What the sidebar's presses ask of the app.
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // A profile's header: it opens, or collapsing unloads it.
    base::RepeatingCallback<void(const std::string& profile_id)> toggle_profile;
    base::RepeatingCallback<void(const std::string& tab_id)> select_tab;
    base::RepeatingCallback<void(const std::string& tab_id)> close_tab;
    base::RepeatingCallback<void(const std::string& profile_id)> new_tab;
    base::RepeatingCallback<void(const std::string& profile_id)> edit_profile;
    base::RepeatingCallback<void(const std::string& profile_id, bool)>
        set_keep_loaded;
    // Once the alert's Delete is chosen.
    base::RepeatingCallback<void(const std::string& profile_id)> delete_profile;
    base::RepeatingClosure collapse_all;
    base::RepeatingClosure new_profile;
    base::RepeatingClosure toggle_settings;
  };

  explicit SidebarView(Callbacks callbacks);
  SidebarView(const SidebarView&) = delete;
  SidebarView& operator=(const SidebarView&) = delete;
  ~SidebarView() override;

  void SetContent(const SidebarContent& content);
  // Some profile is expanded.
  void SetCanCollapseAll(bool can_collapse_all);

  // The header, the traffic lights' row, which moves the window.
  views::View* header() { return header_; }
  // The rows, each by what the channel names it ("pane.profile.<id>",
  // "pane.tab.<id>"), and the list's visible frame ("pane.sidebar.profiles"),
  // for a scenario's read of where each is.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;
  // A row's menu, for the channel.
  ProfileRow* FindProfileRow(const std::string& profile_id);
  TabRow* FindTabRow(const std::string& tab_id);

  // Asks before deleting the profile (the system's alert, a sheet on the
  // window), and deletes it on Delete.
  void AskToDelete(const std::string& profile_id);

  // The list's fades now, for a scenario's read.
  int top_fade() const;
  int bottom_fade() const;

  // views::View:
  void Layout(PassKey) override;

 private:
  class RowList;

  // Rows came, went, or moved: each keeps its view by its key.
  void SetRows(const SidebarContent& content);
  std::unique_ptr<views::View> MakeRow(const std::string& key,
                                       const SidebarProfile& profile,
                                       const SidebarTab* tab);
  // The selected tab's row into view (ADR 0084): the least scroll that shows
  // it whole and clear of the edges' fades, none when it shows; a turn later,
  // so a profile that opened for it has laid its rows out.
  void RevealSelected(bool animated);
  void Revealed(double offset);

  Callbacks callbacks_;
  SidebarContent content_;
  raw_ptr<views::View> header_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<IconButton> collapse_all_ = nullptr;
  raw_ptr<FadingScrollView> list_ = nullptr;
  raw_ptr<RowList> rows_ = nullptr;
  raw_ptr<views::View> footer_ = nullptr;
  raw_ptr<SidebarButton> new_ = nullptr;
  raw_ptr<views::View> rule_ = nullptr;
  raw_ptr<SidebarButton> settings_ = nullptr;
  // Each row's key ("p:<profile>", "t:<tab>", "n:<profile>" for New tab) in
  // the list's order.
  std::vector<std::string> keys_;
  // The tab whose row was last revealed: a new one is revealed.
  std::optional<std::string> selected_tab_;
  bool revealed_once_ = false;
  // The scroll to a revealed row, on the spring Motion.reveal, and the offset
  // it last set: one that moved otherwise (the user's scroll) ends it.
  std::unique_ptr<ValueMotion> reveal_;
  double revealed_offset_ = 0;
  base::WeakPtrFactory<SidebarView> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_SIDEBAR_VIEW_H_
