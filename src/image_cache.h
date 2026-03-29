#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <stddef.h>

typedef struct {
    GLuint texture;
    int    width;
    int    height;
} CachedImage;

/* Sucht ein Bild im Cache. Gibt NULL zurueck wenn nicht gefunden. */
const CachedImage *image_cache_get(const char *url);

/* Dekodiert Bilddaten und laedt sie als GL-Textur in den Cache. */
void image_cache_add(const char *url, const void *data, size_t len);

/* Gibt alle Texturen und Cache-Eintraege frei. */
void image_cache_clear(void);

#endif
