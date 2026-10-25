#pragma once
inline void ao3TestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) ao3TestLog(__VA_ARGS__)
#define LOG_DBG(...) ao3TestLog(__VA_ARGS__)
#define LOG_INF(...) ao3TestLog(__VA_ARGS__)
