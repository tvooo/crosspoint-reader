#pragma once

#include <ArduinoJson.h>

#include <string>

// Home Assistant REST access shared by the Today and dashboard screens.
namespace ha {

// /.crosspoint/ha.json: {"url":"http://192.168.1.20:8123","token":"<long-lived token>"}
constexpr const char* CONNECTION_PATH = "/.crosspoint/ha.json";

struct Connection {
  std::string url;
  std::string token;
};

// False when no usable url and token exist. When ha.json is missing, takes
// them from the legacy /.crosspoint/today-ha.json and writes ha.json.
bool loadConnection(Connection& connection);

// Returns the HTTP status (<= 0 when no response); the body goes to `body`.
int request(const Connection& connection, const char* method, const std::string& path, const std::string& payload,
            std::string& body);

// POST /api/services/<domain>/<service> with `data` as the body.
int callService(const Connection& connection, const char* domain, const char* service, const JsonDocument& data);

// Short, displayable description of a failed step.
std::string errorText(const char* step, int status);

}  // namespace ha
