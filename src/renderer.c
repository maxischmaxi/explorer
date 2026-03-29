#define _POSIX_C_SOURCE 200809L
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* ---- Font Management ---- */

#define MAX_FONTS 32

typedef struct {
    FT_Face  face;
    uint8_t *mem;       /* Daten fuer memory-geladene Fonts (muss leben bleiben) */
    int      active;
} FontSlot;

static FT_Library ft_lib;
static FontSlot   fonts[MAX_FONTS];

/* Fallback-Font-IDs fuer Zeichen die im Primaer-Font nicht vorhanden sind */
#define MAX_FALLBACKS 8
static int fallback_ids[MAX_FALLBACKS];
static int fallback_count;

/* Font-Registry: mappt Familiennamen auf Font-IDs */
#define MAX_FONT_NAMES 64
static struct { char name[64]; uint8_t font_id; } font_registry[MAX_FONT_NAMES];
static int font_registry_count;

/* ---- Glyph Cache (Open-Addressing Hash Map) ---- */

typedef struct {
    uint32_t codepoint;
    uint16_t size_px;
    uint8_t  font_id;
    uint8_t  occupied;
    GLuint   texture;
    int      width, height;
    int      bearing_x, bearing_y;
    int      advance;   /* in Pixeln bei size_px */
} GCEntry;

#define GCACHE_SIZE  16384
#define GCACHE_PROBE 64
#define GCACHE_MAX_LOAD 12288  /* 75% */

static GCEntry gcache[GCACHE_SIZE];
static int     gcache_count;

/* ---- Line-Height Cache ---- */

#define LH_CACHE_SIZE 64

typedef struct {
    uint16_t size_px;
    uint8_t  font_id;
    uint8_t  valid;
    float    line_height;  /* in Framebuffer-Pixeln */
} LHEntry;

static LHEntry lh_cache[LH_CACHE_SIZE];

/* ---- Globale State ---- */

static float  g_dpi_scale;
static float  g_default_size;  /* Default Font-Groesse in logischen Pixeln */
static GLuint text_shader;
static GLuint text_vao, text_vbo;
static GLuint rect_shader;
static GLuint rect_vao, rect_vbo;
static GLuint img_shader;
static GLuint img_vao, img_vbo;

/* Cached Uniform Locations */
static GLint text_proj_loc, text_color_loc;
static GLint rect_proj_loc, rect_color_loc;
static GLint img_proj_loc;

/* Cached Projection Matrix */
static float cached_proj[16];
static int   cached_fb_w, cached_fb_h;

/* ---- Shader-Hilfsfunktionen ---- */

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

static GLuint link_program(GLuint vert, GLuint frag)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "Shader link error: %s\n", log);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return p;
}

/* ---- Shader-Quellen ---- */

static const char *text_vert_src =
    "#version 330 core\n"
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "    TexCoords = vertex.zw;\n"
    "}\n";

static const char *text_frag_src =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "out vec4 color;\n"
    "uniform sampler2D text_tex;\n"
    "uniform vec3 textColor;\n"
    "void main() {\n"
    "    float a = texture(text_tex, TexCoords).r;\n"
    "    color = vec4(textColor, a);\n"
    "}\n";

static const char *rect_vert_src =
    "#version 330 core\n"
    "layout (location = 0) in vec2 pos;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *rect_frag_src =
    "#version 330 core\n"
    "out vec4 color;\n"
    "uniform vec4 rectColor;\n"
    "void main() {\n"
    "    color = rectColor;\n"
    "}\n";

static const char *img_vert_src =
    "#version 330 core\n"
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "uniform mat4 projection;\n"
    "void main() {\n"
    "    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "    TexCoords = vertex.zw;\n"
    "}\n";

static const char *img_frag_src =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "out vec4 color;\n"
    "uniform sampler2D img_tex;\n"
    "void main() {\n"
    "    color = texture(img_tex, TexCoords);\n"
    "}\n";

/* ---- Orthografische Projektionsmatrix ---- */

static void ortho_matrix(float *m, float l, float r, float b, float t)
{
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] =  1.0f;
}

static const float *get_projection(int fb_w, int fb_h)
{
    if (fb_w != cached_fb_w || fb_h != cached_fb_h) {
        ortho_matrix(cached_proj, 0.0f, (float)fb_w, 0.0f, (float)fb_h);
        cached_fb_w = fb_w;
        cached_fb_h = fb_h;
    }
    return cached_proj;
}

/* ---- UTF-8 Dekodierung ---- */

static uint32_t utf8_next(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    uint32_t cp;

    if (s[0] < 0x80) {
        cp = s[0];
        *pp += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *pp += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
               (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) |
             ((uint32_t)(s[1] & 0x3F) << 6) |
             (s[2] & 0x3F);
        *pp += 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
               (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) |
             ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) |
             (s[3] & 0x3F);
        *pp += 4;
    } else {
        cp = 0xFFFD;
        *pp += 1;
    }
    return cp;
}

/* ---- Glyph Cache ---- */

static uint32_t gc_hash(uint8_t font_id, uint32_t cp, uint16_t sz)
{
    uint32_t h = cp * 2654435761u;
    h ^= (uint32_t)sz * 40503u;
    h ^= (uint32_t)font_id * 65537u;
    return h % GCACHE_SIZE;
}

static void gc_clear(void)
{
    for (int i = 0; i < GCACHE_SIZE; i++) {
        if (gcache[i].occupied && gcache[i].texture)
            glDeleteTextures(1, &gcache[i].texture);
    }
    memset(gcache, 0, sizeof(gcache));
    gcache_count = 0;
    memset(lh_cache, 0, sizeof(lh_cache));
}

/* Versucht einen Glyph in einem bestimmten Font zu laden.
   Gibt 1 bei Erfolg zurueck, 0 wenn der Glyph nicht vorhanden ist. */
static int try_load_glyph(int fid, uint32_t cp, uint16_t sz)
{
    if (fid < 0 || fid >= MAX_FONTS || !fonts[fid].active)
        return 0;
    FT_Face face = fonts[fid].face;
    FT_Set_Pixel_Sizes(face, 0, sz);
    /* Pruefe erst ob der Glyph existiert (nicht .notdef) */
    FT_UInt glyph_idx = FT_Get_Char_Index(face, cp);
    if (glyph_idx == 0) return 0;
    return FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0;
}

static GCEntry *gc_rasterize(uint8_t font_id, uint32_t cp, uint16_t sz)
{
    int resolved_fid = -1;

    /* 1. Versuche den angeforderten Font */
    if (try_load_glyph(font_id, cp, sz)) {
        resolved_fid = font_id;
    }
    /* 2. Versuche den System-Font (Font 0) */
    if (resolved_fid < 0 && font_id != 0 && try_load_glyph(0, cp, sz)) {
        resolved_fid = 0;
    }
    /* 3. Versuche alle Fallback-Fonts */
    if (resolved_fid < 0) {
        for (int i = 0; i < fallback_count; i++) {
            if (try_load_glyph(fallback_ids[i], cp, sz)) {
                resolved_fid = fallback_ids[i];
                break;
            }
        }
    }

    if (resolved_fid < 0) return NULL;

    FT_Face face = fonts[resolved_fid].face;

    /* GL-Textur erstellen */
    GLuint tex = 0;
    if (face->glyph->bitmap.width > 0 && face->glyph->bitmap.rows > 0) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                     (GLsizei)face->glyph->bitmap.width,
                     (GLsizei)face->glyph->bitmap.rows,
                     0, GL_RED, GL_UNSIGNED_BYTE,
                     face->glyph->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    /* Cache voll? Leeren. */
    if (gcache_count >= GCACHE_MAX_LOAD)
        gc_clear();

    /* Freien Slot finden */
    uint32_t h = gc_hash(font_id, cp, sz);
    GCEntry *entry = NULL;

    for (int i = 0; i < GCACHE_PROBE; i++) {
        uint32_t idx = (h + (uint32_t)i) % GCACHE_SIZE;
        if (!gcache[idx].occupied) {
            entry = &gcache[idx];
            gcache_count++;
            break;
        }
    }

    if (!entry) {
        /* Evict am Hash-Position */
        entry = &gcache[h];
        if (entry->texture)
            glDeleteTextures(1, &entry->texture);
    }

    entry->codepoint = cp;
    entry->size_px   = sz;
    entry->font_id   = font_id;
    entry->occupied  = 1;
    entry->texture   = tex;
    entry->width     = (int)face->glyph->bitmap.width;
    entry->height    = (int)face->glyph->bitmap.rows;
    entry->bearing_x = face->glyph->bitmap_left;
    entry->bearing_y = face->glyph->bitmap_top;
    entry->advance   = (int)(face->glyph->advance.x >> 6);

    return entry;
}

static GCEntry *get_glyph(uint8_t font_id, uint32_t cp, uint16_t sz)
{
    uint32_t h = gc_hash(font_id, cp, sz);

    for (int i = 0; i < GCACHE_PROBE; i++) {
        uint32_t idx = (h + (uint32_t)i) % GCACHE_SIZE;
        GCEntry *e = &gcache[idx];
        if (!e->occupied)
            break;
        if (e->codepoint == cp && e->size_px == sz && e->font_id == font_id)
            return e;
    }

    return gc_rasterize(font_id, cp, sz);
}

/* ---- Line Height ---- */

static float get_line_height_fb(uint8_t font_id, uint16_t size_px)
{
    uint32_t idx = ((uint32_t)font_id * 7u + size_px) % LH_CACHE_SIZE;
    LHEntry *e = &lh_cache[idx];
    if (e->valid && e->font_id == font_id && e->size_px == size_px)
        return e->line_height;

    uint8_t fid = font_id;
    if (fid >= MAX_FONTS || !fonts[fid].active) fid = 0;
    if (!fonts[fid].active) return (float)size_px * 1.2f;

    FT_Face face = fonts[fid].face;
    FT_Set_Pixel_Sizes(face, 0, size_px);
    float lh = (float)(face->size->metrics.height >> 6);
    if (lh < 1.0f) lh = (float)size_px * 1.2f;

    e->font_id     = font_id;
    e->size_px     = size_px;
    e->line_height = lh;
    e->valid       = 1;

    return lh;
}

/* ---- Hilfsfunktion: logische Font-Groesse → Pixel-Groesse ---- */

static uint16_t font_size_to_px(float font_size)
{
    int px = (int)(font_size * g_dpi_scale + 0.5f);
    if (px < 6) px = 6;
    if (px > 200) px = 200;
    return (uint16_t)px;
}

/* ---- Public API ---- */

int renderer_init(const char *font_path, int font_size, float dpi_scale)
{
    g_dpi_scale = dpi_scale;
    g_default_size = (float)font_size;

    if (FT_Init_FreeType(&ft_lib)) {
        fprintf(stderr, "Failed to init FreeType\n");
        return -1;
    }

    /* System-Font als Font 0 laden */
    if (FT_New_Face(ft_lib, font_path, 0, &fonts[0].face)) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        FT_Done_FreeType(ft_lib);
        return -1;
    }
    fonts[0].active = 1;
    font_registry_count = 0;
    renderer_register_font(0, "sans-serif");
    renderer_register_font(0, "system-ui");
    if (fonts[0].face->family_name)
        renderer_register_font(0, fonts[0].face->family_name);

    /* Serif- und Monospace-Fonts versuchen zu laden */
    {
        static const struct { const char *path; const char *generic; } extra_fonts[] = {
            {"/usr/share/fonts/noto/NotoSerif-Regular.ttf", "serif"},
            {"/usr/share/fonts/noto/NotoSansMono-Regular.ttf", "monospace"},
            {"/usr/share/fonts/adobe-source-code-pro/SourceCodePro-Regular.otf", "monospace"},
            {"/usr/share/fonts/TTF/DejaVuSerif.ttf", "serif"},
            {"/usr/share/fonts/TTF/DejaVuSansMono.ttf", "monospace"},
            {NULL, NULL}
        };
        int have_serif = 0, have_mono = 0;
        for (int i = 0; extra_fonts[i].path; i++) {
            int is_serif = (strcmp(extra_fonts[i].generic, "serif") == 0);
            if ((is_serif && have_serif) || (!is_serif && have_mono)) continue;
            int fid = -1;
            for (int j = 1; j < MAX_FONTS; j++) {
                if (!fonts[j].active) { fid = j; break; }
            }
            if (fid < 0) break;
            if (FT_New_Face(ft_lib, extra_fonts[i].path, 0, &fonts[fid].face) == 0) {
                fonts[fid].active = 1;
                renderer_register_font((uint8_t)fid, extra_fonts[i].generic);
                if (fonts[fid].face->family_name)
                    renderer_register_font((uint8_t)fid, fonts[fid].face->family_name);
                printf("  System font: %s → %s (id=%d)\n",
                       extra_fonts[i].generic, fonts[fid].face->family_name, fid);
                if (is_serif) have_serif = 1; else have_mono = 1;
            }
        }
    }

    /* Fallback-Fonts laden (CJK, Arabisch, Symbole) */
    {
        static const char *fallback_paths[] = {
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf",
            "/usr/share/fonts/noto/NotoSansHebrew-Regular.ttf",
            "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf",
            "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf",
            "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
            NULL
        };
        fallback_count = 0;
        for (int i = 0; fallback_paths[i] && fallback_count < MAX_FALLBACKS; i++) {
            int fid = -1;
            for (int j = 1; j < MAX_FONTS; j++) {
                if (!fonts[j].active) { fid = j; break; }
            }
            if (fid < 0) break;
            if (FT_New_Face(ft_lib, fallback_paths[i], 0, &fonts[fid].face) == 0) {
                fonts[fid].active = 1;
                fallback_ids[fallback_count++] = fid;
                printf("  Fallback font: %s (id=%d)\n", fallback_paths[i], fid);
            }
        }
    }

    /* Text-Shader */
    {
        GLuint v = compile_shader(GL_VERTEX_SHADER, text_vert_src);
        GLuint f = compile_shader(GL_FRAGMENT_SHADER, text_frag_src);
        text_shader = link_program(v, f);
    }

    glGenVertexArrays(1, &text_vao);
    glGenBuffers(1, &text_vbo);
    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* Rect-Shader */
    {
        GLuint v = compile_shader(GL_VERTEX_SHADER, rect_vert_src);
        GLuint f = compile_shader(GL_FRAGMENT_SHADER, rect_frag_src);
        rect_shader = link_program(v, f);
    }

    glGenVertexArrays(1, &rect_vao);
    glGenBuffers(1, &rect_vbo);
    glBindVertexArray(rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* Image-Shader */
    {
        GLuint v = compile_shader(GL_VERTEX_SHADER, img_vert_src);
        GLuint f = compile_shader(GL_FRAGMENT_SHADER, img_frag_src);
        img_shader = link_program(v, f);
    }

    glGenVertexArrays(1, &img_vao);
    glGenBuffers(1, &img_vbo);
    glBindVertexArray(img_vao);
    glBindBuffer(GL_ARRAY_BUFFER, img_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* Uniform-Locations cachen */
    text_proj_loc  = glGetUniformLocation(text_shader, "projection");
    text_color_loc = glGetUniformLocation(text_shader, "textColor");
    rect_proj_loc  = glGetUniformLocation(rect_shader, "projection");
    rect_color_loc = glGetUniformLocation(rect_shader, "rectColor");
    img_proj_loc   = glGetUniformLocation(img_shader,  "projection");

    return 0;
}

void renderer_draw_text_sized(const char *text, float x, float y,
                              float font_size, uint8_t font_id,
                              float r, float g, float b,
                              int fb_width, int fb_height)
{
    if (!text || !*text) return;

    uint16_t size_px = font_size_to_px(font_size);
    float lh_fb = get_line_height_fb(font_id, size_px);

    glUseProgram(text_shader);
    glUniformMatrix4fv(text_proj_loc, 1, GL_FALSE,
                       get_projection(fb_width, fb_height));
    glUniform3f(text_color_loc, r, g, b);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(text_vao);

    /* Logische → Framebuffer-Koordinaten */
    float sx = x * g_dpi_scale;
    float sy = y * g_dpi_scale;

    /* y von oben-links nach OpenGL unten-links */
    float base_y = (float)fb_height - sy - lh_fb * 0.75f;

    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;
        if (cp == '\n' || cp == '\r') continue;

        GCEntry *gl = get_glyph(font_id, cp, size_px);
        if (!gl) continue;

        if (gl->texture) {
            float xpos = sx + (float)gl->bearing_x;
            float ypos = base_y - (float)(gl->height - gl->bearing_y);
            float w = (float)gl->width;
            float h = (float)gl->height;

            float vertices[6][4] = {
                { xpos,     ypos + h, 0.0f, 0.0f },
                { xpos,     ypos,     0.0f, 1.0f },
                { xpos + w, ypos,     1.0f, 1.0f },

                { xpos,     ypos + h, 0.0f, 0.0f },
                { xpos + w, ypos,     1.0f, 1.0f },
                { xpos + w, ypos + h, 1.0f, 0.0f },
            };

            glBindTexture(GL_TEXTURE_2D, gl->texture);
            glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        sx += (float)gl->advance;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderer_draw_text(const char *text, float x, float y,
                        float r, float g, float b,
                        int fb_width, int fb_height)
{
    renderer_draw_text_sized(text, x, y, g_default_size, 0, r, g, b,
                             fb_width, fb_height);
}

float renderer_text_width_sized(const char *text, float font_size, uint8_t font_id)
{
    if (!text || !*text) return 0;

    uint16_t size_px = font_size_to_px(font_size);
    float w = 0;

    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;

        GCEntry *gl = get_glyph(font_id, cp, size_px);
        if (gl) w += (float)gl->advance;
    }

    return w / g_dpi_scale;
}

float renderer_text_width(const char *text)
{
    return renderer_text_width_sized(text, g_default_size, 0);
}

float renderer_line_height_sized(float font_size)
{
    uint16_t size_px = font_size_to_px(font_size);
    return get_line_height_fb(0, size_px) / g_dpi_scale;
}

float renderer_line_height(void)
{
    return renderer_line_height_sized(g_default_size);
}

void renderer_draw_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a,
                        int fb_width, int fb_height)
{
    glUseProgram(rect_shader);
    glUniformMatrix4fv(rect_proj_loc, 1, GL_FALSE,
                       get_projection(fb_width, fb_height));
    glUniform4f(rect_color_loc, r, g, b, a);

    float sx = x * g_dpi_scale;
    float sy = y * g_dpi_scale;
    float sw = w * g_dpi_scale;
    float sh = h * g_dpi_scale;
    float oy = (float)fb_height - sy - sh;

    float vertices[6][2] = {
        { sx,      oy + sh },
        { sx,      oy      },
        { sx + sw, oy      },

        { sx,      oy + sh },
        { sx + sw, oy      },
        { sx + sw, oy + sh },
    };

    glBindVertexArray(rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rect_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void renderer_draw_image(GLuint texture, float x, float y, float w, float h,
                         int fb_width, int fb_height)
{
    glUseProgram(img_shader);
    glUniformMatrix4fv(img_proj_loc, 1, GL_FALSE,
                       get_projection(fb_width, fb_height));

    float sx = x * g_dpi_scale;
    float sy = y * g_dpi_scale;
    float sw = w * g_dpi_scale;
    float sh = h * g_dpi_scale;
    float oy = (float)fb_height - sy - sh;

    float vertices[6][4] = {
        { sx,      oy + sh, 0.0f, 0.0f },
        { sx,      oy,      0.0f, 1.0f },
        { sx + sw, oy,      1.0f, 1.0f },

        { sx,      oy + sh, 0.0f, 0.0f },
        { sx + sw, oy,      1.0f, 1.0f },
        { sx + sw, oy + sh, 1.0f, 0.0f },
    };

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(img_vao);
    glBindBuffer(GL_ARRAY_BUFFER, img_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderer_register_font(uint8_t font_id, const char *family_name)
{
    if (!family_name || font_registry_count >= MAX_FONT_NAMES) return;

    /* Duplikat-Check (case-insensitive) */
    for (int i = 0; i < font_registry_count; i++) {
        if (strcasecmp(font_registry[i].name, family_name) == 0) {
            font_registry[i].font_id = font_id;
            return;
        }
    }

    size_t len = strlen(family_name);
    if (len >= sizeof(font_registry[0].name)) len = sizeof(font_registry[0].name) - 1;
    memcpy(font_registry[font_registry_count].name, family_name, len);
    font_registry[font_registry_count].name[len] = '\0';
    font_registry[font_registry_count].font_id = font_id;
    font_registry_count++;
}

uint8_t renderer_resolve_font_family(const char *family_list)
{
    if (!family_list) return 0;

    const char *p = family_list;
    while (*p) {
        /* Whitespace/Komma ueberspringen */
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;

        /* Font-Namen extrahieren */
        char name[64];
        int len = 0;

        if (*p == '"' || *p == '\'') {
            /* Quoted */
            char q = *p++;
            while (*p && *p != q && len < 63) name[len++] = *p++;
            if (*p == q) p++;
        } else {
            /* Unquoted */
            while (*p && *p != ',' && len < 63) name[len++] = *p++;
            /* Trailing whitespace trimmen */
            while (len > 0 && (name[len-1] == ' ' || name[len-1] == '\t')) len--;
        }
        name[len] = '\0';
        if (len == 0) continue;

        /* In Registry suchen */
        for (int i = 0; i < font_registry_count; i++) {
            if (strcasecmp(font_registry[i].name, name) == 0) {
                return font_registry[i].font_id;
            }
        }
    }

    return 0; /* Default */
}

int renderer_load_font_mem(const void *data, size_t len)
{
    int id = -1;
    for (int i = 1; i < MAX_FONTS; i++) {
        if (!fonts[i].active) { id = i; break; }
    }
    if (id < 0) {
        fprintf(stderr, "renderer: max fonts (%d) reached\n", MAX_FONTS);
        return 0;
    }

    /* Daten kopieren (FreeType braucht sie solange der Face lebt) */
    uint8_t *mem = malloc(len);
    if (!mem) return 0;
    memcpy(mem, data, len);

    if (FT_New_Memory_Face(ft_lib, mem, (FT_Long)len, 0, &fonts[id].face)) {
        free(mem);
        return 0;
    }

    fonts[id].mem    = mem;
    fonts[id].active = 1;
    if (fonts[id].face->family_name) {
        renderer_register_font((uint8_t)id, fonts[id].face->family_name);
        printf("  Font loaded: %s (id=%d)\n", fonts[id].face->family_name, id);
    }
    return id;
}

void renderer_cache_clear(void)
{
    gc_clear();
}

void renderer_cleanup(void)
{
    gc_clear();

    for (int i = 0; i < MAX_FONTS; i++) {
        if (fonts[i].active) {
            FT_Done_Face(fonts[i].face);
            free(fonts[i].mem);
            memset(&fonts[i], 0, sizeof(fonts[i]));
        }
    }

    FT_Done_FreeType(ft_lib);

    glDeleteVertexArrays(1, &text_vao);
    glDeleteBuffers(1, &text_vbo);
    glDeleteVertexArrays(1, &rect_vao);
    glDeleteBuffers(1, &rect_vbo);
    glDeleteVertexArrays(1, &img_vao);
    glDeleteBuffers(1, &img_vbo);
    glDeleteProgram(text_shader);
    glDeleteProgram(rect_shader);
    glDeleteProgram(img_shader);
}
