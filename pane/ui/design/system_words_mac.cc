// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/system_words.h"

namespace pane {

const SystemWords& Words() {
  static constexpr SystemWords kWords{
      .quit = u"Quit",
      .quit_question = u"Quit Pane?",
      .quit_key = u"⌘Q",
      .return_key = u"↩︎",
      .installs_by_hand = u"It installs by hand.",
      // Never shown: the Mac's swap runs before Pane quits.
      .update_not_installed = u"",
  };
  return kWords;
}

}  // namespace pane
