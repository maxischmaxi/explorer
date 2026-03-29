#define _POSIX_C_SOURCE 200809L
#include "http.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

#include "net_log.h"

#define MAX_REDIRECTS 20

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    HTTPResponse *resp = userdata;

    char *tmp = realloc(resp->data, resp->len + total + 1);
    if (!tmp)
        return 0;

    resp->data = tmp;
    memcpy(resp->data + resp->len, ptr, total);
    resp->len += total;
    resp->data[resp->len] = '\0';

    return total;
}

static int is_redirect(long code)
{
    return code == 301 || code == 302 || code == 303 ||
           code == 307 || code == 308;
}

int http_get(const char *url, HTTPResponse *resp)
{
    memset(resp, 0, sizeof(*resp));

    char *current_url = strdup(url);
    if (!current_url)
        return HTTP_ERR_FAILED;

    for (int i = 0; i <= MAX_REDIRECTS; i++) {
        CURL *curl = curl_easy_init();
        if (!curl) {
            free(current_url);
            return HTTP_ERR_FAILED;
        }

        /* Response-Buffer fuer diesen Hop zuruecksetzen */
        free(resp->data);
        resp->data = NULL;
        resp->len = 0;
        resp->status_code = 0;

        curl_easy_setopt(curl, CURLOPT_URL, current_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Explorer/0.1");

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        CURLcode res = curl_easy_perform(curl);

        clock_gettime(CLOCK_MONOTONIC, &t1);
        double elapsed = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                       + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;

        if (res != CURLE_OK) {
            net_log_add(current_url, 0, 0, elapsed, 1, NULL);
            http_response_free(resp);
            curl_easy_cleanup(curl);
            free(current_url);
            return HTTP_ERR_FAILED;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->status_code);

        if (is_redirect(resp->status_code)) {
            /* Redirect-URL von curl holen (bereits absolut aufgeloest) */
            char *redir = NULL;
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redir);

            if (!redir || !*redir) {
                /* Kein Location-Header — behandle wie finale Response */
                net_log_add(current_url, resp->status_code, resp->len,
                            elapsed, 0, NULL);
                curl_easy_cleanup(curl);
                free(current_url);
                return HTTP_OK;
            }

            net_log_add(current_url, resp->status_code, resp->len,
                        elapsed, 0, redir);

            /* Naechsten Hop vorbereiten */
            free(current_url);
            current_url = strdup(redir);
            curl_easy_cleanup(curl);

            if (!current_url)
                return HTTP_ERR_FAILED;

            continue;
        }

        /* Finale Response (kein Redirect) */
        net_log_add(current_url, resp->status_code, resp->len,
                    elapsed, 0, NULL);
        curl_easy_cleanup(curl);
        free(current_url);
        return HTTP_OK;
    }

    /* Zu viele Redirects */
    net_log_add(current_url, 0, 0, 0, 1, "ERR_TOO_MANY_REDIRECTS");
    http_response_free(resp);
    free(current_url);
    return HTTP_ERR_TOO_MANY_REDIRECTS;
}

void http_response_free(HTTPResponse *resp)
{
    free(resp->data);
    resp->data = NULL;
    resp->len = 0;
}
