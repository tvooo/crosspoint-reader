#include "HaClient.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SecureHttpClient.h>

namespace ha {
namespace {

constexpr const char* LEGACY_PATH = "/.crosspoint/today-ha.json";
constexpr uint32_t TIMEOUT_MS = 15000;

bool readConnection(const char* path, Connection& connection) {
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path, doc)) return false;
  connection.url = doc["url"] | "";
  while (!connection.url.empty() && connection.url.back() == '/') connection.url.pop_back();
  connection.token = doc["token"] | "";
  return !connection.url.empty() && !connection.token.empty();
}

}  // namespace

bool loadConnection(Connection& connection) {
  connection = {};
  if (Storage.exists(CONNECTION_PATH)) return readConnection(CONNECTION_PATH, connection);
  if (!readConnection(LEGACY_PATH, connection)) return false;

  JsonDocument doc;
  doc["url"] = connection.url;
  doc["token"] = connection.token;
  if (PersistableStoreBase::writeDocToFile(CONNECTION_PATH, doc)) LOG_INF("HA", "Moved connection to ha.json");
  return true;
}

int request(const Connection& connection, const char* method, const std::string& path, const std::string& payload,
            std::string& body) {
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(TIMEOUT_MS);
  if (!http.begin(connection.url + path)) return -1;
  http.addHeader("Authorization", "Bearer " + connection.token);
  http.addHeader("Content-Type", "application/json");
  const int status = http.sendRequest(method, payload);
  body = http.getString();
  http.end();
  return status;
}

int callService(const Connection& connection, const char* domain, const char* service, const JsonDocument& data) {
  std::string payload, body;
  serializeJson(data, payload);
  return request(connection, "POST", std::string("/api/services/") + domain + "/" + service, payload, body);
}

std::string errorText(const char* step, const int status) {
  LOG_ERR("HA", "%s failed: HTTP %d", step, status);
  if (status == 401) return "Token rejected (401)";
  if (status <= 0) return "Home Assistant unreachable";
  return std::string(step) + " failed (HTTP " + std::to_string(status) + ")";
}

}  // namespace ha
