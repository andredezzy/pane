// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/system_words.h"

namespace pane {

// Windows' word, as Chrome's menu has it there (IDS_EXIT), and its keys as
// Windows and Views write them (IDS_APP_ENTER_KEY). Exit Pane?'s key is the
// one its menu item shows, Alt+F4, which Dia and Arc take on Windows too.
const SystemWords& Words() {
  static constexpr SystemWords kWords{
      .quit = u"Exit",
      .quit_question = u"Exit Pane?",
      .quit_key = u"Alt+F4",
      .return_key = u"Enter",
  };
  return kWords;
}

}  // namespace pane
