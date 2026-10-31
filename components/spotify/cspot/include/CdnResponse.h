#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef BELL_ONLY_CJSON
#include "cJSON.h"
#else
#include "nlohmann/json.hpp"
#endif

namespace cspot {
// An error response is not an audio URL. Never include response bodies or
// signed CDN URLs in exceptions: the caller can safely log the diagnostic.
inline std::string parseCdnUrl(int status, std::string_view body) {
  if (status != 200)
    throw std::runtime_error("CDN resolution HTTP status " + std::to_string(status));
  if (body.empty()) throw std::runtime_error("Empty CDN resolution response");
#ifdef BELL_ONLY_CJSON
  const char *end = nullptr;
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLengthOpts(body.data(), body.size(), &end, false), cJSON_Delete);
  if (!cJSON_IsObject(root.get()))
    throw std::runtime_error("Invalid CDN resolution JSON");
  // Response::body() is a bounded view, not a NUL-terminated C string.
  while (end < body.data() + body.size() &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
  if (end != body.data() + body.size())
    throw std::runtime_error("Trailing data in CDN resolution response");
  const cJSON *urls = cJSON_GetObjectItemCaseSensitive(root.get(), "cdnurl");
  const cJSON *url = cJSON_IsArray(urls) ? cJSON_GetArrayItem(urls, 0) : nullptr;
  if (!cJSON_IsString(url) || !url->valuestring || !url->valuestring[0])
    throw std::runtime_error("Missing CDN URL in resolution response");
  return url->valuestring;
#else
  auto root = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
  if (!root.is_object() || !root.contains("cdnurl") ||
      !root["cdnurl"].is_array() || root["cdnurl"].empty() ||
      !root["cdnurl"][0].is_string() || root["cdnurl"][0].get<std::string>().empty())
    throw std::runtime_error("Missing CDN URL in resolution response");
  return root["cdnurl"][0].get<std::string>();
#endif
}
}  // namespace cspot
