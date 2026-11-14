#include "DashboardActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>

#include <ctime>

#include "DashboardTiles.h"
#include "I18nKeys.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {

constexpr uint32_t CLOCK_SYNC_MS = 5000;

// Local midnight of today, or 0 when the clock is unknown.
int64_t localMidnight() {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return 0;
  const time_t t = static_cast<time_t>(now);
  struct tm local {};
  localtime_r(&t, &local);
  local.tm_hour = local.tm_min = local.tm_sec = 0;
  local.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&local));
}

// "14:05" today, "13 Nov 14:05" otherwise.
std::string formatUpdated(const int64_t updated) {
  const time_t when = static_cast<time_t>(updated);
  const time_t now = time(nullptr);
  struct tm whenTm {}, nowTm {};
  localtime_r(&when, &whenTm);
  localtime_r(&now, &nowTm);
  const bool sameDay = whenTm.tm_year == nowTm.tm_year && whenTm.tm_yday == nowTm.tm_yday;
  char buf[24];
  strftime(buf, sizeof(buf), sameDay ? "%H:%M" : "%d %b %H:%M", &whenTm);
  return buf;
}

}  // namespace

void DashboardActivity::onEnter() {
  // Before the base onEnter, which sizes per-tab navigation from tabCount().
  configured = dashboard::loadLayout(tabs) && ha::loadConnection(connection);
  UiTabListActivity::onEnter();
  dashboard::loadCache(cache);
  rebuildRows();
  requestUpdate();
}

void DashboardActivity::onExit() {
  Activity::onExit();
  // Like other WiFi screens: reboot to clear heap fragmentation.
  if (wifiUsed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    silentRestart();
  }
}

const char* DashboardActivity::headerTitle() const { return tr(STR_HOUSE); }

const char* DashboardActivity::tabLabel(const int index) const {
  return tabs.empty() ? tr(STR_HOUSE) : tabs[static_cast<size_t>(index)].title.c_str();
}

const dashboard::Row& DashboardActivity::layoutRow(const RowRef& ref) const {
  return tabs[static_cast<size_t>(tab)].sections[ref.section].rows[ref.row];
}

void DashboardActivity::rebuildRows() {
  rowItems.clear();
  rowRefs.clear();

  fui::ListItem sync;
  sync.label = tr(STR_TODAY_SYNC);
  rowItems.push_back(sync);
  rowRefs.push_back({});

  // Only toggles are selectable; everything else is drawn as tiles.
  if (!tabs.empty()) {
    const auto& sections = tabs[static_cast<size_t>(tab)].sections;
    for (size_t s = 0; s < sections.size(); ++s) {
      for (size_t r = 0; r < sections[s].rows.size(); ++r) {
        const dashboard::Row& row = sections[s].rows[r];
        if (!row.toggle) continue;
        fui::ListItem item;
        item.label = row.label.c_str();
        item.actionValue = static_cast<int16_t>(rowItems.size());
        GUI.setCheckboxRow(item, cache.toggleIsOn(row.entities[0]));
        if (cache.isPending(row.entities[0])) item.subtitle = tr(STR_DASH_PENDING_TOGGLE);
        rowItems.push_back(item);
        rowRefs.push_back({static_cast<uint8_t>(s), static_cast<uint8_t>(r)});
      }
    }
  }

  auto& n = activeNav();
  if (n.selected > listCount()) n.selected = listCount();
  updateSyncRow();
}

void DashboardActivity::updateSyncRow() {
  if (syncing) {
    syncSubtitle = tr(STR_TODAY_SYNCING);
  } else if (!configured) {
    syncSubtitle = tr(STR_DASH_NOT_CONFIGURED);
  } else if (!syncError.empty()) {
    syncSubtitle = syncError;
  } else if (!cache.pendingToggles.empty()) {
    syncSubtitle = std::string(tr(STR_TODAY_PENDING)) + " " + std::to_string(cache.pendingToggles.size());
  } else if (cache.updated > 0) {
    syncSubtitle = std::string(tr(STR_DASH_UPDATED)) + " " + formatUpdated(cache.updated);
  } else {
    syncSubtitle = tr(STR_DASH_NEVER_SYNCED);
  }
  fui::ListItem& row = rowItems[0];
  row.subtitle = syncSubtitle.c_str();
  row.icon = fui::bitmapFromIcon(icon_refresh_cw_32);
}

void DashboardActivity::selectTab(const int next, const bool focusTabs) {
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

void DashboardActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : tabCount() - 1)) % tabCount();
  selectTab(next, ringPos() == 0);
}

void DashboardActivity::onTabAction(const int index) {
  if (syncing) return;
  app.clearTapFlash();
  selectTab(index, true);
}

bool DashboardActivity::handleButtons() {
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

void DashboardActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || syncing) return;
  app.clearTapFlash();

  if (index == 0) {
    startSync();
    return;
  }
  const dashboard::Row& row = layoutRow(rowRefs[static_cast<size_t>(index)]);
  {
    RenderLock lock(*this);
    cache.flipToggle(row.entities[0]);
    rebuildRows();
  }
  if (!dashboard::saveCache(cache)) LOG_ERR("DASH", "Failed to save cache");
  requestUpdate();
}

void DashboardActivity::startSync() {
  {
    // The layout may have changed on the card since the screen opened.
    RenderLock lock(*this);
    configured = dashboard::loadLayout(tabs) && ha::loadConnection(connection);
    if (tabNavs.size() != static_cast<size_t>(tabCount())) {
      tabNavs.assign(static_cast<size_t>(tabCount()), freeink::ui::ListNav{});
      tab = 0;
      activeNav().selected = 1;  // the Sync row
    }
    rebuildRows();
  }
  if (!configured) {
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

void DashboardActivity::runSync() {
  {
    RenderLock lock(*this);
    syncing = true;
    syncError.clear();
    updateSyncRow();
  }
  requestUpdateAndWait();

  // "Updated" times need the clock; a failed time sync keeps the old one.
  if (!trustedtime::syncNow(CLOCK_SYNC_MS)) LOG_INF("DASH", "Clock sync failed");

  // Sync a copy so rendering never sees half-replaced values.
  dashboard::Cache working = cache;
  const dashboard::SyncResult result = dashboard::sync(connection, tabs, working, time(nullptr), localMidnight());
  if (wifiUsed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  {
    RenderLock lock(*this);
    cache = std::move(working);
    syncing = false;
    syncError = result.ok ? "" : result.error;
    rebuildRows();
  }
  if (!dashboard::saveCache(cache)) LOG_ERR("DASH", "Failed to save cache");
  requestUpdate();
}

void DashboardActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  buildTabBar(screen);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncTabListViewport(screen, props);
  // The list takes only its rows' height; the tiles get the rest.
  const int16_t listHeight = static_cast<int16_t>(fui::listContentHeight(
      screen.target(), screen.frame().assets(), screen.contentRect(), screen.resolveListProps(props)));
  screen.list(props, listHeight);
  if (tabs.empty()) return;

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  fui::Rect tiles = screen.contentRect();
  tiles.x = static_cast<int16_t>(tiles.x + side);
  tiles.width = static_cast<int16_t>(tiles.width - 2 * side);
  dashboard_tiles::drawTab(screen, tiles, tabs[static_cast<size_t>(tab)], cache);
}
