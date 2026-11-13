#pragma once

#include <string>
#include <vector>

// Today's tasks, persisted as /.crosspoint/today.json:
//   {"tasks":[{"id":"1","title":"Water plants","done":false}]}
struct TodayTask {
  std::string id;
  std::string title;
  bool done = false;
};

namespace today {

constexpr const char* FILE_PATH = "/.crosspoint/today.json";
constexpr size_t MAX_TASKS = 32;
constexpr size_t MAX_TITLE_BYTES = 120;

// Clears `tasks`, then reads the file. A missing file is seeded with sample
// tasks. Returns false when the file exists but cannot be parsed.
bool load(std::vector<TodayTask>& tasks);
bool save(const std::vector<TodayTask>& tasks);

}  // namespace today
