#include "TodayTasks.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>

namespace today {
namespace {

std::string clampTitle(const char* title) {
  std::string out(title);
  if (out.size() <= MAX_TITLE_BYTES) return out;
  size_t cut = MAX_TITLE_BYTES;
  // Back up to a UTF-8 lead byte so the title stays valid.
  while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
  out.resize(cut);
  return out;
}

void seedSample(std::vector<TodayTask>& tasks) {
  static constexpr const char* titles[] = {"Water the plants", "Reply to the landlord", "Buy coffee beans",
                                           "30 minutes of reading", "Take out the recycling"};
  int id = 1;
  for (const char* title : titles) tasks.push_back({std::to_string(id++), title, false});
}

}  // namespace

bool load(std::vector<TodayTask>& tasks) {
  tasks.clear();

  if (!Storage.exists(FILE_PATH)) {
    seedSample(tasks);
    LOG_INF("TODAY", "No task file; wrote %d sample tasks", static_cast<int>(tasks.size()));
    return save(tasks);
  }

  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(FILE_PATH, doc)) return false;

  JsonArrayConst arr = doc["tasks"].as<JsonArrayConst>();
  tasks.reserve(std::min(arr.size(), MAX_TASKS));
  for (JsonObjectConst obj : arr) {
    if (tasks.size() >= MAX_TASKS) break;
    const char* title = obj["title"] | "";
    if (!*title) continue;
    tasks.push_back({obj["id"] | "", clampTitle(title), obj["done"] | false});
  }
  LOG_DBG("TODAY", "Loaded %d tasks", static_cast<int>(tasks.size()));
  return true;
}

bool save(const std::vector<TodayTask>& tasks) {
  JsonDocument doc;
  JsonArray arr = doc["tasks"].to<JsonArray>();
  for (const auto& task : tasks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = task.id;
    obj["title"] = task.title;
    obj["done"] = task.done;
  }
  return PersistableStoreBase::writeDocToFile(FILE_PATH, doc);
}

}  // namespace today
