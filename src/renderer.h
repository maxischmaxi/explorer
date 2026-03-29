#ifndef RENDERER_H
#define RENDERER_H

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include <stddef.h>

/* Initialisiert den Text-Renderer mit einer TTF-Datei.
   dpi_scale: Verhaeltnis framebuffer_width / window_width (z.B. 2.0 bei HiDPI).
   Gibt 0 bei Erfolg zurueck, -1 bei Fehler. */
int renderer_init(const char *font_path, int font_size, float dpi_scale);

/* Rendert einen UTF-8-String an Position (x, y) in logischen Pixeln
   mit spezifischer Font-Groesse und Font-ID. */
void renderer_draw_text_sized(const char *text, float x, float y,
                              float font_size, uint8_t font_id,
                              float r, float g, float b,
                              int fb_width, int fb_height);

/* Rendert einen UTF-8-String an Position (x, y) mit Default-Font/Groesse. */
void renderer_draw_text(const char *text, float x, float y,
                        float r, float g, float b,
                        int fb_width, int fb_height);

/* Gibt die Breite eines UTF-8-Strings bei spezifischer Groesse/Font zurueck. */
float renderer_text_width_sized(const char *text, float font_size, uint8_t font_id);

/* Gibt die Breite eines Strings bei Default-Font/Groesse zurueck. */
float renderer_text_width(const char *text);

/* Gibt die Zeilenhoehe fuer eine spezifische Font-Groesse zurueck (logische Pixel). */
float renderer_line_height_sized(float font_size);

/* Gibt die Zeilenhoehe bei Default-Groesse zurueck. */
float renderer_line_height(void);

/* Rendert ein gefuelltes Rechteck in logischen Pixeln. */
void renderer_draw_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a,
                        int fb_width, int fb_height);

/* Rendert eine Textur (Bild) als Rechteck. */
void renderer_draw_image(GLuint texture, float x, float y, float w, float h,
                         int fb_width, int fb_height);

/* Laedt einen Font aus dem Speicher (TTF/OTF/WOFF).
   Gibt font_id (>0) zurueck, oder 0 bei Fehler.
   Registriert automatisch den family_name in der Font-Registry. */
int renderer_load_font_mem(const void *data, size_t len);

/* Registriert einen Font unter einem Familiennamen (case-insensitive). */
void renderer_register_font(uint8_t font_id, const char *family_name);

/* Loest eine komma-separierte font-family Liste auf.
   Gibt die Font-ID des ersten verfuegbaren Fonts zurueck (0 = default). */
uint8_t renderer_resolve_font_family(const char *family_list);

/* Leert den Glyph-Cache (z.B. bei Navigation zu neuer Seite). */
void renderer_cache_clear(void);

/* Gibt alle Ressourcen frei. */
void renderer_cleanup(void);

#endif
