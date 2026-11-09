// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/reorder.h"

#include <algorithm>
#include <cmath>

namespace pane::model {

int ReorderTarget(const std::vector<std::string>& ids,
                  const std::map<std::string, double>& heights,
                  double first_top,
                  const std::string& dragged,
                  double top,
                  double spacing) {
  double place = first_top;
  int best = 0;
  double best_distance = std::abs(place - top);
  int index = 0;
  for (const std::string& id : ids) {
    const auto height = heights.find(id);
    if (id == dragged || height == heights.end()) {
      continue;
    }
    place += height->second + spacing;
    ++index;
    if (std::abs(place - top) < best_distance) {
      best = index;
      best_distance = std::abs(place - top);
    }
  }
  return best;
}

std::vector<std::string> ReorderedIds(const std::vector<std::string>& ids,
                                      const std::string& id,
                                      int target) {
  const auto start = std::ranges::find(ids, id);
  if (start == ids.end()) {
    return ids;
  }
  std::vector<std::string> order = ids;
  order.erase(order.begin() + (start - ids.begin()));
  const int place = std::clamp(target, 0, static_cast<int>(order.size()));
  order.insert(order.begin() + place, id);
  return order;
}

}  // namespace pane::model
