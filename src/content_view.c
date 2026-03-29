#define _POSIX_C_SOURCE 200809L
#include "content_view.h"
#include "html_layout.h"
#include "renderer.h"
#include "scrollbar.h"

#include <stdlib.h>
#include <string.h>

#include <lexbor/html/html.h>

#define CONTENT_PADDING 12.0f

static lxb_html_document_t *doc;
static Layout  layout;
static float   scroll_offset;
static float   max_width = 800.0f;
static float   last_viewport_h = 600.0f;
static int     layout_dirty;
static const char *hover_href;  /* gehoverter Link (Pointer-Vergleich genuegt nicht, strcmp noetig) */
static float   last_y_offset;
static ScrollbarState scrollbar_state;

#define CONTENT_PADDING_VAL 12.0f

void content_view_set_html(const char *html, size_t len)
{
    html_layout_free(&layout);
    if (doc) {
        lxb_html_document_destroy(doc);
        doc = NULL;
    }
    scroll_offset = 0;

    if (!html || len == 0) return;

    doc = lxb_html_document_create();
    if (!doc) return;

    lxb_status_t status = lxb_html_document_parse(
        doc, (const lxb_char_t *)html, len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        doc = NULL;
        return;
    }

    layout_dirty = 1;
}

void content_view_set_max_width(float w)
{
    if (w < 50.0f) w = 50.0f;
    max_width = w;
    layout_dirty = 1;
}

void content_view_draw(float y_offset, float content_w, int fb_w, int fb_h)
{
    if (!doc) return;

    if (layout_dirty) {
        html_layout_free(&layout);
        float avail = max_width - 2.0f * CONTENT_PADDING;
        if (avail < 50.0f) avail = 50.0f;
        html_layout_build(doc, avail, &layout);
        layout_dirty = 0;
    }

    if (layout.count == 0) return;

    float lh = renderer_line_height();
    float viewport_h = (float)fb_h - y_offset;
    last_viewport_h = viewport_h;
    last_y_offset = y_offset;

    for (int i = 0; i < layout.count; i++) {
        LayoutBox *b = &layout.boxes[i];
        float draw_y = b->y - scroll_offset + y_offset;

        if (draw_y + b->h < y_offset) continue;
        if (draw_y > y_offset + viewport_h) break;

        float draw_x = b->x + CONTENT_PADDING;

        if (b->bg_a > 0) {
            renderer_draw_rect(draw_x, draw_y, b->w, b->h,
                               b->bg_r, b->bg_g, b->bg_b, b->bg_a,
                               fb_w, fb_h);
        }

        if (b->image_texture) {
            renderer_draw_image(b->image_texture, draw_x, draw_y, b->w, b->h,
                                fb_w, fb_h);
        }

        if (b->is_hr) {
            renderer_draw_rect(draw_x, draw_y, b->w, 1.0f,
                               b->bg_r, b->bg_g, b->bg_b, b->bg_a,
                               fb_w, fb_h);
        }

        /* Border */
        if (b->border_width > 0) {
            float bw = b->border_width;
            /* Top */
            renderer_draw_rect(draw_x, draw_y, b->w, bw,
                               b->border_r, b->border_g, b->border_b, 1.0f, fb_w, fb_h);
            /* Bottom */
            renderer_draw_rect(draw_x, draw_y + b->h - bw, b->w, bw,
                               b->border_r, b->border_g, b->border_b, 1.0f, fb_w, fb_h);
            /* Left */
            renderer_draw_rect(draw_x, draw_y, bw, b->h,
                               b->border_r, b->border_g, b->border_b, 1.0f, fb_w, fb_h);
            /* Right */
            renderer_draw_rect(draw_x + b->w - bw, draw_y, bw, b->h,
                               b->border_r, b->border_g, b->border_b, 1.0f, fb_w, fb_h);
        }

        if (b->text) {
            float tr = b->color_r, tg = b->color_g, tb = b->color_b;
            int is_hov = (b->href && hover_href &&
                          strcmp(b->href, hover_href) == 0);

            if (is_hov) { tr = 0.55f; tg = 0.75f; tb = 1.0f; }

            if (b->font_scale != 1.0f)
                renderer_draw_text_scaled(b->text, draw_x, draw_y, b->font_scale,
                                          tr, tg, tb, fb_w, fb_h);
            else
                renderer_draw_text(b->text, draw_x, draw_y, tr, tg, tb, fb_w, fb_h);

            /* Text-Decoration: underline */
            float tw = renderer_text_width(b->text) * b->font_scale;
            if (b->text_decoration == 1 || (b->href && is_hov)) {
                renderer_draw_rect(draw_x, draw_y + lh - 2.0f, tw, 1.0f,
                                   tr, tg, tb, 0.9f, fb_w, fb_h);
            }
            /* Text-Decoration: line-through */
            if (b->text_decoration == 2) {
                renderer_draw_rect(draw_x, draw_y + lh * 0.45f, tw, 1.0f,
                                   tr, tg, tb, 0.7f, fb_w, fb_h);
            }
        }
    }

    /* Scrollbar */
    scrollbar_state.scroll_offset   = scroll_offset;
    scrollbar_state.content_height  = layout.total_height;
    scrollbar_state.viewport_height = viewport_h;
    scrollbar_state.x               = content_w;
    scrollbar_state.y               = y_offset;
    scrollbar_draw(&scrollbar_state, fb_w, fb_h);
}

static void clamp_scroll(void)
{
    float max_scroll = layout.total_height - last_viewport_h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset > max_scroll)
        scroll_offset = max_scroll;
    if (scroll_offset < 0)
        scroll_offset = 0;
}

void content_view_scroll(float delta)
{
    float lh = renderer_line_height();
    scroll_offset += delta * lh * 3.0f;
    clamp_scroll();
    scrollbar_touch(&scrollbar_state, 0);
    {
        extern double glfwGetTime(void);
        scrollbar_touch(&scrollbar_state, glfwGetTime());
    }
}

void content_view_scroll_page(float viewport_h, int dir)
{
    scroll_offset += (float)dir * viewport_h * 0.9f;
    clamp_scroll();
    {
        extern double glfwGetTime(void);
        scrollbar_touch(&scrollbar_state, glfwGetTime());
    }
}

void content_view_scroll_to(int pos)
{
    if (pos == 0)
        scroll_offset = 0;
    else
        scroll_offset = layout.total_height;
    clamp_scroll();
    {
        extern double glfwGetTime(void);
        scrollbar_touch(&scrollbar_state, glfwGetTime());
    }
}

static int find_box_at(float x, float y)
{
    float lx = x - CONTENT_PADDING_VAL;
    float ly = y - last_y_offset + scroll_offset;

    for (int i = 0; i < layout.count; i++) {
        LayoutBox *b = &layout.boxes[i];
        if (!b->text) continue;
        float tw = renderer_text_width(b->text);
        if (lx >= b->x && lx <= b->x + tw &&
            ly >= b->y && ly <= b->y + b->h) {
            return i;
        }
    }
    return -1;
}

const char *content_view_link_at(float x, float y)
{
    int idx = find_box_at(x, y);
    if (idx < 0) return NULL;
    return layout.boxes[idx].href;
}

void content_view_set_hover(float x, float y)
{
    int idx = find_box_at(x, y);
    if (idx >= 0 && layout.boxes[idx].href)
        hover_href = layout.boxes[idx].href;
    else
        hover_href = NULL;
}

lxb_html_document_t *content_view_get_doc(void)
{
    return doc;
}

void content_view_mark_dirty(void)
{
    layout_dirty = 1;
}

ScrollbarState *content_view_get_scrollbar(void)
{
    return &scrollbar_state;
}

void content_view_set_scroll(float offset)
{
    scroll_offset = offset;
    clamp_scroll();
}

void content_view_free(void)
{
    html_layout_free(&layout);
    if (doc) {
        lxb_html_document_destroy(doc);
        doc = NULL;
    }
}
