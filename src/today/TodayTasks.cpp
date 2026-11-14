#include "TodayTasks.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <TrustedTime.h>

#include <ctime>

namespace today {
namespace {

std::string localDate(const time_t now, const int dayOffset) {
  struct tm local {};
  localtime_r(&now, &local);
  local.tm_mday += dayOffset;
  local.tm_hour = 12;  // noon: DST shifts cannot move the date
  local.tm_isdst = -1;
  const time_t shifted = mktime(&local);
  localtime_r(&shifted, &local);
  char buf[DATE_LEN + 1];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
  return buf;
}

void seedSample(std::vector<TodayList>& lists) {
  const Dates dates = currentDates();
  TodayList home{"sample.home", "Home", {}};
  home.tasks.push_back({"1", "Water the plants", dates.today});
  home.tasks.push_back({"2", "Take out the recycling", dates.tomorrow});
  home.tasks.push_back({"3", "Reply to the landlord", ""});
  TodayList errands{"sample.errands", "Errands", {}};
  errands.tasks.push_back({"4", "Buy coffee beans", dates.today});
  errands.tasks.push_back({"5", "Pick up the parcel", ""});
  lists.push_back(std::move(home));
  lists.push_back(std::move(errands));
}

}  // namespace

Dates currentDates() {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return {};
  return {localDate(static_cast<time_t>(now), 0), localDate(static_cast<time_t>(now), 1)};
}

Bucket bucketOf(const TodayTask& task, const Dates& dates) {
  if (task.due.empty()) return Bucket::Backlog;
  if (!dates.known() || task.due <= dates.today) return Bucket::Today;  // ISO dates compare as strings
  if (task.due == dates.tomorrow) return Bucket::Tomorrow;
  return Bucket::Backlog;
}

std::string clampTitle(const char* title) {
  std::string out(title);
  if (out.size() <= MAX_TITLE_BYTES) return out;
  size_t cut = MAX_TITLE_BYTES;
  // Back up to a UTF-8 lead byte so the title stays valid.
  while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
  out.resize(cut);
  return out;
}

bool load(std::vector<TodayList>& lists) {
  lists.clear();

  if (!Storage.exists(FILE_PATH)) {
    seedSample(lists);
    LOG_INF("TODAY", "No task file; wrote sample lists");
    return save(lists);
  }

  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(FILE_PATH, doc)) return false;

  size_t taskCount = 0;
  for (JsonObjectConst listObj : doc["lists"].as<JsonArrayConst>()) {
    if (lists.size() >= MAX_LISTS) break;
    TodayList list{listObj["id"] | "", clampTitle(listObj["title"] | ""), {}};
    for (JsonObjectConst obj : listObj["tasks"].as<JsonArrayConst>()) {
      if (taskCount >= MAX_TASKS) break;
      const char* title = obj["title"] | "";
      if (!*title) continue;
      TodayTask task;
      task.uid = obj["uid"] | "";
      task.title = clampTitle(title);
      task.due = std::string(obj["due"] | "").substr(0, DATE_LEN);
      task.done = obj["done"] | false;
      task.dirty = obj["dirty"] | false;
      task.dueDirty = obj["dueDirty"] | false;
      task.deleted = obj["deleted"] | false;
      task.movedFrom = obj["movedFrom"] | "";
      list.tasks.push_back(std::move(task));
      ++taskCount;
    }
    lists.push_back(std::move(list));
  }
  LOG_DBG("TODAY", "Loaded %d lists, %d tasks", static_cast<int>(lists.size()), static_cast<int>(taskCount));
  return true;
}

bool save(const std::vector<TodayList>& lists) {
  JsonDocument doc;
  JsonArray listsArr = doc["lists"].to<JsonArray>();
  for (const auto& list : lists) {
    JsonObject listObj = listsArr.add<JsonObject>();
    listObj["id"] = list.id;
    listObj["title"] = list.title;
    JsonArray tasksArr = listObj["tasks"].to<JsonArray>();
    for (const auto& task : list.tasks) {
      JsonObject obj = tasksArr.add<JsonObject>();
      obj["uid"] = task.uid;
      obj["title"] = task.title;
      if (!task.due.empty()) obj["due"] = task.due;
      obj["done"] = task.done;
      if (task.dirty) obj["dirty"] = true;
      if (task.dueDirty) obj["dueDirty"] = true;
      if (task.deleted) obj["deleted"] = true;
      if (!task.movedFrom.empty()) obj["movedFrom"] = task.movedFrom;
    }
  }
  return PersistableStoreBase::writeDocToFile(FILE_PATH, doc);
}

bool loadConfig(TodayConfig& config) {
  config = {};
  if (!ha::loadConnection(config.connection)) return false;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(CONFIG_PATH, doc)) return false;

  for (JsonVariantConst id : doc["lists"].as<JsonArrayConst>()) {
    if (config.listIds.size() >= MAX_LISTS) break;
    const char* value = id | "";
    if (*value) config.listIds.emplace_back(value);
  }
  return !config.listIds.empty();
}

}  // namespace today
