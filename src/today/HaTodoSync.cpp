#include "HaTodoSync.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace today {
namespace {

int request(const TodayConfig& config, const char* method, const std::string& path, const std::string& payload,
            std::string& body) {
  return ha::request(config.connection, method, path, payload, body);
}

int callService(const TodayConfig& config, const char* service, const JsonDocument& data) {
  return ha::callService(config.connection, "todo", service, data);
}

SyncResult httpError(const char* step, const int status) { return {false, ha::errorText(step, status)}; }

// 400 means the item no longer exists remotely: drop the change so it cannot
// block every later sync.
bool accepted(const int status, const char* what, const TodayTask& task) {
  if (status == 400) {
    LOG_INF("TODAY", "Dropped %s for missing item %s", what, task.uid.c_str());
    return true;
  }
  return status == 200;
}

int removeItem(const TodayConfig& config, const std::string& listId, const std::string& uid) {
  JsonDocument data;
  data["entity_id"] = listId;
  data["item"] = uid;
  return callService(config, "remove_item", data);
}

// The remote item is replaced, so Home Assistant assigns a new uid; the
// following fetch picks it up. Adds before removing: a failure in between
// leaves a duplicate rather than losing the task.
int moveItem(const TodayConfig& config, const std::string& listId, const TodayTask& task) {
  JsonDocument add;
  add["entity_id"] = listId;
  add["item"] = task.title;
  if (!task.due.empty()) add["due_date"] = task.due;
  if (const int status = callService(config, "add_item", add); status != 200) return status;
  if (task.done) {
    // New items start open; find it by title to restore the done state.
    JsonDocument update;
    update["entity_id"] = listId;
    update["item"] = task.title;
    update["status"] = "completed";
    if (const int status = callService(config, "update_item", update); status != 200) return status;
  }
  return removeItem(config, task.movedFrom, task.uid);
}

int updateItem(const TodayConfig& config, const std::string& listId, const TodayTask& task) {
  JsonDocument data;
  data["entity_id"] = listId;
  data["item"] = task.uid;
  if (task.dirty) data["status"] = task.done ? "completed" : "needs_action";
  if (task.dueDirty) {
    if (task.due.empty()) {
      data["due_date"] = nullptr;  // clears the due date
    } else {
      data["due_date"] = task.due;
    }
  }
  return callService(config, "update_item", data);
}

SyncResult pushChanges(const TodayConfig& config, std::vector<TodayList>& lists) {
  for (auto& list : lists) {
    for (auto& task : list.tasks) {
      if (!task.pending()) continue;
      int status;
      if (task.deleted) {
        status = removeItem(config, task.movedFrom.empty() ? list.id : task.movedFrom, task.uid);
      } else if (!task.movedFrom.empty()) {
        status = moveItem(config, list.id, task);
      } else {
        status = updateItem(config, list.id, task);
      }
      if (!accepted(status, "change", task)) return httpError("Sending", status);
      task.dirty = task.dueDirty = false;
      task.movedFrom.clear();
    }
  }
  return {true, ""};
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
  item["due"] = true;
  JsonDocument doc;
  if (const auto err = deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    LOG_ERR("TODAY", "Bad get_items response: %s", err.c_str());
    return {false, "Unexpected response"};
  }

  out = {entityId, fetchTitle(config, entityId), {}};
  for (JsonObjectConst obj : doc["service_response"][entityId]["items"].as<JsonArrayConst>()) {
    const char* summary = obj["summary"] | "";
    if (!*summary) continue;
    TodayTask task;
    task.uid = obj["uid"] | "";
    task.title = clampTitle(summary);
    task.due = std::string(obj["due"] | "").substr(0, DATE_LEN);  // datetimes keep their date
    task.done = std::strcmp(obj["status"] | "", "completed") == 0;
    out.tasks.push_back(std::move(task));
  }
  // Open tasks first, as in Home Assistant's own list view; the budget then trims completed ones.
  std::stable_partition(out.tasks.begin(), out.tasks.end(), [](const TodayTask& t) { return !t.done; });
  if (out.tasks.size() > taskBudget) out.tasks.resize(taskBudget);
  taskBudget -= out.tasks.size();
  return {true, ""};
}

}  // namespace

SyncResult syncWithHomeAssistant(const TodayConfig& config, std::vector<TodayList>& lists) {
  if (auto result = pushChanges(config, lists); !result.ok) return result;

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
