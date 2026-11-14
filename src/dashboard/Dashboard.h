#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ha/HaClient.h"

// A read-mostly Home Assistant dashboard. The layout comes from
// /.crosspoint/ha-dashboard.json:
//   {"tabs":[{"title":"Energy","sections":[{"title":"Now","rows":[
//     {"label":"Outside","entities":["sensor.outdoor_temperature","sensor.outdoor_humidity"]},
//     {"label":"Fridge","entities":["sensor.fridge_energy"],"today":true},
//     {"label":"Away","toggle":"input_boolean.away"}]}]}]}
// A row shows its entities' values joined with " · ". A "today" row shows
// how much each meter grew since local midnight. A toggle row switches one
// input_boolean; nothing else can be switched.
namespace dashboard {

constexpr const char* LAYOUT_PATH = "/.crosspoint/ha-dashboard.json";
constexpr const char* CACHE_PATH = "/.crosspoint/ha-dashboard-cache.json";
constexpr size_t MAX_TABS = 4;
constexpr size_t MAX_ROWS = 48;  // across all tabs
constexpr size_t MAX_ENTITIES = 64;

struct Row {
  std::string label;
  std::vector<std::string> entities;
  bool toggle = false;  // entities holds exactly one input_boolean
  bool today = false;   // show growth since midnight
};

struct Section {
  std::string title;
  std::vector<Row> rows;
};

struct Tab {
  std::string title;
  std::vector<Section> sections;
};

struct Value {
  std::string entity;
  std::string state;
  std::string unit;
};

// Last fetched values plus toggles waiting for the next sync.
struct Cache {
  int64_t updated = 0;  // epoch seconds, 0 = never
  std::vector<Value> values;
  std::vector<Value> midnight;  // meter readings at the start of the day of `updated`
  std::vector<std::pair<std::string, bool>> pendingToggles;  // entity -> wanted on

  const Value* find(const std::string& entity) const;
  // Pending state if queued, else the fetched one.
  bool toggleIsOn(const std::string& entity) const;
  bool isPending(const std::string& entity) const;
  // Queues the flip, or cancels it when it returns to the fetched state.
  void flipToggle(const std::string& entity);
};

// False when the file is missing or has no tabs.
bool loadLayout(std::vector<Tab>& tabs);
bool loadCache(Cache& cache);
bool saveCache(const Cache& cache);

// The row's values for display, e.g. "20.6° · 54% · 923 ppm".
std::string rowText(const Row& row, const Cache& cache);

struct SyncResult {
  bool ok = false;
  std::string error;
};

// Sends pending toggles, then fetches every entity in the layout in one
// template request, plus the midnight readings of "today" rows from the
// history API (skipped when `midnight` is 0, i.e. the clock is unknown). On
// failure, sent toggles are no longer pending and the fetched values are
// unchanged.
SyncResult sync(const ha::Connection& connection, const std::vector<Tab>& tabs, Cache& cache, int64_t now,
                int64_t midnight);

}  // namespace dashboard
