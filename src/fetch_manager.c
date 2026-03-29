#define _POSIX_C_SOURCE 200809L
#include "fetch_manager.h"
#include "http.h"
#include "net_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

#define MAX_PARALLEL    6
#define MAX_REDIRECTS  20

/* ---- Prioritaet (niedrigerer Wert = hoehere Prioritaet) ---- */

static int priority_for_type(ResourceType t)
{
    switch (t) {
    case RES_TYPE_CSS:   return 0;
    case RES_TYPE_JS:    return 1;
    case RES_TYPE_FONT:  return 2;
    case RES_TYPE_IMAGE: return 3;
    default:             return 4;
    }
}

/* ---- Aktiver Transfer ---- */

typedef struct {
    CURL         *easy;
    char         *url;
    ResourceType  type;
    char         *data;
    size_t        data_len;
    struct timespec t0;
    int           active;
} ActiveTransfer;

/* ---- State ---- */

static CURLM          *multi;
static ActiveTransfer  transfers[MAX_PARALLEL];
static int             active_count;

static PendingQueue   *queue;
static int             queue_pos;

/* ---- curl write callback ---- */

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    ActiveTransfer *t = userdata;

    char *tmp = realloc(t->data, t->data_len + total + 1);
    if (!tmp) return 0;

    t->data = tmp;
    memcpy(t->data + t->data_len, ptr, total);
    t->data_len += total;
    t->data[t->data_len] = '\0';

    return total;
}

/* ---- Slot-Management ---- */

static ActiveTransfer *find_free_slot(void)
{
    for (int i = 0; i < MAX_PARALLEL; i++) {
        if (!transfers[i].active)
            return &transfers[i];
    }
    return NULL;
}

static ActiveTransfer *find_by_easy(CURL *easy)
{
    for (int i = 0; i < MAX_PARALLEL; i++) {
        if (transfers[i].active && transfers[i].easy == easy)
            return &transfers[i];
    }
    return NULL;
}

static void start_transfer(PendingResource *pr)
{
    ActiveTransfer *t = find_free_slot();
    if (!t) return;

    CURL *easy = curl_easy_init();
    if (!easy) return;

    memset(t, 0, sizeof(*t));
    t->easy   = easy;
    t->url    = strdup(pr->url);
    t->type   = pr->type;
    t->data   = NULL;
    t->data_len = 0;
    t->active = 1;
    clock_gettime(CLOCK_MONOTONIC, &t->t0);

    curl_easy_setopt(easy, CURLOPT_URL, pr->url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, (long)MAX_REDIRECTS);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Explorer/0.1");
    curl_easy_setopt(easy, CURLOPT_PRIVATE, t);

    curl_multi_add_handle(multi, easy);
    active_count++;
}

static void finish_transfer(ActiveTransfer *t, CURLcode result,
                            ResourceCollection *col)
{
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t->t0.tv_sec) * 1000.0
                   + (double)(t1.tv_nsec - t->t0.tv_nsec) / 1e6;

    if (result != CURLE_OK) {
        net_log_add(t->url, 0, 0, elapsed, 1, NULL);
        printf("  FAILED: %s\n", t->url);
    } else {
        long status = 0;
        curl_easy_getinfo(t->easy, CURLINFO_RESPONSE_CODE, &status);

        /* Effektive URL (nach Redirects) */
        char *effective_url = NULL;
        curl_easy_getinfo(t->easy, CURLINFO_EFFECTIVE_URL, &effective_url);

        /* Redirect loggen wenn URL sich geaendert hat */
        if (effective_url && strcmp(effective_url, t->url) != 0) {
            net_log_add(t->url, 302, 0, 0, 0, effective_url);
        }

        net_log_add(effective_url ? effective_url : t->url,
                    status, t->data_len, elapsed, 0, NULL);

        printf("  [%s] %ld %s (%zu bytes, %.0fms)\n",
               resource_type_name(t->type), status, t->url,
               t->data_len, elapsed);

        /* In ResourceCollection einfuegen */
        if (col->count < RES_MAX_ENTRIES) {
            Resource *r = &col->entries[col->count++];
            r->url      = strdup(effective_url ? effective_url : t->url);
            r->data     = t->data;
            r->data_len = t->data_len;
            r->type     = t->type;
            r->is_inline = 0;
            t->data = NULL; /* Ownership uebertragen */
        }
    }

    curl_multi_remove_handle(multi, t->easy);
    curl_easy_cleanup(t->easy);
    free(t->data);
    free(t->url);
    t->active = 0;
    active_count--;
}

/* ---- Sortierung ---- */

static int cmp_priority(const void *a, const void *b)
{
    const PendingResource *pa = a;
    const PendingResource *pb = b;
    return priority_for_type(pa->type) - priority_for_type(pb->type);
}

/* ---- Public API ---- */

void fetch_manager_init(void)
{
    multi = curl_multi_init();
    active_count = 0;
    queue = NULL;
    queue_pos = 0;
    memset(transfers, 0, sizeof(transfers));
}

void fetch_manager_start(PendingQueue *q)
{
    fetch_manager_abort();
    queue = q;
    queue_pos = 0;

    /* Queue nach Prioritaet sortieren */
    if (queue && queue->count > 0) {
        qsort(queue->items, (size_t)queue->count,
              sizeof(PendingResource), cmp_priority);
    }
}

int fetch_manager_poll(ResourceCollection *col)
{
    if (!multi) return 0;

    /* Freie Slots mit neuen Transfers fuellen */
    while (active_count < MAX_PARALLEL && queue &&
           queue_pos < queue->count) {
        start_transfer(&queue->items[queue_pos++]);
    }

    /* Non-blocking perform */
    int still_running = 0;
    curl_multi_perform(multi, &still_running);

    /* Fertige Transfers einsammeln */
    int msgs_left;
    CURLMsg *msg;
    while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            ActiveTransfer *t = find_by_easy(msg->easy_handle);
            if (t) {
                finish_transfer(t, msg->data.result, col);
            }
        }
    }

    int remaining = 0;
    if (queue)
        remaining = queue->count - queue_pos;
    return active_count + remaining;
}

void fetch_manager_abort(void)
{
    if (!multi) return;

    for (int i = 0; i < MAX_PARALLEL; i++) {
        if (transfers[i].active) {
            curl_multi_remove_handle(multi, transfers[i].easy);
            curl_easy_cleanup(transfers[i].easy);
            free(transfers[i].data);
            free(transfers[i].url);
            transfers[i].active = 0;
        }
    }
    active_count = 0;
    queue = NULL;
    queue_pos = 0;
}

void fetch_manager_cleanup(void)
{
    fetch_manager_abort();
    if (multi) {
        curl_multi_cleanup(multi);
        multi = NULL;
    }
}
