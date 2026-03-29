#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

typedef struct {
    char  *data;
    size_t len;
    long   status_code;
} HTTPResponse;

#define HTTP_OK                    0
#define HTTP_ERR_FAILED           -1
#define HTTP_ERR_TOO_MANY_REDIRECTS -2

/* Fetcht eine URL per GET. Folgt Redirects manuell (max 20, wie Chrome).
   Jeder Redirect-Hop wird im net_log sichtbar.
   Aufrufer muss http_response_free() aufrufen. */
int http_get(const char *url, HTTPResponse *resp);

void http_response_free(HTTPResponse *resp);

#endif
