#include "HaTodoSync.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <algorithm>
#include <cstring>

namespace today {
namespace {

constexpr uint32_t TIMEOUT_MS = 15000;

// Returns the HTTP status (<= 0 when no response); the body goes to `body`.
int request(const TodayConfig& config, const char* method, const std::string& path, const std::string& payload,
            std::string& body) {
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(TIMEOUT_MS);
  if (!http.begin(config.url + path)) return -1;
  http.addHeader("Authorization", "Bearer " + config.token);
  http.addHeader("Content-Type", "application/json");
  const int status = http.sendRequest(method, payload);
  body = http.getString();
  http.end();
  return status;
}

SyncResult httpError(const char* step, int status) {
  LOG_ERR("TODAY", "%s failed: HTTP %d", step, status);
  if (status == 401) return {false, "Token rejected (401)"};
  if (status <= 0) return {false, "Home Assistant unreachable"};
  return {false, std::string(step) + " failed (HTTP " + std::to_string(status) + ")"};
}

std::string getItemsBody(const std::string& entityId) {
  JsonDocument doc;
  doc["entity_id"] = entityId;
  // Explicit: the REST default differs from the documented needs_action default.
  JsonArray status = doc["status"].to<JsonArray>();
  status.add("needs_action");
  status.add("completed");
  std::string out;
  serializeJson(doc, out);
  return out;
}

SyncResult pushDirty(const TodayConfig& config, std::vector<TodayList>& lists) {
  for (auto& list : lists) {
    for (auto& task : list.tasks) {
      if (!task.dirty) continue;
      JsonDocument doc;
      doc["entity_id"] = list.id;
      doc["item"] = task.uid;
      doc["status"] = task.done ? "completed" : "needs_action";
      std::string payload, body;
      serializeJson(doc, payload);
      const int status = request(config, "POST", "/api/services/todo/update_item", payload, body);
      // 400: the item no longer exists remotely; drop the change so it cannot block every sync.
      if (status == 400) {
        LOG_INF("TODAY", "Dropped change for missing item %s in %s", task.uid.c_str(), list.id.c_str());
      } else if (status != 200) {
        return httpError("Sending", status);
      }
      task.dirty = false;
    }
  }
  return {true, ""};
}

std::string fetchTitle(const TodayConfig& config, const std::string& entityId) {
  std::string body;
  const int status = request(config, "GET", "/api/states/" + entityId, "", body);
  if (status != 200) return entityId;
  JsonDocument filter;
  filter["attributes"]["friendly_name"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return entityId;
  const char* name = doc["attributes"]["friendly_name"] | "";
  return *name ? clampTitle(name) : entityId;
}

SyncResult fetchList(const TodayConfig& config, const std::string& entityId, size_t& taskBudget, TodayList& out) {
  std::string body;
  const int status =
      request(config, "POST", "/api/services/todo/get_items?return_response", getItemsBody(entityId), body);
  if (status != 200) return httpError("Loading", status);

  JsonDocument filter;
  JsonObject item = filter["service_response"][entityId]["items"].add<JsonObject>();
  item["uid"] = true;
  item["summary"] = true;
  item["status"] = true;
  JsonDocument doc;
  if (const auto err = deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    LOG_ERR("TODAY", "Bad get_items response: %s", err.c_str());
    return {false, "Unexpected response"};
  }

  out = {entityId, fetchTitle(config, entityId), {}};
  for (JsonObjectConst obj : doc["service_response"][entityId]["items"].as<JsonArrayConst>()) {
    const char* summary = obj["summary"] | "";
    if (!*summary) continue;
    const bool done = std::strcmp(obj["status"] | "", "completed") == 0;
    out.tasks.push_back({obj["uid"] | "", clampTitle(summary), done});
  }
  // Open tasks first, as in Home Assistant's own list view; the budget then trims completed ones.
  std::stable_partition(out.tasks.begin(), out.tasks.end(), [](const TodayTask& t) { return !t.done; });
  if (out.tasks.size() > taskBudget) out.tasks.resize(taskBudget);
  taskBudget -= out.tasks.size();
  return {true, ""};
}

}  // namespace

SyncResult syncWithHomeAssistant(const TodayConfig& config, std::vector<TodayList>& lists) {
  if (auto result = pushDirty(config, lists); !result.ok) return result;

  std::vector<TodayList> fresh;
  fresh.reserve(config.listIds.size());
  size_t taskBudget = MAX_TASKS;
  for (const auto& id : config.listIds) {
    TodayList list;
    if (auto result = fetchList(config, id, taskBudget, list); !result.ok) return result;
    fresh.push_back(std::move(list));
  }
  lists = std::move(fresh);
  LOG_INF("TODAY", "Synced %d lists", static_cast<int>(lists.size()));
  return {true, ""};
}

}  // namespace today
