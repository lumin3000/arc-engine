#ifndef LOG_H
#define LOG_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_QUIET = 0,
    LOG_LEVEL_NORMAL = 1,
    LOG_LEVEL_VERBOSE = 2,
} LogLevel;

extern int g_log_level;

#define LOG_INFO(fmt, ...) \
    do { if (g_log_level >= LOG_LEVEL_NORMAL) fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)

#define LOG_VERBOSE(fmt, ...) \
    do { if (g_log_level >= LOG_LEVEL_VERBOSE) fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[ERROR] " fmt, ##__VA_ARGS__)

#endif
