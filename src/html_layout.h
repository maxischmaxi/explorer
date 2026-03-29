#ifndef HTML_LAYOUT_H
#define HTML_LAYOUT_H

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <stdint.h>
#include <lexbor/html/html.h>

typedef struct {
    float   x, y, w, h;
    char   *text;
    float   font_size;   /* tatsaechliche Font-Groesse in logischen Pixeln */
    uint8_t font_id;     /* Font-ID fuer Rendering */
    float  color_r, color_g, color_b;
    float  bg_r, bg_g, bg_b, bg_a;
    int    is_hr;
    int    is_link;
    char  *href;
    int    is_img_placeholder;
    GLuint image_texture;
    int    text_decoration;  /* 0=none, 1=underline, 2=line-through */
    int    text_align;       /* 0=left, 1=right, 2=center */
    float  border_width;
    float  border_r, border_g, border_b;
} LayoutBox;

typedef struct {
    LayoutBox *boxes;
    int        count;
    int        capacity;
    float      total_height;
    float      body_bg_r, body_bg_g, body_bg_b, body_bg_a;
    int        has_body_bg;
} Layout;

void html_layout_build(lxb_html_document_t *doc, float avail_w, Layout *out);
void html_layout_free(Layout *layout);

#endif
