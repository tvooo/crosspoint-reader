#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/UiListActivity.h"
#include "today/TodayTasks.h"

// Today's tasks grouped by list. Row 0 syncs with Home Assistant; the other
// rows are checkboxes that toggle a task and save it for the next sync.
class TodayActivity final : public UiListActivity {
 public:
  TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Today", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int SYNC_ROW = 0;

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Call with the render lock held, or before the first render.
  void rebuildRows();
  void updateSyncSubtitle();
  void startSync();
  void runSync();
  TodayTask& taskAt(int row);

  std::vector<TodayList> lists;
  TodayConfig config;
  bool configured = false;
  bool loadFailed = false;
  bool syncing = false;
  bool wifiUsed = false;
  std::string syncError;

  // Rows point into `lists` and the strings below; rebuilt with rebuildRows().
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::pair<uint8_t, uint8_t>> rowTask;  // row - 1 -> (list, task)
  std::string syncSubtitle;
};
