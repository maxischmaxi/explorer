#ifndef RENDERER_H
#define RENDERER_H

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

/* Initialisiert den Text-Renderer mit einer TTF-Datei.
   dpi_scale: Verhaeltnis framebuffer_width / window_width (z.B. 2.0 bei HiDPI).
   Gibt 0 bei Erfolg zurueck, -1 bei Fehler. */
int renderer_init(const char *font_path, int font_size, float dpi_scale);

/* Rendert einen String an Position (x, y) in logischen Pixeln.
   Farbe als RGB float [0..1]. fb_width/fb_height = Framebuffer-Groesse. */
void renderer_draw_text(const char *text, float x, float y,
                        float r, float g, float b,
                        int fb_width, int fb_height);

/* Rendert ein gefuelltes Rechteck in logischen Pixeln. */
void renderer_draw_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a,
                        int fb_width, int fb_height);

/* Rendert Text mit Skalierungsfaktor (1.0 = normal). */
void renderer_draw_text_scaled(const char *text, float x, float y, float text_scale,
                               float r, float g, float b,
                               int fb_width, int fb_height);

/* Gibt die Breite eines skalierten Strings in Pixeln zurueck. */
float renderer_text_width_scaled(const char *text, float text_scale);

/* Rendert eine Textur (Bild) als Rechteck. */
void renderer_draw_image(GLuint texture, float x, float y, float w, float h,
                         int fb_width, int fb_height);

/* Gibt die Breite eines Strings in Pixeln zurueck. */
float renderer_text_width(const char *text);

/* Gibt die Zeilenhoehe in Pixeln zurueck. */
float renderer_line_height(void);

/* Gibt alle Ressourcen frei. */
void renderer_cleanup(void);

#endif
