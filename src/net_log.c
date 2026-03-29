#include "net_log.h"
#include <string.h>

static NetLog log_instance;

NetLog *net_log_get(void)
{
    return &log_instance;
}

void net_log_clear(void)
{
    log_instance.count = 0;
}

void net_log_add(const char *url, long status_code,
                 size_t response_bytes, double elapsed_ms,
                 int failed, const char *redirect_url)
{
    if (log_instance.count >= NET_LOG_MAX_ENTRIES)
        return;

    NetLogEntry *e = &log_instance.entries[log_instance.count++];
    strncpy(e->url, url, sizeof(e->url) - 1);
    e->url[sizeof(e->url) - 1] = '\0';
    e->redirect_url[0] = '\0';
    if (redirect_url) {
        strncpy(e->redirect_url, redirect_url, sizeof(e->redirect_url) - 1);
        e->redirect_url[sizeof(e->redirect_url) - 1] = '\0';
    }
    e->status_code    = status_code;
    e->response_bytes = response_bytes;
    e->elapsed_ms     = elapsed_ms;
    e->failed         = failed;
}
