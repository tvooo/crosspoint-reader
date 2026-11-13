#include "TodayActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "I18nKeys.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "today/HaTodoSync.h"

namespace fui = freeink::ui;

namespace {

// The theme's list row styles as FreeInkApp would expand them, but with done
// rows' text in solid dark gray: the default dithered disabled foreground only
// grays the checkbox, since list labels take solid colors only.
void applyRowStyles(const fui::ThemeTokens& theme, fui::ListProps& props) {
  fui::StyleSet styles = theme.listRow.unset() ? fui::defaultListRowStyles() : theme.listRow;
  switch (theme.listSelectionStyle) {
    case fui::SelectionStyle::LightPill:
      styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
      styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
      styles.active = styles.selected;
      break;
    case fui::SelectionStyle::Underline:
    case fui::SelectionStyle::Triangle:
      styles.selected = styles.normal;
      props.selectionMarker = theme.listSelectionStyle == fui::SelectionStyle::Underline
                                  ? fui::SelectionMarker::Underline
                                  : fui::SelectionMarker::Triangle;
      break;
    default:
      break;
  }
  styles.disabled.foreground = fui::Paint::solid(fui::Color::DarkGray);
  props.rowStyles = styles;
}

}  // namespace

void TodayActivity::onEnter() {
  UiListActivity::onEnter();

  loadFailed = !today::load(lists);
  configured = today::loadConfig(config);
  rebuildRows();
  requestUpdate();
}

void TodayActivity::onExit() {
  Activity::onExit();
  // Like other WiFi screens: reboot to clear heap fragmentation.
  if (wifiUsed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    silentRestart();
  }
}

const char* TodayActivity::headerTitle() const { return tr(STR_TODAY); }

void TodayActivity::rebuildRows() {
  rowItems.clear();
  rowTask.clear();

  fui::ListItem sync;
  sync.label = tr(STR_TODAY_SYNC);
  sync.actionValue = SYNC_ROW;
  rowItems.push_back(sync);

  // Open tasks under their list, then every done task in one Done group.
  const auto addRow = [this](const size_t l, const size_t t, const char* heading) {
    fui::ListItem item;
    item.label = lists[l].tasks[t].title.c_str();
    item.sectionHeading = heading;
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
    rowTask.emplace_back(static_cast<uint8_t>(l), static_cast<uint8_t>(t));
  };
  for (size_t l = 0; l < lists.size(); ++l) {
    const char* heading = lists[l].title.c_str();
    for (size_t t = 0; t < lists[l].tasks.size(); ++t) {
      if (lists[l].tasks[t].done) continue;
      addRow(l, t, heading);
      heading = nullptr;
    }
  }
  const char* doneHeading = tr(STR_TODAY_DONE);
  for (size_t l = 0; l < lists.size(); ++l) {
    for (size_t t = 0; t < lists[l].tasks.size(); ++t) {
      if (!lists[l].tasks[t].done) continue;
      addRow(l, t, doneHeading);
      doneHeading = nullptr;
    }
  }
  if (nav.selected >= listCount()) nav.selected = listCount() - 1;
  updateSyncSubtitle();
}

void TodayActivity::updateSyncSubtitle() {
  int pending = 0;
  for (const auto& list : lists) {
    for (const auto& task : list.tasks) pending += task.dirty ? 1 : 0;
  }

  if (syncing) {
    syncSubtitle = tr(STR_TODAY_SYNCING);
  } else if (!configured) {
    syncSubtitle = tr(STR_TODAY_NOT_CONFIGURED);
  } else if (!syncError.empty()) {
    syncSubtitle = syncError;
  } else if (pending > 0) {
    syncSubtitle = std::string(tr(STR_TODAY_PENDING)) + " " + std::to_string(pending);
  } else {
    syncSubtitle.clear();
  }
  rowItems[SYNC_ROW].subtitle = syncSubtitle.empty() ? nullptr : syncSubtitle.c_str();
}

TodayTask& TodayActivity::taskAt(const int row) {
  const auto& [l, t] = rowTask[row - 1];
  return lists[l].tasks[t];
}

void TodayActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || syncing) return;
  nav.selected = index;
  // The row repaints in place; a lingering flash would gray it.
  app.clearTapFlash();

  if (index == SYNC_ROW) {
    startSync();
    return;
  }

  {
    RenderLock lock(*this);
    TodayTask& task = taskAt(index);
    task.done = !task.done;
    task.dirty = !task.dirty;  // toggling back cancels the pending change
    // The task moves group; the selection stays put, landing on the next task.
    rebuildRows();
  }
  if (!today::save(lists)) LOG_ERR("TODAY", "Failed to save task state");
  requestUpdate();
}

void TodayActivity::startSync() {
  configured = today::loadConfig(config);
  if (!configured) {
    RenderLock lock(*this);
    updateSyncSubtitle();
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    runSync();
    return;
  }
  wifiUsed = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) runSync();
                         });
}

void TodayActivity::runSync() {
  {
    RenderLock lock(*this);
    syncing = true;
    syncError.clear();
    updateSyncSubtitle();
  }
  requestUpdateAndWait();

  // Sync a copy so rendering never sees half-replaced lists.
  std::vector<TodayList> working = lists;
  const today::SyncResult result = today::syncWithHomeAssistant(config, working);
  if (wifiUsed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  {
    RenderLock lock(*this);
    lists = std::move(working);
    syncing = false;
    syncError = result.ok ? "" : result.error;
    nav.selected = SYNC_ROW;
    rebuildRows();
  }
  if (!today::save(lists)) LOG_ERR("TODAY", "Failed to save synced tasks");
  requestUpdate();
}

void TodayActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (loadFailed) {
    screen.centeredText(tr(STR_TODAY_LOAD_FAILED), screen.theme().bodyText);
    return;
  }

  for (size_t row = 1; row < rowItems.size(); ++row) {
    const bool done = taskAt(static_cast<int>(row)).done;
    GUI.setCheckboxRow(rowItems[row], done);
    // Dimming is visual only; rows stay tappable. Disabled styling would hide
    // the selection highlight, so the selected row is drawn normally.
    rowItems[row].state = done && static_cast<int>(row) != nav.selected ? fui::StateDisabled : fui::StateNormal;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  applyRowStyles(screen.theme(), props);
  syncListViewport(screen, props);
  screen.list(props);
}
