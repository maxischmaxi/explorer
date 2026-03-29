#define _POSIX_C_SOURCE 200809L
#include "image_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#define CACHE_MAX 256

typedef struct {
    char        *url;
    CachedImage  img;
} CacheEntry;

static CacheEntry entries[CACHE_MAX];
static int        count;

const CachedImage *image_cache_get(const char *url)
{
    if (!url) return NULL;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].url, url) == 0)
            return &entries[i].img;
    }
    return NULL;
}

void image_cache_add(const char *url, const void *data, size_t len)
{
    if (!url || !data || len == 0) return;
    if (count >= CACHE_MAX) return;

    /* Bereits im Cache? */
    if (image_cache_get(url)) return;

    int w, h, channels;
    unsigned char *pixels = stbi_load_from_memory(
        (const unsigned char *)data, (int)len, &w, &h, &channels, 4);
    if (!pixels) {
        printf("  IMG decode failed: %s\n", url);
        return;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);

    CacheEntry *e = &entries[count++];
    e->url = strdup(url);
    e->img.texture = tex;
    e->img.width   = w;
    e->img.height  = h;
}

void image_cache_clear(void)
{
    for (int i = 0; i < count; i++) {
        if (entries[i].img.texture)
            glDeleteTextures(1, &entries[i].img.texture);
        free(entries[i].url);
    }
    count = 0;
}
