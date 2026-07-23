#ifndef WOLFRAM_LOG_H
#define WOLFRAM_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WF_LOG_LEVEL_DEBUG = 0,
    WF_LOG_LEVEL_INFO  = 1,
    WF_LOG_LEVEL_WARN  = 2,
    WF_LOG_LEVEL_ERROR = 3,
} wf_log_level;

static inline int wf_log_level_from_env(void) {
    const char *level = getenv("WOLFRAM_LOG_LEVEL");
    if (!level || !level[0]) return WF_LOG_LEVEL_WARN;
    if (strcmp(level, "debug") == 0 || strcmp(level, "DEBUG") == 0) return WF_LOG_LEVEL_DEBUG;
    if (strcmp(level, "info") == 0 || strcmp(level, "INFO") == 0) return WF_LOG_LEVEL_INFO;
    if (strcmp(level, "warn") == 0 || strcmp(level, "WARN") == 0) return WF_LOG_LEVEL_WARN;
    if (strcmp(level, "error") == 0 || strcmp(level, "ERROR") == 0) return WF_LOG_LEVEL_ERROR;
    char *end = NULL;
    long v = strtol(level, &end, 10);
    if (end && *end == '\0' && v >= 0 && v <= 3) return (int)v;
    return WF_LOG_LEVEL_WARN;
}

static inline void wf_log(int level, const char *module, const char *fmt, ...) {
    static int current_level = -1;
    if (current_level < 0) current_level = wf_log_level_from_env();
    if (level < current_level) return;
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    va_list ap;
    time_t now = time(NULL);
    struct tm tm;
#if defined(__APPLE__)
    localtime_r(&now, &tm);
#else
    localtime_r(&now, &tm);
#endif
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    fprintf(stderr, "Wolfram [%s] [%s] [%s] ", names[level], module, ts);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

#define WF_LOG_DEBUG(module, ...) wf_log(WF_LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#define WF_LOG_INFO(module, ...)  wf_log(WF_LOG_LEVEL_INFO,  module, __VA_ARGS__)
#define WF_LOG_WARN(module, ...)  wf_log(WF_LOG_LEVEL_WARN,  module, __VA_ARGS__)
#define WF_LOG_ERROR(module, ...) wf_log(WF_LOG_LEVEL_ERROR, module, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LOG_H */
