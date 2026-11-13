#pragma once

#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"
#include "today/TodayTasks.h"

// Tasks in Today / Tomorrow / Backlog tabs by due date. Each tab lists open
// tasks (grouped by list, except Backlog), then a Done group. Toggling,
// clearing, and rescheduling are saved locally; the Sync row sends them to
// Home Assistant.
class TodayActivity final : public UiTabListActivity {
 public:
  TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiTabListActivity("Today", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&& lock) override;

 private:
  enum class RowKind : uint8_t { Sync, Task, ClearDone };
  struct Row {
    RowKind kind;
    uint8_t list = 0;
    uint8_t task = 0;
  };

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  int tabCount() const override { return 3; }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  // Call with the render lock held, or before the first render.
  void rebuildRows();
  void updateSyncRow();
  void selectTab(today::Bucket next, bool focusTabs);
  void persist();

  void toggleTask(const Row& row);
  void clearDone();
  void showReschedule(int index);
  void rescheduleTo(const Row& row, int option);
  void startSync();
  void runSync();

  std::vector<TodayList> lists;
  TodayConfig config;
  today::Dates dates;
  today::Bucket tab = today::Bucket::Today;
  bool configured = false;
  bool loadFailed = false;
  bool syncing = false;
  bool wifiUsed = false;
  std::string syncError;

  // Rows point into `lists` and the strings below; rebuilt with rebuildRows().
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<Row> rows;
  std::string syncSubtitle;

  OptionPopup popup;
  // Reschedule choices: day buckets first, then list indices.
  std::vector<int> rescheduleChoices;
};
