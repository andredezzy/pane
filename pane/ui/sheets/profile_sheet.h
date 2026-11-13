// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SHEETS_PROFILE_SHEET_H_
#define PANE_UI_SHEETS_PROFILE_SHEET_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/model/profile.h"
#include "pane/model/profile_draft.h"
#include "pane/model/time_zones.h"
#include "pane/ui/design/motion.h"
#include "ui/views/view.h"

namespace views {
class ScrollView;
class Textfield;
}  // namespace views

namespace pane {

class Field;
class IconButton;
class MenuButton;
class ProxyTest;
class Switch;
class TextButton;

// Create or edit a profile (the Swift's ProfileSheet; Paper 05.4 to 05.11;
// ADR 0120), in the 400-point panel the window shows at its right
// (RootView::ShowProfileSheet): the title and a close X, then, in a body
// that scrolls between two rules, the name and the color, then Privacy,
// Fingerprint, and Network in cards, then Cancel and Create (Save for an
// edit). The sheet shows no Group (N1, the user's answer of 2026-11-11): a
// saved one stays as it is.
//
// The form is model::ProfileDraft, as the model checks and applies it.
// Create with a problem saves nothing: the field that keeps it from saving is
// ringed in red, with no words under it (memory field-problem-red-ring), and
// brought into view, its words for the screen reader alone; the ring stays
// until the field can save. A proxy string pasted, or typed and left, in the
// host fills the address and credentials. Test connection fetches the proxy
// test's echo through the proxy as filled in; its answer is about that
// proxy, and goes as another is typed. What a switch shows or hides, and the
// warning another platform than the computer's brings, comes and goes with
// Motion.toggle, and a note that changes crossfades (Motion.status).
class ProfileSheet : public views::View {
  METADATA_HEADER(ProfileSheet, views::View)

 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // Create or Save, with the form as filled in.
    base::RepeatingCallback<void(const model::ProfileDraft& draft)> save;
    // Cancel, the X, Esc, and a press beside the sheet; and once saved.
    base::RepeatingClosure close;
  };

  // The sheet's width.
  static constexpr int kWidth = 400;

  explicit ProfileSheet(Callbacks callbacks);
  ProfileSheet(const ProfileSheet&) = delete;
  ProfileSheet& operator=(const ProfileSheet&) = delete;
  ~ProfileSheet() override;

  // The form for a new profile, or for `profile` as it is, at the body's
  // top, with no problem and no test. The zones' menu is listed again, off
  // the UI thread, for today's offsets.
  void Begin(const model::BrowserProfile* profile);
  // nullopt for a new profile.
  // The name field takes the keys, its text selected.
  void FocusName();
  // Esc, the X, Cancel.
  void Cancel();
  // Return, Create or Save: saved and closed, or the field with the first
  // problem ringed and brought into view.
  void Submit();

  // Its controls by the Swift's names ("pane.profile.name",
  // "pane.profile.platform.windows"), each that shows, for the channel.
  std::vector<std::pair<std::string, const views::View*>> NamedViews() const;
  // A menu by its name ("timeZone", "proxyType"), to read and choose in.
  MenuButton* FindMenu(std::string_view name);
  // A field by its name ("name", "proxyHost", "proxyPort", "proxyUsername",
  // "proxyPassword").
  views::Textfield* FindField(std::string_view name);

  // views::View:
  void Layout(PassKey) override;

 private:
  class AddressField;
  class Hairline;
  class PlatformTile;
  class Reveal;
  class RowLabels;
  class Swatch;
  class TestStatus;
  class TileRow;

  // What the form shows, from the draft: each control, the notes, what is
  // revealed, the rings. `animate`: as the user's change shows it.
  void Show(bool animate);
  // The draft changed by an edit: a problem's ring stays until its field can
  // save, and a test's answer goes once the proxy is another.
  void Edited();
  void TestProxy();
  // Ends the test's interval at the frame that shows its answer.
  void EndTestInterval();
  void Tested(const model::ProxyConfig& tested,
              std::optional<std::string> ip,
              std::string error);
  // The zones' menu, once they are listed.
  void ZonesListed(std::vector<model::TimeZoneRegion> regions);
  void ChooseZone(size_t index);
  // The field with the problem comes into view, the rules' 12 clear of it
  // (Motion.reveal).
  void RevealProblem();
  std::u16string HistoryNote() const;
  std::u16string SpoofNote() const;
  std::u16string ZoneNote() const;

  Callbacks callbacks_;
  model::ProfileDraft draft_;
  std::optional<std::string> profile_id_;
  // The edited profile kept its history until now: Save clears it.
  bool saved_history_ = false;
  std::optional<model::ProfileDraft::Problem> problem_;
  // The proxy test: none, running, or its answer, about `tested_`.
  enum class TestState { kNone, kTesting, kConnected, kFailed };
  TestState test_state_ = TestState::kNone;
  std::u16string test_text_;
  std::optional<model::ProxyConfig> tested_;
  std::unique_ptr<ProxyTest> test_;
  // The test's press, timed to the frame showing its answer.
  std::optional<uint64_t> testing_interval_;
  // The zones' menu: its options' identifiers, "" for Automatic.
  std::vector<std::string> zone_ids_;
  // Writing the draft into the fields: their changes are no edit.
  bool showing_ = false;

  raw_ptr<views::View> title_ = nullptr;
  raw_ptr<IconButton> close_ = nullptr;
  raw_ptr<Hairline> header_rule_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<Hairline> footer_rule_ = nullptr;
  raw_ptr<TextButton> cancel_ = nullptr;
  raw_ptr<TextButton> save_ = nullptr;

  raw_ptr<views::View> name_block_ = nullptr;
  raw_ptr<Field> name_ = nullptr;
  std::vector<raw_ptr<Swatch>> swatches_;
  raw_ptr<Switch> history_ = nullptr;
  raw_ptr<RowLabels> history_labels_ = nullptr;
  raw_ptr<Switch> spoof_ = nullptr;
  raw_ptr<RowLabels> spoof_labels_ = nullptr;
  raw_ptr<Reveal> spoof_rows_ = nullptr;
  std::vector<raw_ptr<PlatformTile>> tiles_;
  raw_ptr<Reveal> warning_ = nullptr;
  raw_ptr<views::View> warning_text_ = nullptr;
  raw_ptr<RowLabels> zone_labels_ = nullptr;
  raw_ptr<MenuButton> zone_ = nullptr;
  raw_ptr<Switch> proxy_ = nullptr;
  raw_ptr<RowLabels> proxy_labels_ = nullptr;
  raw_ptr<Reveal> proxy_rows_ = nullptr;
  raw_ptr<AddressField> address_ = nullptr;
  raw_ptr<Field> username_ = nullptr;
  raw_ptr<Field> password_ = nullptr;
  raw_ptr<TextButton> test_button_ = nullptr;
  raw_ptr<TestStatus> test_status_ = nullptr;

  // Each field's text, followed into the draft.
  std::vector<base::CallbackListSubscription> text_subscriptions_;
  // The body's scroll to a problem, as it moves.
  ValueMotion scroll_motion_;
  // The zone menu's opening, from the press to the menu about to show.
  std::optional<uint64_t> opening_menu_;
  base::WeakPtrFactory<ProfileSheet> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SHEETS_PROFILE_SHEET_H_
