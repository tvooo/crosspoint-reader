#include "TodayTasks.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

namespace today {
namespace {

void seedSample(std::vector<TodayList>& lists) {
  lists.push_back({"sample.home",
                   "Home",
                   {{"1", "Water the plants"}, {"2", "Take out the recycling"}, {"3", "Reply to the landlord"}}});
  lists.push_back({"sample.errands", "Errands", {{"4", "Buy coffee beans"}, {"5", "Pick up the parcel"}}});
}

}  // namespace

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
      list.tasks.push_back({obj["uid"] | "", clampTitle(title), obj["done"] | false, obj["dirty"] | false});
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
      obj["done"] = task.done;
      obj["dirty"] = task.dirty;
    }
  }
  return PersistableStoreBase::writeDocToFile(FILE_PATH, doc);
}

bool loadConfig(TodayConfig& config) {
  config = {};
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(CONFIG_PATH, doc)) return false;

  config.url = doc["url"] | "";
  while (!config.url.empty() && config.url.back() == '/') config.url.pop_back();
  config.token = doc["token"] | "";
  for (JsonVariantConst id : doc["lists"].as<JsonArrayConst>()) {
    if (config.listIds.size() >= MAX_LISTS) break;
    const char* value = id | "";
    if (*value) config.listIds.emplace_back(value);
  }
  return !config.url.empty() && !config.token.empty() && !config.listIds.empty();
}

}  // namespace today
