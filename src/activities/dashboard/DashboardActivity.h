#pragma once

#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "dashboard/Dashboard.h"

// Home Assistant values in tabs from ha-dashboard.json, drawn as tiles from
// the cache until the Sync row fetches new ones. Only Sync and toggle rows
// are selectable; a toggle queues a switch for the next sync.
class DashboardActivity final : public UiTabListActivity {
 public:
  DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiTabListActivity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  // Row 0 is Sync; others point at a toggle row of the layout.
  struct RowRef {
    uint8_t section = 0;
    uint8_t row = 0;
  };

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleButtons() override;

  int tabCount() const override { return tabs.empty() ? 1 : static_cast<int>(tabs.size()); }
  int activeTab() const override { return tab; }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  // Call with the render lock held, or before the first render.
  void rebuildRows();
  void updateSyncRow();
  void selectTab(int next, bool focusTabs);
  const dashboard::Row& layoutRow(const RowRef& ref) const;

  void startSync();
  void runSync();

  std::vector<dashboard::Tab> tabs;
  dashboard::Cache cache;
  ha::Connection connection;
  int tab = 0;
  bool configured = false;
  bool syncing = false;
  bool wifiUsed = false;
  std::string syncError;

  // Rows point into `tabs` and syncSubtitle; rebuilt with rebuildRows().
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<RowRef> rowRefs;
  std::string syncSubtitle;
};
