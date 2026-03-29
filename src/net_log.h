#ifndef NET_LOG_H
#define NET_LOG_H

#include <stddef.h>

#define NET_LOG_MAX_ENTRIES 256

typedef struct {
    char   url[2048];
    char   redirect_url[2048];
    long   status_code;
    size_t response_bytes;
    double elapsed_ms;
    int    failed;
} NetLogEntry;

typedef struct {
    NetLogEntry entries[NET_LOG_MAX_ENTRIES];
    int         count;
} NetLog;

NetLog *net_log_get(void);
void    net_log_clear(void);
void    net_log_add(const char *url, long status_code,
                    size_t response_bytes, double elapsed_ms,
                    int failed, const char *redirect_url);

#endif
