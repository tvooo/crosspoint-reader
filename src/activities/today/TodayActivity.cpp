#include "TodayActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "I18nKeys.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void TodayActivity::onEnter() {
  UiListActivity::onEnter();

  loadFailed = !today::load(tasks);
  rowItems.clear();
  rowItems.reserve(tasks.size());
  for (size_t i = 0; i < tasks.size(); ++i) {
    fui::ListItem item;
    item.label = tasks[i].title.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
  requestUpdate();
}

const char* TodayActivity::headerTitle() const { return tr(STR_TODAY); }

void TodayActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  // The row repaints with a new checkbox; a lingering flash would gray it.
  app.clearTapFlash();

  tasks[index].done = !tasks[index].done;
  if (!today::save(tasks)) LOG_ERR("TODAY", "Failed to save task state");
  requestUpdate();
}

void TodayActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems.empty()) {
    screen.centeredText(loadFailed ? tr(STR_TODAY_LOAD_FAILED) : tr(STR_TODAY_EMPTY), screen.theme().bodyText);
    return;
  }

  for (size_t i = 0; i < rowItems.size(); ++i) GUI.setCheckboxRow(rowItems[i], tasks[i].done);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
