#pragma once

#ifdef __cplusplus
#include <cstring>

namespace bell {
inline const char* strstr(const char* haystack, const char* needle) {
  return std::strstr(haystack, needle);
}

inline char* strstr(char* haystack, const char* needle) {
  return std::strstr(haystack, needle);
}
}  // namespace bell
#else
#include <string.h>
#endif
