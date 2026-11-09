// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_REORDER_H_
#define PANE_MODEL_REORDER_H_

#include <map>
#include <string>
#include <vector>

namespace pane::model {

// Where a row dragged up or down a list drops, as dnd-kit's closest center
// picks for the Electron app's sortable lists: the place whose top is nearest
// the dragged copy's, each place's top counted with the dragged row out of the
// list and back in at it, so rows of any height count (an expanded profile is
// its header and its tabs). `ids` in their order, each row's height, the
// list's first top, the dragged row, the copy's top, and the space between
// rows.
int ReorderTarget(const std::vector<std::string>& ids,
                  const std::map<std::string, double>& heights,
                  double first_top,
                  const std::string& dragged,
                  double top,
                  double spacing);

// `ids` with `id` moved to `target`, among the others.
std::vector<std::string> ReorderedIds(const std::vector<std::string>& ids,
                                      const std::string& id,
                                      int target);

}  // namespace pane::model

#endif  // PANE_MODEL_REORDER_H_
