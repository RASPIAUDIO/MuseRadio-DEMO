#pragma once

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { lERROR = 0, lWARN, lINFO, lDEBUG, lSDEBUG } log_level;

extern log_level raop_loglevel;
extern log_level util_loglevel;

const char *logtime(void);
void logprint(const char *fmt, ...);
log_level debug2level(char *level);
char *level2debug(log_level level);
u32_t _gettime_ms_(void);

#define LOG_ERROR(fmt, ...) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  if (*loglevel >= lWARN) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  if (*loglevel >= lINFO) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) if (*loglevel >= lDEBUG) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_SDEBUG(fmt, ...) if (*loglevel >= lSDEBUG) logprint("%s %s:%d " fmt "\n", logtime(), __FUNCTION__, __LINE__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

