#pragma once

#include <vector>

#include "activities/UiListActivity.h"
#include "today/TodayTasks.h"

// Today's tasks as a checkbox list; Confirm or tap toggles a task and saves.
class TodayActivity final : public UiListActivity {
 public:
  TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Today", renderer, mappedInput) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::vector<TodayTask> tasks;
  // Labels point into `tasks`; rebuilt only when `tasks` is reloaded.
  std::vector<freeink::ui::ListItem> rowItems;
  bool loadFailed = false;
};
