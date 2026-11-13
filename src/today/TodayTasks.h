#pragma once

#include <string>
#include <vector>

// Today's tasks, grouped by remote list, persisted as /.crosspoint/today.json:
//   {"lists":[{"id":"todo.home","title":"Home",
//              "tasks":[{"uid":"a1","title":"Water plants","done":false,"dirty":false}]}]}
// `dirty` marks a done state that differs from the remote and still has to be sent.
struct TodayTask {
  std::string uid;
  std::string title;
  bool done = false;
  bool dirty = false;
};

struct TodayList {
  std::string id;
  std::string title;
  std::vector<TodayTask> tasks;
};

// Home Assistant connection, read from /.crosspoint/today-ha.json:
//   {"url":"http://192.168.1.20:8123","token":"<long-lived token>","lists":["todo.home","todo.errands"]}
struct TodayConfig {
  std::string url;
  std::string token;
  std::vector<std::string> listIds;
};

namespace today {

constexpr const char* FILE_PATH = "/.crosspoint/today.json";
constexpr const char* CONFIG_PATH = "/.crosspoint/today-ha.json";
constexpr size_t MAX_LISTS = 8;
constexpr size_t MAX_TASKS = 48;  // across all lists
constexpr size_t MAX_TITLE_BYTES = 120;

// Clears `lists`, then reads the file. A missing file is seeded with sample
// lists. Returns false when the file exists but cannot be parsed.
bool load(std::vector<TodayList>& lists);
bool save(const std::vector<TodayList>& lists);

// False when the file is missing or lacks url, token, or lists.
bool loadConfig(TodayConfig& config);

std::string clampTitle(const char* title);

}  // namespace today
