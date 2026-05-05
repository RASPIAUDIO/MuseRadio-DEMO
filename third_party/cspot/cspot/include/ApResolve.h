#pragma once

#include <string>  // for string
#ifdef BELL_ONLY_CJSON
#include "cJSON.h"
#else
#endif

namespace cspot {
class ApResolve {
 public:
  ApResolve(std::string apOverride);

  /**
   * @brief Connects to spotify's servers and returns first valid ap address
   * @returns std::string Address in form of url:port
   */
  std::string fetchFirstApAddress();
  std::string fetchFirstSpclientAddress();

 private:
  std::string fetchFirstAddress(const std::string& url,
                                const std::string& jsonKey);
  std::string apOverride;
};
}  // namespace cspot
