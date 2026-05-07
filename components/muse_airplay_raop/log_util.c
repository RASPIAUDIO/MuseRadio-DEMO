#include "log_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <strings.h>

#include "esp_timer.h"

log_level raop_loglevel = lINFO;
log_level util_loglevel = lWARN;

u32_t _gettime_ms_(void)
{
  return (u32_t)(esp_timer_get_time() / 1000ULL);
}

const char *logtime(void)
{
  static char timebuf[16];
  snprintf(timebuf, sizeof(timebuf), "%010u", (unsigned)_gettime_ms_());
  return timebuf;
}

void logprint(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

log_level debug2level(char *level)
{
  if (!level) return lINFO;
  if (!strcasecmp(level, "error")) return lERROR;
  if (!strcasecmp(level, "warn")) return lWARN;
  if (!strcasecmp(level, "info")) return lINFO;
  if (!strcasecmp(level, "debug")) return lDEBUG;
  if (!strcasecmp(level, "sdebug")) return lSDEBUG;
  return lINFO;
}

char *level2debug(log_level level)
{
  switch (level) {
    case lERROR: return (char *)"error";
    case lWARN: return (char *)"warn";
    case lINFO: return (char *)"info";
    case lDEBUG: return (char *)"debug";
    case lSDEBUG: return (char *)"sdebug";
    default: return (char *)"info";
  }
}

