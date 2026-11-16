// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/system_words.h"

namespace pane {

// Windows' word, as Chrome's menu has it there (IDS_EXIT), and its keys as
// Windows and Views write them (IDS_APP_ENTER_KEY). Exit Pane?'s key is the
// one its menu item shows, Alt+F4, which Dia and Arc take on Windows too.
// The update's words are Paper 11.12.12's and 11.12.14's.
const SystemWords& Words() {
  static constexpr SystemWords kWords{
      .quit = u"Exit",
      .quit_question = u"Exit Pane?",
      .quit_key = u"Alt+F4",
      .return_key = u"Enter",
      .installs_by_hand = u"Its installer opens when Pane exits.",
      .update_not_installed =
          u"The update didn\u2019t install last time. Pane tries again when "
          u"it exits.",
  };
  return kWords;
}

}  // namespace pane
