#include "Dashboard.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace dashboard {
namespace {

constexpr const char* TOGGLE_DOMAIN = "input_boolean.";

bool isToggleEntity(const std::string& entity) { return entity.rfind(TOGGLE_DOMAIN, 0) == 0; }

// Entity ids are spliced into the fetch template, so only HA's id characters pass.
bool isEntityId(const char* id) {
  if (!*id || !std::strchr(id, '.')) return false;
  for (const char* c = id; *c; ++c) {
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.')) return false;
  }
  return true;
}

Reading formatValue(const Value& value) {
  const std::string& state = value.state;
  if (state.empty() || state == "unavailable" || state == "unknown") return {"–", ""};

  char* end = nullptr;
  double number = std::strtod(state.c_str(), &end);
  if (end == state.c_str() || *end != '\0') return {state, ""};  // not a number: on, off, heat, ...

  std::string unit = value.unit;
  if ((unit == "kW" && std::fabs(number) < 10) || (unit == "kWh" && std::fabs(number) < 1)) {
    number *= 1000;
    unit.erase(0, 1);
  }
  const bool whole = unit == "%" || unit == "ppm" || unit == "W" || unit == "Wh";
  char buf[24];
  snprintf(buf, sizeof(buf), whole ? "%.0f" : "%.1f", number);
  if (unit == "°C" || unit == "°F") unit = "°";
  return {buf, unit};
}

bool parseNumber(const std::string& text, double& out) {
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end != text.c_str() && *end == '\0';
}

const Value* findIn(const std::vector<Value>& values, const std::string& entity) {
  for (const auto& value : values) {
    if (value.entity == entity) return &value;
  }
  return nullptr;
}

// The meter's growth since midnight, in the meter's unit.
bool todayNumber(const std::string& entity, const Cache& cache, double& grown, std::string& unit) {
  const Value* now = cache.find(entity);
  const Value* start = findIn(cache.midnight, entity);
  double nowNumber, startNumber;
  if (!now || !start || !parseNumber(now->state, nowNumber) || !parseNumber(start->state, startNumber)) return false;
  grown = nowNumber - startNumber;
  // A meter that went back was reset today; its reading is all of today.
  if (grown < 0) grown = nowNumber;
  unit = now->unit;
  return true;
}

// The entity's number for display (today's growth on "today" rows), converted
// to kWh or W so rows with mixed prefixes compare.
bool amountOf(const std::string& entity, const bool today, const Cache& cache, double& amount, std::string& unit) {
  if (today) {
    if (!todayNumber(entity, cache, amount, unit)) return false;
  } else {
    const Value* value = cache.find(entity);
    if (!value || !parseNumber(value->state, amount)) return false;
    unit = value->unit;
  }
  if (unit == "Wh") {
    amount /= 1000;
    unit = "kWh";
  } else if (unit == "MWh") {
    amount *= 1000;
    unit = "kWh";
  } else if (unit == "kW") {
    amount *= 1000;
    unit = "W";
  }
  return true;
}

// "2026-11-14T23:00:00+00:00", URL-encoded.
std::string isoUtc(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm utc {};
  gmtime_r(&t, &utc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%%2B00:00", &utc);
  return buf;
}

// Distinct entities in layout order; with `todayOnly`, only those of "today" rows.
std::vector<std::string> layoutEntities(const std::vector<Tab>& tabs, const bool todayOnly = false) {
  std::vector<std::string> ids;
  for (const auto& tab : tabs) {
    for (const auto& section : tab.sections) {
      if (!section.total.empty() && (!todayOnly || section.totalToday) &&
          std::find(ids.begin(), ids.end(), section.total) == ids.end()) {
        ids.push_back(section.total);
      }
      for (const auto& row : section.rows) {
        if (todayOnly && !row.today) continue;
        for (const auto& entity : row.entities) {
          if (std::find(ids.begin(), ids.end(), entity) == ids.end()) ids.push_back(entity);
        }
      }
    }
  }
  return ids;
}

SyncResult sendToggles(const ha::Connection& connection, Cache& cache) {
  while (!cache.pendingToggles.empty()) {
    const auto& [entity, on] = cache.pendingToggles.front();
    JsonDocument data;
    data["entity_id"] = entity;
    const int status = ha::callService(connection, "input_boolean", on ? "turn_on" : "turn_off", data);
    if (status != 200) return {false, ha::errorText("Switching", status)};
    cache.pendingToggles.erase(cache.pendingToggles.begin());
  }
  return {true, ""};
}

SyncResult fetchValues(const ha::Connection& connection, const std::vector<std::string>& ids, Cache& cache) {
  // One request: HA renders [[state, unit], ...] in the order of `ids`.
  std::string tmpl = "{% set ids = [";
  for (size_t i = 0; i < ids.size(); ++i) {
    tmpl += (i ? ",\"" : "\"") + ids[i] + "\"";
  }
  tmpl += "] %}[{% for e in ids %}{{ [states(e), state_attr(e, 'unit_of_measurement')] | tojson }}"
          "{{ ',' if not loop.last }}{% endfor %}]";
  JsonDocument request;
  request["template"] = tmpl;
  std::string payload, body;
  serializeJson(request, payload);

  const int status = ha::request(connection, "POST", "/api/template", payload, body);
  if (status != 200) return {false, ha::errorText("Loading", status)};

  JsonDocument doc;
  if (const auto err = deserializeJson(doc, body)) {
    LOG_ERR("DASH", "Bad template response: %s", err.c_str());
    return {false, "Unexpected response"};
  }
  JsonArrayConst rows = doc.as<JsonArrayConst>();
  if (rows.size() != ids.size()) return {false, "Unexpected response"};

  cache.values.clear();
  cache.values.reserve(ids.size());
  size_t i = 0;
  for (JsonArrayConst pair : rows) {
    cache.values.push_back({ids[i++], pair[0] | "", pair[1] | ""});
  }
  return {true, ""};
}

// The state at `midnight` of each meter, from a one-minute history window
// (history includes each entity's state at the window start).
SyncResult fetchMidnight(const ha::Connection& connection, const std::vector<std::string>& ids,
                         const int64_t midnight, Cache& cache) {
  std::string path = "/api/history/period/" + isoUtc(midnight) + "?end_time=" + isoUtc(midnight + 60) +
                     "&minimal_response&no_attributes&filter_entity_id=";
  for (size_t i = 0; i < ids.size(); ++i) path += (i ? "," : "") + ids[i];

  std::string body;
  const int status = ha::request(connection, "GET", path, "", body);
  if (status != 200) return {false, ha::errorText("History", status)};

  JsonDocument filter;
  JsonObject first = filter.add<JsonArray>().add<JsonObject>();
  first["entity_id"] = true;
  first["state"] = true;
  JsonDocument doc;
  if (const auto err = deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    LOG_ERR("DASH", "Bad history response: %s", err.c_str());
    return {false, "Unexpected response"};
  }
  cache.midnight.clear();
  for (JsonArrayConst series : doc.as<JsonArrayConst>()) {
    JsonObjectConst start = series[0];
    cache.midnight.push_back({start["entity_id"] | "", start["state"] | "", ""});
  }
  return {true, ""};
}

}  // namespace

const Value* Cache::find(const std::string& entity) const { return findIn(values, entity); }

bool Cache::isPending(const std::string& entity) const {
  return std::any_of(pendingToggles.begin(), pendingToggles.end(),
                     [&](const auto& pending) { return pending.first == entity; });
}

bool Cache::toggleIsOn(const std::string& entity) const {
  for (const auto& [id, on] : pendingToggles) {
    if (id == entity) return on;
  }
  const Value* value = find(entity);
  return value && value->state == "on";
}

void Cache::flipToggle(const std::string& entity) {
  const auto it = std::find_if(pendingToggles.begin(), pendingToggles.end(),
                               [&](const auto& pending) { return pending.first == entity; });
  if (it != pendingToggles.end()) {
    pendingToggles.erase(it);  // back to the fetched state
  } else {
    const Value* value = find(entity);
    pendingToggles.emplace_back(entity, !(value && value->state == "on"));
  }
}

bool loadLayout(std::vector<Tab>& tabs) {
  tabs.clear();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(LAYOUT_PATH, doc)) return false;

  size_t rowCount = 0;
  size_t entityCount = 0;
  for (JsonObjectConst tabObj : doc["tabs"].as<JsonArrayConst>()) {
    if (tabs.size() >= MAX_TABS) break;
    Tab tab{tabObj["title"] | "", {}};
    for (JsonObjectConst sectionObj : tabObj["sections"].as<JsonArrayConst>()) {
      Section section;
      section.title = sectionObj["title"] | "";
      const std::string style = sectionObj["style"] | "cards";
      section.style = style == "breakdown" ? Style::Breakdown : style == "rows" ? Style::Rows : Style::Cards;
      if (const char* total = sectionObj["total"] | ""; *total) {
        if (isEntityId(total)) {
          section.total = total;
        } else {
          LOG_ERR("DASH", "Skipping invalid total %s", total);
        }
      }
      section.totalToday = sectionObj["totalToday"] | false;
      for (JsonObjectConst rowObj : sectionObj["rows"].as<JsonArrayConst>()) {
        if (rowCount >= MAX_ROWS) break;
        Row row;
        row.label = rowObj["label"] | "";
        row.today = rowObj["today"] | false;
        if (const char* toggle = rowObj["toggle"] | ""; *toggle) {
          if (!isEntityId(toggle) || !isToggleEntity(toggle)) {
            LOG_ERR("DASH", "Only input_boolean can be switched; skipping %s", toggle);
            continue;
          }
          row.toggle = true;
          row.entities.emplace_back(toggle);
        }
        for (JsonVariantConst entity : rowObj["entities"].as<JsonArrayConst>()) {
          const char* id = entity | "";
          if (!isEntityId(id)) {
            LOG_ERR("DASH", "Skipping invalid entity id %s", id);
          } else if (entityCount < MAX_ENTITIES) {
            row.entities.emplace_back(id);
            ++entityCount;
          }
        }
        if (row.entities.empty()) continue;
        section.rows.push_back(std::move(row));
        ++rowCount;
      }
      tab.sections.push_back(std::move(section));
    }
    tabs.push_back(std::move(tab));
  }
  return !tabs.empty();
}

bool loadCache(Cache& cache) {
  cache = {};
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(CACHE_PATH, doc)) return false;
  cache.updated = doc["updated"] | static_cast<int64_t>(0);
  for (JsonObjectConst obj : doc["values"].as<JsonArrayConst>()) {
    cache.values.push_back({obj["entity"] | "", obj["state"] | "", obj["unit"] | ""});
  }
  for (JsonObjectConst obj : doc["midnight"].as<JsonArrayConst>()) {
    cache.midnight.push_back({obj["entity"] | "", obj["state"] | "", ""});
  }
  for (JsonObjectConst obj : doc["pending"].as<JsonArrayConst>()) {
    cache.pendingToggles.emplace_back(obj["entity"] | "", obj["on"] | false);
  }
  return true;
}

bool saveCache(const Cache& cache) {
  JsonDocument doc;
  doc["updated"] = cache.updated;
  JsonArray values = doc["values"].to<JsonArray>();
  for (const auto& value : cache.values) {
    JsonObject obj = values.add<JsonObject>();
    obj["entity"] = value.entity;
    obj["state"] = value.state;
    if (!value.unit.empty()) obj["unit"] = value.unit;
  }
  JsonArray midnight = doc["midnight"].to<JsonArray>();
  for (const auto& value : cache.midnight) {
    JsonObject obj = midnight.add<JsonObject>();
    obj["entity"] = value.entity;
    obj["state"] = value.state;
  }
  JsonArray pending = doc["pending"].to<JsonArray>();
  for (const auto& [entity, on] : cache.pendingToggles) {
    JsonObject obj = pending.add<JsonObject>();
    obj["entity"] = entity;
    obj["on"] = on;
  }
  return PersistableStoreBase::writeDocToFile(CACHE_PATH, doc);
}

Reading formatAmount(const double amount, const std::string& unit) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%f", amount);
  return formatValue({"", buf, unit});
}

std::string Reading::text() const {
  if (unit.empty() || unit == "°" || unit == "%") return value + unit;
  return value + " " + unit;
}

Reading reading(const Row& row, const size_t index, const Cache& cache) {
  if (index >= row.entities.size()) return {"–", ""};
  const std::string& entity = row.entities[index];
  if (row.today) {
    double amount;
    std::string unit;
    return amountOf(entity, true, cache, amount, unit) ? formatAmount(amount, unit) : Reading{"–", ""};
  }
  const Value* value = cache.find(entity);
  return value ? formatValue(*value) : Reading{"–", ""};
}

std::string restText(const Row& row, const Cache& cache) {
  std::string out;
  for (size_t i = 1; i < row.entities.size(); ++i) {
    if (!out.empty()) out += " · ";
    out += reading(row, i, cache).text();
  }
  return out;
}

std::string rowText(const Row& row, const Cache& cache) {
  const std::string rest = restText(row, cache);
  const std::string first = reading(row, 0, cache).text();
  return rest.empty() ? first : first + " · " + rest;
}

std::vector<Slice> breakdown(const Section& section, const Cache& cache) {
  std::vector<Slice> slices;
  slices.reserve(section.rows.size() + 1);
  std::string sliceUnit;
  double sum = 0;
  for (const auto& row : section.rows) {
    if (row.toggle) continue;
    double amount;
    std::string unit;
    if (!amountOf(row.entities[0], row.today, cache, amount, unit) || amount <= 0) continue;
    if (sliceUnit.empty()) sliceUnit = unit;
    if (unit != sliceUnit) continue;
    slices.push_back({&row, amount, unit, formatAmount(amount, unit)});
    sum += amount;
  }
  std::sort(slices.begin(), slices.end(), [](const Slice& a, const Slice& b) { return a.amount > b.amount; });
  double total;
  std::string totalUnit;
  if (!section.total.empty() && amountOf(section.total, section.totalToday, cache, total, totalUnit) &&
      (slices.empty() || totalUnit == sliceUnit) && total - sum > total * 0.005) {
    slices.push_back({nullptr, total - sum, totalUnit, formatAmount(total - sum, totalUnit)});
  }
  return slices;
}

SyncResult sync(const ha::Connection& connection, const std::vector<Tab>& tabs, Cache& cache, const int64_t now,
                const int64_t midnight) {
  if (auto result = sendToggles(connection, cache); !result.ok) return result;
  const std::vector<std::string> ids = layoutEntities(tabs);
  if (auto result = fetchValues(connection, ids, cache); !result.ok) return result;
  cache.midnight.clear();
  if (const auto todayIds = layoutEntities(tabs, true); !todayIds.empty() && midnight > 0) {
    if (auto result = fetchMidnight(connection, todayIds, midnight, cache); !result.ok) return result;
  }
  cache.updated = now;
  LOG_INF("DASH", "Fetched %d values", static_cast<int>(ids.size()));
  return {true, ""};
}

}  // namespace dashboard
