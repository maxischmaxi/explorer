#ifndef RESOURCE_FETCH_H
#define RESOURCE_FETCH_H

#include <stddef.h>

#define RES_MAX_ENTRIES 128

typedef enum {
    RES_TYPE_CSS,
    RES_TYPE_JS,
    RES_TYPE_IMAGE,
    RES_TYPE_FONT,
    RES_TYPE_OTHER
} ResourceType;

typedef struct {
    char        *url;
    char        *data;
    size_t       data_len;
    ResourceType type;
    int          is_inline;
} Resource;

typedef struct {
    Resource entries[RES_MAX_ENTRIES];
    int      count;
} ResourceCollection;

/* Pending-Queue: extrahierte URLs die noch gefetcht werden muessen. */
typedef struct {
    char         *url;
    ResourceType  type;
} PendingResource;

#define PENDING_MAX 256

typedef struct {
    PendingResource items[PENDING_MAX];
    int             count;
    int             next;   /* naechster zu fetchender Index */
} PendingQueue;

/* Parst HTML mit lexbor, findet alle verlinkten Ressourcen,
   loest URLs relativ zu base_url auf und fetcht sie.
   Aufrufer muss resource_collection_free() aufrufen. */
int resource_fetch_all(const char *html, size_t html_len,
                       const char *base_url, ResourceCollection *out);

/* Extrahiert URLs aus HTML ohne zu fetchen. Fuellt pending-Queue.
   Aufrufer muss pending_queue_free() aufrufen. */
int resource_extract_urls(const char *html, size_t html_len,
                          const char *base_url, PendingQueue *queue);

/* Fetcht den naechsten Eintrag aus der Queue.
   Gibt 1 zurueck wenn ein Fetch gemacht wurde, 0 wenn Queue leer. */
int resource_fetch_next(PendingQueue *queue, ResourceCollection *col);

/* Extrahiert Inline-<style>-Bloecke aus HTML direkt in die Collection.
   Braucht keinen Fetch — die Daten sind schon im HTML. */
int resource_extract_inline_css(const char *html, size_t html_len,
                                ResourceCollection *col);

void pending_queue_free(PendingQueue *queue);
void resource_collection_free(ResourceCollection *col);

const char *resource_type_name(ResourceType type);

#endif
