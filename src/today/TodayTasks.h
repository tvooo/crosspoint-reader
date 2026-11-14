#pragma once

#include <string>
#include <vector>

#include "ha/HaClient.h"

// Tasks grouped by remote list, persisted as /.crosspoint/today.json:
//   {"lists":[{"id":"todo.morning","title":"Morning",
//              "tasks":[{"uid":"a1","title":"Water plants","due":"2026-11-14","done":false}]}]}
// The pending-change fields are written only when set.
struct TodayTask {
  std::string uid;
  std::string title;
  std::string due;  // "YYYY-MM-DD", empty when undated
  bool done = false;
  // Local changes not yet sent:
  bool dirty = false;     // done state
  bool dueDirty = false;  // due date
  bool deleted = false;   // cleared; hidden until the remote item is removed
  std::string movedFrom;  // remote list that still holds the item after a local move

  bool pending() const { return dirty || dueDirty || deleted || !movedFrom.empty(); }
};

struct TodayList {
  std::string id;
  std::string title;
  std::vector<TodayTask> tasks;
};

// Lists to show, read from /.crosspoint/today-ha.json: {"lists":["todo.home","todo.errands"]}.
// The connection comes from ha.json.
struct TodayConfig {
  ha::Connection connection;
  std::vector<std::string> listIds;
};

namespace today {

constexpr const char* FILE_PATH = "/.crosspoint/today.json";
constexpr const char* CONFIG_PATH = "/.crosspoint/today-ha.json";
constexpr size_t MAX_LISTS = 8;
constexpr size_t MAX_TASKS = 48;  // across all lists
constexpr size_t MAX_TITLE_BYTES = 120;
constexpr size_t DATE_LEN = 10;  // "YYYY-MM-DD"

enum class Bucket { Today, Tomorrow, Backlog };

// Local dates from the trusted clock. Empty strings when the clock is unknown.
struct Dates {
  std::string today;
  std::string tomorrow;
  bool known() const { return !today.empty(); }
};
Dates currentDates();

// Undated tasks are backlog; overdue tasks are today. With an unknown clock,
// every dated task counts as today.
Bucket bucketOf(const TodayTask& task, const Dates& dates);

// Clears `lists`, then reads the file. A missing file is seeded with sample
// lists. Returns false when the file exists but cannot be parsed.
bool load(std::vector<TodayList>& lists);
bool save(const std::vector<TodayList>& lists);

// False without a connection or without lists.
bool loadConfig(TodayConfig& config);

std::string clampTitle(const char* title);

}  // namespace today
