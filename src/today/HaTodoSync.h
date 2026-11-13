#pragma once

#include <string>
#include <vector>

#include "TodayTasks.h"

namespace today {

struct SyncResult {
  bool ok = false;
  std::string error;  // short, for display
};

// Sends pending changes to Home Assistant (removals, moves between lists as
// remove + add, then status and due date updates), then replaces `lists` with
// the items of every configured list, open ones first. Changes to items
// deleted remotely are dropped. On failure, sent tasks are no longer pending
// and the rest of `lists` is unchanged.
SyncResult syncWithHomeAssistant(const TodayConfig& config, std::vector<TodayList>& lists);

}  // namespace today
