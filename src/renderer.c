#define _POSIX_C_SOURCE 200809L
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* Glyph-Cache fuer die ersten 128 ASCII-Zeichen */
typedef struct {
    GLuint texture;
    int    width;
    int    height;
    int    bearing_x;
    int    bearing_y;
    int    advance;
} Glyph;

static Glyph    glyphs[128];
static GLuint   text_shader;
static GLuint   text_vao, text_vbo;
static GLuint   rect_shader;
static GLuint   rect_vao, rect_vbo;
static GLuint   img_shader;
static GLuint   img_vao, img_vbo;
static float    line_h;
static float    scale;

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

/* ---- Text-Shader ---- */

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

/* ---- Rect-Shader ---- */

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

/* ---- Image-Shader ---- */

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

/* ---- Public API ---- */

int renderer_init(const char *font_path, int font_size, float dpi_scale)
{
    scale = dpi_scale;

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Failed to init FreeType\n");
        return -1;
    }

    FT_Face face;
    if (FT_New_Face(ft, font_path, 0, &face)) {
        fprintf(stderr, "Failed to load font: %s\n", font_path);
        FT_Done_FreeType(ft);
        return -1;
    }

    /* Font in Framebuffer-Pixeln rastern fuer scharfe Darstellung */
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)(font_size * scale));

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

    /* Glyph-Texturen laden */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
            continue;

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                     (GLsizei)face->glyph->bitmap.width,
                     (GLsizei)face->glyph->bitmap.rows,
                     0, GL_RED, GL_UNSIGNED_BYTE,
                     face->glyph->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glyphs[c].texture   = tex;
        glyphs[c].width     = (int)face->glyph->bitmap.width;
        glyphs[c].height    = (int)face->glyph->bitmap.rows;
        glyphs[c].bearing_x = face->glyph->bitmap_left;
        glyphs[c].bearing_y = face->glyph->bitmap_top;
        glyphs[c].advance   = (int)(face->glyph->advance.x >> 6);
    }

    /* Zeilenhoehe in logischen Pixeln */
    line_h = (float)(face->size->metrics.height >> 6) / scale;

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return 0;
}

void renderer_draw_text(const char *text, float x, float y,
                        float r, float g, float b,
                        int fb_width, int fb_height)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(text_shader);

    /* Projektion in Framebuffer-Pixeln */
    float proj[16];
    ortho_matrix(proj, 0.0f, (float)fb_width, 0.0f, (float)fb_height);
    glUniformMatrix4fv(glGetUniformLocation(text_shader, "projection"),
                       1, GL_FALSE, proj);
    glUniform3f(glGetUniformLocation(text_shader, "textColor"), r, g, b);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(text_vao);

    /* Logische Koordinaten -> Framebuffer-Koordinaten */
    float sx = x * scale;
    float sy = y * scale;
    float scaled_lh = line_h * scale;

    /* y von oben-links nach OpenGL unten-links umrechnen */
    float base_y = (float)fb_height - sy - scaled_lh * 0.75f;

    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 128) continue;

        Glyph *gl = &glyphs[c];

        /* Glyph-Metriken sind bereits in Framebuffer-Pixeln */
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

        sx += (float)gl->advance;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

void renderer_draw_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a,
                        int fb_width, int fb_height)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(rect_shader);

    float proj[16];
    ortho_matrix(proj, 0.0f, (float)fb_width, 0.0f, (float)fb_height);
    glUniformMatrix4fv(glGetUniformLocation(rect_shader, "projection"),
                       1, GL_FALSE, proj);
    glUniform4f(glGetUniformLocation(rect_shader, "rectColor"), r, g, b, a);

    /* Logische Koordinaten -> Framebuffer-Koordinaten */
    float sx = x * scale;
    float sy = y * scale;
    float sw = w * scale;
    float sh = h * scale;

    /* y von oben-links nach OpenGL unten-links */
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

    glDisable(GL_BLEND);
}

void renderer_draw_image(GLuint texture, float x, float y, float w, float h,
                         int fb_width, int fb_height)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(img_shader);

    float proj[16];
    ortho_matrix(proj, 0.0f, (float)fb_width, 0.0f, (float)fb_height);
    glUniformMatrix4fv(glGetUniformLocation(img_shader, "projection"),
                       1, GL_FALSE, proj);

    float sx = x * scale;
    float sy = y * scale;
    float sw = w * scale;
    float sh = h * scale;
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

    glDisable(GL_BLEND);
}

float renderer_text_width(const char *text)
{
    float w = 0;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 128)
            w += (float)glyphs[c].advance / scale;
    }
    return w;
}

float renderer_text_width_scaled(const char *text, float text_scale)
{
    return renderer_text_width(text) * text_scale;
}

void renderer_draw_text_scaled(const char *text, float x, float y, float text_scale,
                               float r, float g, float b,
                               int fb_width, int fb_height)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(text_shader);

    float proj[16];
    ortho_matrix(proj, 0.0f, (float)fb_width, 0.0f, (float)fb_height);
    glUniformMatrix4fv(glGetUniformLocation(text_shader, "projection"),
                       1, GL_FALSE, proj);
    glUniform3f(glGetUniformLocation(text_shader, "textColor"), r, g, b);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(text_vao);

    float sx = x * scale;
    float sy = y * scale;
    float s = text_scale;
    float scaled_lh = line_h * scale;
    float base_y = (float)fb_height - sy - scaled_lh * s * 0.75f;

    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 128) continue;

        Glyph *gl = &glyphs[c];

        float xpos = sx + (float)gl->bearing_x * s;
        float ypos = base_y - (float)(gl->height - gl->bearing_y) * s;
        float w = (float)gl->width * s;
        float h = (float)gl->height * s;

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

        sx += (float)gl->advance * s;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

float renderer_line_height(void)
{
    return line_h;
}

void renderer_cleanup(void)
{
    for (int i = 0; i < 128; i++) {
        if (glyphs[i].texture)
            glDeleteTextures(1, &glyphs[i].texture);
    }
    glDeleteVertexArrays(1, &text_vao);
    glDeleteBuffers(1, &text_vbo);
    glDeleteVertexArrays(1, &rect_vao);
    glDeleteBuffers(1, &rect_vbo);
    glDeleteProgram(text_shader);
    glDeleteProgram(rect_shader);
    glDeleteVertexArrays(1, &img_vao);
    glDeleteBuffers(1, &img_vbo);
    glDeleteProgram(img_shader);
}
