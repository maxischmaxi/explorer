#define _POSIX_C_SOURCE 200809L
#include "resource_fetch.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

/* ---- URL-Aufloesung ---- */

static char *resolve_url(const char *base_url, const char *href)
{
    if (!href || !*href)
        return NULL;

    /* Bereits absolut */
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0)
        return strdup(href);

    /* Protocol-relative: //example.com/... */
    if (strncmp(href, "//", 2) == 0) {
        const char *colon = strchr(base_url, ':');
        size_t scheme_len = colon ? (size_t)(colon - base_url) : 5;
        size_t hlen = strlen(href);
        char *url = malloc(scheme_len + 1 + hlen + 1);
        if (!url) return NULL;
        memcpy(url, base_url, scheme_len);
        url[scheme_len] = ':';
        memcpy(url + scheme_len + 1, href, hlen + 1);
        return url;
    }

    /* Root-relativ: /path/... */
    if (href[0] == '/') {
        const char *p = strstr(base_url, "://");
        if (!p) return strdup(href);
        p += 3;
        const char *slash = strchr(p, '/');
        size_t origin_len = slash ? (size_t)(slash - base_url) : strlen(base_url);
        size_t hlen = strlen(href);
        char *url = malloc(origin_len + hlen + 1);
        if (!url) return NULL;
        memcpy(url, base_url, origin_len);
        memcpy(url + origin_len, href, hlen + 1);
        return url;
    }

    /* Relativ */
    const char *last_slash = strrchr(base_url, '/');
    const char *after_scheme = strstr(base_url, "://");
    if (after_scheme && last_slash <= after_scheme + 2) {
        size_t blen = strlen(base_url);
        size_t hlen = strlen(href);
        char *url = malloc(blen + 1 + hlen + 1);
        if (!url) return NULL;
        memcpy(url, base_url, blen);
        url[blen] = '/';
        memcpy(url + blen + 1, href, hlen + 1);
        return url;
    }

    size_t base_len = (size_t)(last_slash - base_url + 1);
    size_t hlen = strlen(href);
    char *url = malloc(base_len + hlen + 1);
    if (!url) return NULL;
    memcpy(url, base_url, base_len);
    memcpy(url + base_len, href, hlen + 1);
    return url;
}

/* ---- Hilfsfunktionen ---- */

static void add_resource(ResourceCollection *col, char *url, char *data,
                         size_t data_len, ResourceType type, int is_inline)
{
    if (col->count >= RES_MAX_ENTRIES) {
        free(url);
        free(data);
        return;
    }
    Resource *r = &col->entries[col->count++];
    r->url      = url;
    r->data     = data;
    r->data_len = data_len;
    r->type     = type;
    r->is_inline = is_inline;
}

static void fetch_and_add(ResourceCollection *col, const char *base_url,
                          const char *href, ResourceType type)
{
    char *url = resolve_url(base_url, href);
    if (!url) return;

    printf("  [%s] %s\n", resource_type_name(type), url);

    HTTPResponse resp;
    if (http_get(url, &resp) == 0) {
        add_resource(col, url, resp.data, resp.len, type, 0);
        resp.data = NULL;
        http_response_free(&resp);
    } else {
        printf("  FAILED: %s\n", url);
        free(url);
    }
}

/* Holt ein Attribut als C-String (malloc'd) oder NULL. */
static char *get_attr(lxb_dom_element_t *el, const char *name)
{
    size_t val_len = 0;
    const lxb_char_t *val = lxb_dom_element_get_attribute(
        el, (const lxb_char_t *)name, strlen(name), &val_len);
    if (!val || val_len == 0)
        return NULL;
    char *s = malloc(val_len + 1);
    if (!s) return NULL;
    memcpy(s, val, val_len);
    s[val_len] = '\0';
    return s;
}

/* Prueft ob ein Attribut einen bestimmten Wert hat (case-insensitiv). */
static int attr_eq(lxb_dom_element_t *el, const char *name, const char *expected)
{
    char *val = get_attr(el, name);
    if (!val) return 0;
    int eq = (strcasecmp(val, expected) == 0);
    free(val);
    return eq;
}

/* Erkennt Font-Dateien anhand der Endung. */
static int is_font_url(const char *url)
{
    size_t len = strlen(url);
    if (len < 5) return 0;
    const char *ext = url + len - 4;
    return (strcasecmp(ext, ".ttf") == 0 ||
            strcasecmp(ext, ".otf") == 0 ||
            strcasecmp(ext, ".eot") == 0 ||
            (len >= 6 && strcasecmp(url + len - 5, ".woff") == 0) ||
            (len >= 7 && strcasecmp(url + len - 6, ".woff2") == 0));
}

/* Erkennt Bild-Dateien anhand der Endung. */
static int is_image_url(const char *url)
{
    size_t len = strlen(url);
    if (len < 5) return 0;
    const char *ext = url + len - 4;
    return (strcasecmp(ext, ".png") == 0 ||
            strcasecmp(ext, ".jpg") == 0 ||
            strcasecmp(ext, ".gif") == 0 ||
            strcasecmp(ext, ".svg") == 0 ||
            strcasecmp(ext, ".ico") == 0 ||
            (len >= 6 && strcasecmp(url + len - 5, ".jpeg") == 0) ||
            (len >= 6 && strcasecmp(url + len - 5, ".webp") == 0));
}

/* ---- Elemente nach Tag-Name sammeln ---- */

static void collect_elements(lxb_dom_element_t *root,
                             lxb_dom_collection_t *col,
                             const char *tag)
{
    lxb_dom_elements_by_tag_name(root, col,
                                 (const lxb_char_t *)tag, strlen(tag));
}

/* ---- Hauptfunktion ---- */

int resource_fetch_all(const char *html, size_t html_len,
                       const char *base_url, ResourceCollection *out)
{
    memset(out, 0, sizeof(*out));
    if (!html || html_len == 0)
        return 0;

    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) return -1;

    lxb_status_t status = lxb_html_document_parse(
        doc, (const lxb_char_t *)html, html_len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return -1;
    }

    /* <html>-Element als Root fuer die Suche — enthaelt <head> und <body> */
    lxb_dom_element_t *root = lxb_dom_document_element(
        &doc->dom_document);
    if (!root) {
        lxb_html_document_destroy(doc);
        return 0;
    }

    lxb_dom_collection_t *col = lxb_dom_collection_create(
        lxb_dom_interface_document(doc));
    if (!col) {
        lxb_html_document_destroy(doc);
        return -1;
    }
    lxb_dom_collection_init(col, 64);

    /* --- <link> Tags: CSS, Favicon, etc. --- */
    collect_elements(root, col, "link");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *href = get_attr(el, "href");
        if (!href) continue;

        if (attr_eq(el, "rel", "stylesheet")) {
            fetch_and_add(out, base_url, href, RES_TYPE_CSS);
        } else if (attr_eq(el, "rel", "icon") ||
                   attr_eq(el, "rel", "shortcut icon") ||
                   attr_eq(el, "rel", "apple-touch-icon")) {
            fetch_and_add(out, base_url, href, RES_TYPE_IMAGE);
        } else if (attr_eq(el, "rel", "preload")) {
            char *as = get_attr(el, "as");
            if (as) {
                if (strcasecmp(as, "font") == 0)
                    fetch_and_add(out, base_url, href, RES_TYPE_FONT);
                else if (strcasecmp(as, "style") == 0)
                    fetch_and_add(out, base_url, href, RES_TYPE_CSS);
                else if (strcasecmp(as, "script") == 0)
                    fetch_and_add(out, base_url, href, RES_TYPE_JS);
                else if (strcasecmp(as, "image") == 0)
                    fetch_and_add(out, base_url, href, RES_TYPE_IMAGE);
                free(as);
            }
        }
        free(href);
    }
    lxb_dom_collection_clean(col);

    /* --- <script> Tags --- */
    collect_elements(root, col, "script");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *src = get_attr(el, "src");
        if (src) {
            fetch_and_add(out, base_url, src, RES_TYPE_JS);
            free(src);
        }
    }
    lxb_dom_collection_clean(col);

    /* --- <img> Tags --- */
    collect_elements(root, col, "img");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *src = get_attr(el, "src");
        if (src) {
            fetch_and_add(out, base_url, src, RES_TYPE_IMAGE);
            free(src);
        }
    }
    lxb_dom_collection_clean(col);

    /* --- <style> Inline-Blocks --- */
    collect_elements(root, col, "style");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        lxb_dom_node_t *child = lxb_dom_node_first_child(
            lxb_dom_interface_node(el));

        while (child) {
            if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                size_t text_len;
                const lxb_char_t *text = lxb_dom_node_text_content(
                    child, &text_len);
                if (text && text_len > 0) {
                    char *data = malloc(text_len + 1);
                    if (data) {
                        memcpy(data, text, text_len);
                        data[text_len] = '\0';
                        add_resource(out, strdup("<inline style>"), data,
                                     text_len, RES_TYPE_CSS, 1);
                    }
                }
            }
            child = lxb_dom_node_next(child);
        }
    }
    lxb_dom_collection_clean(col);

    /* --- @font-face URLs aus CSS extrahieren (einfach) --- */
    for (int i = 0; i < out->count; i++) {
        if (out->entries[i].type != RES_TYPE_CSS || !out->entries[i].data)
            continue;

        const char *css = out->entries[i].data;
        const char *p = css;

        while ((p = strstr(p, "url(")) != NULL) {
            p += 4;
            char quote = 0;
            if (*p == '"' || *p == '\'') { quote = *p; p++; }

            const char *start = p;
            const char *end;
            if (quote) {
                end = strchr(p, quote);
            } else {
                end = strchr(p, ')');
            }
            if (!end) break;

            size_t ulen = (size_t)(end - start);
            if (ulen > 0 && ulen < 2048 && strncmp(start, "data:", 5) != 0) {
                char href[2048];
                memcpy(href, start, ulen);
                href[ulen] = '\0';

                /* CSS-URL relativ zum CSS-File auflösen */
                const char *css_base = out->entries[i].is_inline
                    ? base_url : out->entries[i].url;

                if (is_font_url(href))
                    fetch_and_add(out, css_base, href, RES_TYPE_FONT);
                else if (is_image_url(href))
                    fetch_and_add(out, css_base, href, RES_TYPE_IMAGE);
            }

            p = end + 1;
        }
    }

    lxb_dom_collection_destroy(col, true);
    lxb_html_document_destroy(doc);

    printf("  Total: %d resources fetched\n", out->count);
    return 0;
}

/* ---- Queue-basiertes inkrementelles Fetching ---- */

static void queue_add(PendingQueue *q, const char *base_url,
                      const char *href, ResourceType type)
{
    if (q->count >= PENDING_MAX) return;
    char *url = resolve_url(base_url, href);
    if (!url) return;
    q->items[q->count].url  = url;
    q->items[q->count].type = type;
    q->count++;
}

int resource_extract_urls(const char *html, size_t html_len,
                          const char *base_url, PendingQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
    if (!html || html_len == 0) return 0;

    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) return -1;

    lxb_status_t status = lxb_html_document_parse(
        doc, (const lxb_char_t *)html, html_len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return -1;
    }

    lxb_dom_element_t *root = lxb_dom_document_element(&doc->dom_document);
    if (!root) { lxb_html_document_destroy(doc); return 0; }

    lxb_dom_collection_t *col = lxb_dom_collection_create(
        lxb_dom_interface_document(doc));
    if (!col) { lxb_html_document_destroy(doc); return -1; }
    lxb_dom_collection_init(col, 64);

    /* <link> Tags */
    collect_elements(root, col, "link");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *href = get_attr(el, "href");
        if (!href) continue;
        if (attr_eq(el, "rel", "stylesheet")) {
            queue_add(queue, base_url, href, RES_TYPE_CSS);
        } else if (attr_eq(el, "rel", "icon") ||
                   attr_eq(el, "rel", "shortcut icon")) {
            queue_add(queue, base_url, href, RES_TYPE_IMAGE);
        } else if (attr_eq(el, "rel", "preload")) {
            char *as = get_attr(el, "as");
            if (as) {
                if (strcasecmp(as, "font") == 0)
                    queue_add(queue, base_url, href, RES_TYPE_FONT);
                else if (strcasecmp(as, "style") == 0)
                    queue_add(queue, base_url, href, RES_TYPE_CSS);
                else if (strcasecmp(as, "script") == 0)
                    queue_add(queue, base_url, href, RES_TYPE_JS);
                else if (strcasecmp(as, "image") == 0)
                    queue_add(queue, base_url, href, RES_TYPE_IMAGE);
                free(as);
            }
        }
        free(href);
    }
    lxb_dom_collection_clean(col);

    /* <script> Tags */
    collect_elements(root, col, "script");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *src = get_attr(el, "src");
        if (src) { queue_add(queue, base_url, src, RES_TYPE_JS); free(src); }
    }
    lxb_dom_collection_clean(col);

    /* <img> Tags */
    collect_elements(root, col, "img");
    for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(col, i);
        char *src = get_attr(el, "src");
        if (src) { queue_add(queue, base_url, src, RES_TYPE_IMAGE); free(src); }
    }
    lxb_dom_collection_clean(col);

    lxb_dom_collection_destroy(col, true);
    lxb_html_document_destroy(doc);

    printf("  Queued %d resources for fetching\n", queue->count);
    return 0;
}

int resource_fetch_next(PendingQueue *queue, ResourceCollection *col)
{
    if (queue->next >= queue->count)
        return 0;

    PendingResource *pr = &queue->items[queue->next++];
    printf("  [%s] %s\n", resource_type_name(pr->type), pr->url);

    HTTPResponse resp;
    if (http_get(pr->url, &resp) == 0) {
        if (col->count < RES_MAX_ENTRIES) {
            Resource *r = &col->entries[col->count++];
            r->url      = strdup(pr->url);
            r->data     = resp.data;
            r->data_len = resp.len;
            r->type     = pr->type;
            r->is_inline = 0;
            resp.data = NULL;
        }
        http_response_free(&resp);
    } else {
        printf("  FAILED: %s\n", pr->url);
    }

    return 1;
}

void pending_queue_free(PendingQueue *queue)
{
    for (int i = 0; i < queue->count; i++)
        free(queue->items[i].url);
    memset(queue, 0, sizeof(*queue));
}

void resource_collection_free(ResourceCollection *col)
{
    for (int i = 0; i < col->count; i++) {
        free(col->entries[i].url);
        free(col->entries[i].data);
    }
    col->count = 0;
}

int resource_extract_inline_css(const char *html, size_t html_len,
                                ResourceCollection *col)
{
    if (!html || html_len == 0) return 0;

    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) return -1;

    lxb_status_t status = lxb_html_document_parse(
        doc, (const lxb_char_t *)html, html_len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return -1;
    }

    lxb_dom_element_t *root = lxb_dom_document_element(&doc->dom_document);
    if (!root) { lxb_html_document_destroy(doc); return 0; }

    lxb_dom_collection_t *elcol = lxb_dom_collection_create(
        lxb_dom_interface_document(doc));
    if (!elcol) { lxb_html_document_destroy(doc); return -1; }
    lxb_dom_collection_init(elcol, 16);

    collect_elements(root, elcol, "style");
    int found = 0;
    for (size_t i = 0; i < lxb_dom_collection_length(elcol); i++) {
        lxb_dom_element_t *el = lxb_dom_collection_element(elcol, i);
        lxb_dom_node_t *child = lxb_dom_node_first_child(
            lxb_dom_interface_node(el));

        while (child) {
            if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
                size_t text_len;
                const lxb_char_t *text = lxb_dom_node_text_content(
                    child, &text_len);
                if (text && text_len > 0) {
                    char *data = malloc(text_len + 1);
                    if (data) {
                        memcpy(data, text, text_len);
                        data[text_len] = '\0';
                        add_resource(col, strdup("<inline style>"), data,
                                     text_len, RES_TYPE_CSS, 1);
                        found++;
                    }
                }
            }
            child = lxb_dom_node_next(child);
        }
    }

    lxb_dom_collection_destroy(elcol, true);
    lxb_html_document_destroy(doc);
    printf("  Extracted %d inline <style> blocks\n", found);
    return found;
}

const char *resource_type_name(ResourceType type)
{
    switch (type) {
    case RES_TYPE_CSS:   return "CSS";
    case RES_TYPE_JS:    return "JS";
    case RES_TYPE_IMAGE: return "IMG";
    case RES_TYPE_FONT:  return "FONT";
    case RES_TYPE_OTHER: return "OTHER";
    }
    return "?";
}
