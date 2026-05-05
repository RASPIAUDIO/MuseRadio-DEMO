#include "ApResolve.h"

#include <initializer_list>  // for initializer_list
#include <map>               // for operator!=, operator==
#include <memory>            // for allocator, unique_ptr
#include <stdexcept>
#include <string>
#include <string_view>       // for string_view
#include <vector>            // for vector

#include "HTTPClient.h"  // for HTTPClient, HTTPClient::Response
#ifdef BELL_ONLY_CJSON
#include "cJSON.h"
#else
#include "nlohmann/json.hpp"      // for basic_json<>::object_t, basic_json
#include "nlohmann/json_fwd.hpp"  // for json
#endif

using namespace cspot;

ApResolve::ApResolve(std::string apOverride) {
  this->apOverride = apOverride;
}

std::string ApResolve::fetchFirstApAddress() {
  if (apOverride != "") {
    return apOverride;
  }

  return fetchFirstAddress("http://apresolve.spotify.com/", "ap_list");
}

std::string ApResolve::fetchFirstSpclientAddress() {
  return fetchFirstAddress("http://apresolve.spotify.com/?type=spclient",
                           "spclient");
}

std::string ApResolve::fetchFirstAddress(const std::string& url,
                                         const std::string& jsonKey) {
  auto request = bell::HTTPClient::get(url);
  std::string_view responseStr = request->body();

  // parse json with nlohmann
#ifdef BELL_ONLY_CJSON
  std::string responseCopy(responseStr);
  cJSON* json = cJSON_Parse(responseCopy.c_str());
  cJSON* apList = json ? cJSON_GetObjectItem(json, jsonKey.c_str()) : nullptr;
  cJSON* firstAp = apList ? cJSON_GetArrayItem(apList, 0) : nullptr;
  if (!firstAp || !cJSON_IsString(firstAp)) {
    cJSON_Delete(json);
    throw std::runtime_error("Invalid AP resolve response");
  }
  auto ap_string = std::string(firstAp->valuestring);
  cJSON_Delete(json);
  return ap_string;
#else
  auto json = nlohmann::json::parse(responseStr);
  return json[jsonKey][0];
#endif
}
