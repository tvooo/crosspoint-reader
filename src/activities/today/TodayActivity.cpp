#include "TodayActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include "I18nKeys.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "today/HaTodoSync.h"

namespace fui = freeink::ui;
using today::Bucket;

namespace {

constexpr unsigned long RESCHEDULE_HOLD_MS = 700;  // same hold as bookmark actions
constexpr uint32_t CLOCK_SYNC_MS = 5000;
// Reschedule choices below this are day buckets; at or above, list indices.
constexpr int CHOICE_LIST_BASE = 100;

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

const char* bucketLabel(const Bucket bucket) {
  switch (bucket) {
    case Bucket::Today:
      return tr(STR_TODAY);
    case Bucket::Tomorrow:
      return tr(STR_TOMORROW);
    default:
      return tr(STR_BACKLOG);
  }
}

}  // namespace

void TodayActivity::onEnter() {
  UiTabListActivity::onEnter();

  dates = today::currentDates();
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

void TodayActivity::render(RenderLock&& lock) {
  // The popup owns the whole frame while it is up.
  if (popup.processRender(renderer, mappedInput)) return;
  UiTabListActivity::render(std::move(lock));
}

const char* TodayActivity::headerTitle() const { return tr(STR_TASKS); }

const char* TodayActivity::tabLabel(const int index) const { return bucketLabel(static_cast<Bucket>(index)); }

void TodayActivity::rebuildRows() {
  rowItems.clear();
  rows.clear();

  fui::ListItem sync;
  sync.label = tr(STR_TODAY_SYNC);
  rowItems.push_back(sync);
  rows.push_back({RowKind::Sync});

  const auto addTask = [this](const size_t l, const size_t t, const char* heading) {
    fui::ListItem item;
    item.label = lists[l].tasks[t].title.c_str();
    item.sectionHeading = heading;
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
    rows.push_back({RowKind::Task, static_cast<uint8_t>(l), static_cast<uint8_t>(t)});
  };
  const auto inTab = [this](const TodayTask& task, const bool done) {
    return !task.deleted && task.done == done && today::bucketOf(task, dates) == tab;
  };

  // Open tasks under their list; the backlog has no meaningful groups.
  const bool grouped = tab != Bucket::Backlog;
  for (size_t l = 0; l < lists.size(); ++l) {
    const char* heading = grouped ? lists[l].title.c_str() : nullptr;
    for (size_t t = 0; t < lists[l].tasks.size(); ++t) {
      if (!inTab(lists[l].tasks[t], false)) continue;
      addTask(l, t, heading);
      heading = nullptr;
    }
  }

  // Every done task in one Done group, then the action to clear them.
  const char* doneHeading = tr(STR_TODAY_DONE);
  for (size_t l = 0; l < lists.size(); ++l) {
    for (size_t t = 0; t < lists[l].tasks.size(); ++t) {
      if (!inTab(lists[l].tasks[t], true)) continue;
      addTask(l, t, doneHeading);
      doneHeading = nullptr;
    }
  }
  if (!doneHeading) {
    fui::ListItem clear;
    clear.label = tr(STR_TODAY_CLEAR_DONE);
    clear.icon = fui::bitmapFromIcon(icon_trash_2_24);
    clear.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(clear);
    rows.push_back({RowKind::ClearDone});
  }

  auto& n = activeNav();
  if (n.selected > listCount()) n.selected = listCount();
  updateSyncRow();
}

void TodayActivity::updateSyncRow() {
  int pending = 0;
  for (const auto& list : lists) {
    for (const auto& task : list.tasks) pending += task.pending() ? 1 : 0;
  }

  if (syncing) {
    syncSubtitle = tr(STR_TODAY_SYNCING);
  } else if (!configured) {
    syncSubtitle = tr(STR_TODAY_NOT_CONFIGURED);
  } else if (!syncError.empty()) {
    syncSubtitle = syncError;
  } else if (pending > 0) {
    syncSubtitle = std::string(tr(STR_TODAY_PENDING)) + " " + std::to_string(pending);
  } else if (!dates.known()) {
    syncSubtitle = tr(STR_TODAY_NO_CLOCK);
  } else {
    syncSubtitle.clear();
  }
  fui::ListItem& row = rowItems[0];
  row.subtitle = syncSubtitle.empty() ? nullptr : syncSubtitle.c_str();
  // Subtitle rows carry the larger icon, as elsewhere in the firmware.
  row.icon = fui::bitmapFromIcon(row.subtitle ? icon_refresh_cw_32 : icon_refresh_cw_24);
}

void TodayActivity::persist() {
  if (!today::save(lists)) LOG_ERR("TODAY", "Failed to save tasks");
}

void TodayActivity::selectTab(const Bucket next, const bool focusTabs) {
  {
    RenderLock lock(*this);
    tab = next;
    rebuildRows();
    auto& n = activeNav();
    if (focusTabs) n.selected = 0;
    n.followOnBuild = true;  // pull the tab's viewport to its remembered selection
  }
  requestUpdate();
}

void TodayActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : tabCount() - 1)) % tabCount();
  selectTab(static_cast<Bucket>(next), ringPos() == 0);
}

void TodayActivity::onTabAction(const int index) {
  if (popup.isActive() || syncing) return;
  app.clearTapFlash();
  selectTab(static_cast<Bucket>(index), true);
}

bool TodayActivity::handleCustomInput() {
  return popup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool TodayActivity::handleButtons() {
  // The release after a hold is consumed globally, so it cannot also toggle.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, RESCHEDULE_HOLD_MS)) {
    if (ringPos() > 0) showReschedule(ringPos() - 1);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

void TodayActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || syncing || popup.isActive()) return;
  // The row repaints in place or moves; a lingering flash would gray it.
  app.clearTapFlash();

  switch (rows[index].kind) {
    case RowKind::Sync:
      startSync();
      break;
    case RowKind::ClearDone:
      clearDone();
      break;
    case RowKind::Task:
      toggleTask(rows[index]);
      break;
  }
}

void TodayActivity::onRowLongPress(const int index) {
  app.clearTapFlash();
  showReschedule(index);
}

void TodayActivity::toggleTask(const Row& row) {
  {
    RenderLock lock(*this);
    TodayTask& task = lists[row.list].tasks[row.task];
    task.done = !task.done;
    task.dirty = !task.dirty;  // toggling back cancels the pending change
    // The task moves group; the selection stays put, landing on the next task.
    rebuildRows();
  }
  persist();
  requestUpdate();
}

void TodayActivity::clearDone() {
  {
    RenderLock lock(*this);
    for (auto& list : lists) {
      for (auto& task : list.tasks) {
        if (task.done && !task.deleted && today::bucketOf(task, dates) == tab) task.deleted = true;
      }
    }
    rebuildRows();
  }
  persist();
  requestUpdate();
}

void TodayActivity::showReschedule(const int index) {
  if (index < 0 || index >= listCount() || rows[index].kind != RowKind::Task || syncing || popup.isActive()) return;
  const Row row = rows[index];
  const TodayTask& task = lists[row.list].tasks[row.task];

  rescheduleChoices.clear();
  std::vector<const char*> labels;
  int current = 0;
  const Bucket bucket = today::bucketOf(task, dates);
  for (const Bucket b : {Bucket::Today, Bucket::Tomorrow, Bucket::Backlog}) {
    // Without the date, Today and Tomorrow cannot be set.
    if (b != Bucket::Backlog && !dates.known()) continue;
    if (b == bucket) current = static_cast<int>(labels.size());
    rescheduleChoices.push_back(static_cast<int>(b));
    labels.push_back(bucketLabel(b));
  }
  for (size_t l = 0; l < lists.size(); ++l) {
    rescheduleChoices.push_back(CHOICE_LIST_BASE + static_cast<int>(l));
    labels.push_back(lists[l].title.c_str());
  }

  popup.show(tr(STR_TODAY_RESCHEDULE), task.title.c_str(), labels.data(), static_cast<int>(labels.size()), current,
             [this, row](const int choice) { rescheduleTo(row, choice); });
  requestUpdate();
}

void TodayActivity::rescheduleTo(const Row& row, const int option) {
  if (option < 0 || option >= static_cast<int>(rescheduleChoices.size())) return;
  const int choice = rescheduleChoices[option];
  {
    RenderLock lock(*this);
    auto& source = lists[row.list].tasks;
    TodayTask& task = source[row.task];
    if (choice < CHOICE_LIST_BASE) {
      const auto bucket = static_cast<Bucket>(choice);
      const std::string due =
          bucket == Bucket::Today ? dates.today : bucket == Bucket::Tomorrow ? dates.tomorrow : std::string();
      if (task.due != due) {
        task.due = due;
        task.dueDirty = true;
      }
    } else if (const size_t target = static_cast<size_t>(choice - CHOICE_LIST_BASE); target != row.list) {
      // A move is a remote remove + add; remember where the remote item lives.
      TodayTask moved = std::move(task);
      if (moved.movedFrom.empty()) {
        moved.movedFrom = lists[row.list].id;
      } else if (moved.movedFrom == lists[target].id) {
        moved.movedFrom.clear();  // back in its remote list
      }
      source.erase(source.begin() + row.task);
      lists[target].tasks.push_back(std::move(moved));
    }
    rebuildRows();
  }
  persist();
  requestUpdate();
}

void TodayActivity::startSync() {
  configured = today::loadConfig(config);
  if (!configured) {
    RenderLock lock(*this);
    updateSyncRow();
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
    updateSyncRow();
  }
  requestUpdateAndWait();

  // Due dates are bucketed by the local date; a failed time sync keeps the old clock.
  if (!trustedtime::syncNow(CLOCK_SYNC_MS)) LOG_INF("TODAY", "Clock sync failed");

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
    dates = today::currentDates();
    syncing = false;
    syncError = result.ok ? "" : result.error;
    activeNav().selected = 1;  // the Sync row
    rebuildRows();
  }
  persist();
  requestUpdate();
}

void TodayActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  buildTabBar(screen);

  if (loadFailed) {
    screen.centeredText(tr(STR_TODAY_LOAD_FAILED), screen.theme().bodyText);
    return;
  }

  // This hint names a physical button, so omit it on touch boards.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_TODAY_HOLD_HELP));
  }

  const int selectedRow = ringPos() - 1;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].kind != RowKind::Task) continue;
    const bool done = lists[rows[i].list].tasks[rows[i].task].done;
    GUI.setCheckboxRow(rowItems[i], done);
    // Dimming is visual only; rows stay tappable. Disabled styling would hide
    // the selection highlight, so the selected row is drawn normally.
    rowItems[i].state = done && static_cast<int>(i) != selectedRow ? fui::StateDisabled : fui::StateNormal;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // physical buttons stay in loop()
  applyRowStyles(screen.theme(), props);
  syncTabListViewport(screen, props);
  screen.list(props);
}
