#define _POSIX_C_SOURCE 200809L
#include "html_layout.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "image_cache.h"
#include "css_engine.h"

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/tag/const.h>

/* ---- Display-Modi ---- */

typedef enum {
    DISP_BLOCK,
    DISP_INLINE,
    DISP_LIST_ITEM,
    DISP_FLEX,
    DISP_NONE
} DispMode;

/* ---- Default-Styles ---- */

typedef struct {
    float font_scale;
    float margin_top;    /* in Vielfachen von line_height */
    float margin_bottom;
    float padding_left;  /* in Pixeln */
    float r, g, b;
} TagStyle;

static DispMode display_for_tag(lxb_tag_id_t tag)
{
    switch (tag) {
    case LXB_TAG_HEAD: case LXB_TAG_TITLE: case LXB_TAG_META:
    case LXB_TAG_LINK: case LXB_TAG_STYLE: case LXB_TAG_SCRIPT:
    case LXB_TAG_NOSCRIPT:
        return DISP_NONE;

    case LXB_TAG_BODY: case LXB_TAG_DIV: case LXB_TAG_P:
    case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3:
    case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6:
    case LXB_TAG_HEADER: case LXB_TAG_FOOTER: case LXB_TAG_SECTION:
    case LXB_TAG_ARTICLE: case LXB_TAG_ASIDE: case LXB_TAG_NAV:
    case LXB_TAG_MAIN: case LXB_TAG_UL: case LXB_TAG_OL:
    case LXB_TAG_BLOCKQUOTE: case LXB_TAG_PRE: case LXB_TAG_FORM:
    case LXB_TAG_FIGURE: case LXB_TAG_DL: case LXB_TAG_DT:
    case LXB_TAG_DD: case LXB_TAG_HR: case LXB_TAG_ADDRESS:
    case LXB_TAG_TABLE: case LXB_TAG_TR:
        return DISP_BLOCK;

    case LXB_TAG_LI:
        return DISP_LIST_ITEM;

    default:
        return DISP_INLINE;
    }
}

static TagStyle style_for_tag(lxb_tag_id_t tag)
{
    TagStyle s = {1.0f, 0, 0, 0, 0.80f, 0.82f, 0.84f};

    switch (tag) {
    case LXB_TAG_H1:
        s.font_scale = 2.0f; s.margin_top = 0.67f; s.margin_bottom = 0.67f;
        s.r = 0.95f; s.g = 0.95f; s.b = 0.97f;
        break;
    case LXB_TAG_H2:
        s.font_scale = 1.5f; s.margin_top = 0.83f; s.margin_bottom = 0.83f;
        s.r = 0.92f; s.g = 0.92f; s.b = 0.95f;
        break;
    case LXB_TAG_H3:
        s.font_scale = 1.17f; s.margin_top = 1.0f; s.margin_bottom = 1.0f;
        s.r = 0.90f; s.g = 0.90f; s.b = 0.93f;
        break;
    case LXB_TAG_H4:
        s.font_scale = 1.0f; s.margin_top = 1.33f; s.margin_bottom = 1.33f;
        s.r = 0.88f; s.g = 0.88f; s.b = 0.90f;
        break;
    case LXB_TAG_H5:
        s.font_scale = 0.83f; s.margin_top = 1.67f; s.margin_bottom = 1.67f;
        s.r = 0.85f; s.g = 0.85f; s.b = 0.88f;
        break;
    case LXB_TAG_H6:
        s.font_scale = 0.67f; s.margin_top = 2.33f; s.margin_bottom = 2.33f;
        s.r = 0.82f; s.g = 0.82f; s.b = 0.85f;
        break;
    case LXB_TAG_P:
        s.margin_top = 1.0f; s.margin_bottom = 1.0f;
        break;
    case LXB_TAG_BLOCKQUOTE:
        s.margin_top = 1.0f; s.margin_bottom = 1.0f; s.padding_left = 40.0f;
        s.r = 0.70f; s.g = 0.72f; s.b = 0.74f;
        break;
    case LXB_TAG_PRE:
        s.margin_top = 1.0f; s.margin_bottom = 1.0f;
        s.r = 0.70f; s.g = 0.80f; s.b = 0.70f;
        break;
    case LXB_TAG_LI:
        s.padding_left = 20.0f;
        break;
    case LXB_TAG_DD:
        s.padding_left = 40.0f;
        break;
    case LXB_TAG_A:
        s.r = 0.40f; s.g = 0.60f; s.b = 1.0f;
        break;
    case LXB_TAG_CODE:
        s.r = 0.55f; s.g = 0.80f; s.b = 0.55f;
        break;
    default:
        break;
    }
    return s;
}

/* ---- Layout-Kontext ---- */

#define MAX_BOXES 100000

typedef struct {
    Layout *layout;
    float   avail_w;
    float   cursor_x;
    float   cursor_y;
    float   block_start_x;
    float   line_h;
    int     at_line_start;
    int     text_decoration;  /* vererbt: 0=none, 1=underline, 2=line-through */
    int     text_align;       /* vererbt: 0=left, 1=right, 2=center */
    float   font_scale;       /* vererbt: 1.0 = 16px */
} LayoutCtx;

/* Attribut-Wert als int parsen (-1 wenn nicht vorhanden oder ungueltig). */
static int get_int_attr(lxb_dom_element_t *el, const char *name)
{
    size_t val_len = 0;
    const lxb_char_t *val = lxb_dom_element_get_attribute(
        el, (const lxb_char_t *)name, strlen(name), &val_len);
    if (!val || val_len == 0) return -1;
    return atoi((const char *)val);
}

static char *get_str_attr(lxb_dom_element_t *el, const char *name)
{
    size_t val_len = 0;
    const lxb_char_t *val = lxb_dom_element_get_attribute(
        el, (const lxb_char_t *)name, strlen(name), &val_len);
    if (!val || val_len == 0) return NULL;
    char *s = malloc(val_len + 1);
    if (!s) return NULL;
    memcpy(s, val, val_len);
    s[val_len] = '\0';
    return s;
}

static void emit_box(LayoutCtx *ctx, float x, float y, float w, float h,
                     const char *text, float font_scale,
                     float cr, float cg, float cb,
                     float bgr, float bgg, float bgb, float bga,
                     int is_hr, int is_link, int is_img)
{
    Layout *L = ctx->layout;
    if (L->count >= MAX_BOXES) return;

    if (L->count >= L->capacity) {
        int new_cap = L->capacity ? L->capacity * 2 : 256;
        LayoutBox *tmp = realloc(L->boxes, sizeof(LayoutBox) * (size_t)new_cap);
        if (!tmp) return;
        L->boxes = tmp;
        L->capacity = new_cap;
    }

    LayoutBox *b = &L->boxes[L->count++];
    b->x = x; b->y = y; b->w = w; b->h = h;
    b->text = text ? strdup(text) : NULL;
    b->font_scale = font_scale;
    b->color_r = cr; b->color_g = cg; b->color_b = cb;
    b->bg_r = bgr; b->bg_g = bgg; b->bg_b = bgb; b->bg_a = bga;
    b->is_hr = is_hr;
    b->is_link = is_link;
    b->href = NULL;
    b->is_img_placeholder = is_img;
    b->image_texture = 0;
    b->text_decoration = 0;
    b->text_align = 0;
    b->border_width = 0;

    float bottom = y + h;
    if (bottom > L->total_height)
        L->total_height = bottom;
}

/* ---- Whitespace-Collapsing ---- */

static char *collapse_whitespace(const char *text, size_t len)
{
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t j = 0;
    int in_space = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == ' ') {
            if (!in_space) {
                out[j++] = ' ';
                in_space = 1;
            }
        } else {
            out[j++] = (char)c;
            in_space = 0;
        }
    }
    out[j] = '\0';
    return out;
}

/* ---- Text-Layout (Word-Wrap) ---- */

static void layout_text(LayoutCtx *ctx, const char *text,
                        float font_scale, float r, float g, float b,
                        int is_link, const char *href, float right_edge)
{
    float lh = ctx->line_h * font_scale;
    if (lh < ctx->line_h) lh = ctx->line_h;

    const char *p = text;
    while (*p) {
        /* Führende Spaces am Zeilenanfang überspringen */
        if (ctx->at_line_start) {
            while (*p == ' ') p++;
            if (!*p) break;
        }

        /* Nächstes Wort finden */
        const char *word_start = p;
        while (*p && *p != ' ') p++;
        size_t word_len = (size_t)(p - word_start);
        if (word_len == 0) { if (*p) p++; continue; }

        char word[512];
        if (word_len >= sizeof(word)) word_len = sizeof(word) - 1;
        memcpy(word, word_start, word_len);
        word[word_len] = '\0';

        float ww = renderer_text_width(word) * font_scale;

        /* Zeilenumbruch nötig? */
        if (!ctx->at_line_start && ctx->cursor_x + ww > right_edge) {
            ctx->cursor_y += lh;
            ctx->cursor_x = ctx->block_start_x;
            ctx->at_line_start = 1;
            /* Nochmal Space-Skip am Zeilenanfang */
            continue;
        }

        /* Wort emittieren */
        int idx = ctx->layout->count;
        emit_box(ctx, ctx->cursor_x, ctx->cursor_y, ww, lh,
                 word, font_scale, r, g, b, 0, 0, 0, 0,
                 0, is_link, 0);
        if (idx < ctx->layout->count) {
            if (href) ctx->layout->boxes[idx].href = strdup(href);
            ctx->layout->boxes[idx].text_decoration = ctx->text_decoration;
        }

        ctx->cursor_x += ww;
        ctx->at_line_start = 0;

        /* Space nach dem Wort */
        if (*p == ' ') {
            float sw = renderer_text_width(" ") * font_scale;
            ctx->cursor_x += sw;
            p++;
        }
    }
}

/* ---- Pre-Text-Layout (erhält Whitespace) ---- */

static void layout_pre_text(LayoutCtx *ctx, const char *text, size_t len,
                            float r, float g, float b, float right_edge)
{
    float lh = ctx->line_h;
    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        if (line_len > 0) {
            char line[1024];
            if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
            memcpy(line, p, line_len);
            line[line_len] = '\0';

            float ww = renderer_text_width(line);
            emit_box(ctx, ctx->cursor_x, ctx->cursor_y, ww, lh,
                     line, 1.0f, r, g, b, 0, 0, 0, 0, 0, 0, 0);
        }

        ctx->cursor_y += lh;
        ctx->cursor_x = ctx->block_start_x;
        ctx->at_line_start = 1;

        p += line_len;
        if (nl) p++; /* Skip \n */
    }

    (void)right_edge;
}

/* Forward declaration */
static void layout_node(LayoutCtx *ctx, lxb_dom_node_t *node,
                        float r, float g, float b,
                        int is_link, const char *href,
                        int in_pre, float right_edge);

/* ---- Flex Layout ---- */

#define FLEX_MAX_ITEMS 256

typedef struct {
    lxb_dom_node_t *node;
    float basis;       /* intrinsische / flex-basis Groesse */
    float grow;
    float shrink;
    float final_size;  /* nach Flex-Verteilung */
    float cross_size;
} FlexItem;

/* Misst die intrinsische Groesse eines Knotens durch temporaeren Layout. */
static float measure_intrinsic(LayoutCtx *parent, lxb_dom_node_t *node,
                               float max_w, float r, float g, float b,
                               int is_link, const char *href, int in_pre,
                               float *out_cross)
{
    Layout tmp = {0};
    LayoutCtx mctx = *parent;
    mctx.layout = &tmp;
    mctx.cursor_x = 0;
    mctx.cursor_y = 0;
    mctx.block_start_x = 0;
    mctx.at_line_start = 1;

    /* Forward-Declaration nutzen */
    /* forward-declared as static above */

    layout_node(&mctx, node, r, g, b, is_link, href, in_pre, max_w);

    float main_size = mctx.cursor_x;  /* Fuer row: Breite */
    float cross = tmp.total_height;
    if (cross < mctx.line_h) cross = mctx.line_h;

    if (out_cross) *out_cross = cross;

    /* Temporary layout freigeben */
    for (int i = 0; i < tmp.count; i++) {
        free(tmp.boxes[i].text);
        free(tmp.boxes[i].href);
    }
    free(tmp.boxes);

    return main_size > 0 ? main_size : max_w;
}

static void layout_flex(LayoutCtx *ctx, lxb_dom_node_t *container,
                        TagStyle *style, ComputedStyle *css,
                        float r, float g, float b,
                        int is_link, const char *href,
                        int in_pre, float right_edge)
{
    float lh = ctx->line_h;
    int is_column = (css->flex_direction == 2 || css->flex_direction == 3);
    float avail_main = is_column ? 100000.0f : (right_edge - ctx->block_start_x);

    /* Pending Inline abschliessen */
    if (!ctx->at_line_start) {
        ctx->cursor_y += lh;
        ctx->cursor_x = ctx->block_start_x;
        ctx->at_line_start = 1;
    }

    ctx->cursor_y += style->margin_top * lh;

    float container_x = ctx->block_start_x + style->padding_left;
    float container_y = ctx->cursor_y;
    float pad_main = is_column ? 0 : style->padding_left;
    avail_main -= pad_main;
    float gap = css->gap;

    /* Hintergrund */
    if (css->has_bg) {
        emit_box(ctx, ctx->block_start_x, container_y,
                 right_edge - ctx->block_start_x, lh,
                 NULL, 1.0f, 0, 0, 0,
                 css->bg_r, css->bg_g, css->bg_b, css->bg_a,
                 0, 0, 0);
    }

    /* Phase A: Kinder sammeln */
    FlexItem items[FLEX_MAX_ITEMS];
    int item_count = 0;

    lxb_dom_node_t *child = lxb_dom_node_first_child(container);
    while (child && item_count < FLEX_MAX_ITEMS) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *cel = lxb_dom_interface_element(child);
            ComputedStyle ccss;
            css_engine_get_style(cel, &ccss);

            if (!(ccss.has_display && ccss.display == 0)) {
                FlexItem *fi = &items[item_count++];
                fi->node = child;
                fi->grow = ccss.has_flex_grow ? ccss.flex_grow : 0;
                fi->shrink = ccss.has_flex_shrink ? ccss.flex_shrink : 1;
                fi->cross_size = 0;

                /* width als Basis wenn kein flex-basis */
                if (ccss.has_flex_basis && ccss.flex_basis >= 0) {
                    fi->basis = ccss.flex_basis;
                } else if (ccss.has_width && ccss.width >= 0) {
                    fi->basis = ccss.width;
                } else if (ccss.has_width && ccss.width_pct >= 0) {
                    fi->basis = avail_main * ccss.width_pct / 100.0f;
                } else {
                    float cross = 0;
                    fi->basis = measure_intrinsic(ctx, child,
                        is_column ? avail_main : avail_main,
                        r, g, b, is_link, href, in_pre, &cross);
                    fi->cross_size = cross;
                }
                fi->final_size = fi->basis;
            }
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            /* Text-Knoten in Flex-Container werden zu anonymen Flex-Items */
            size_t text_len;
            const lxb_char_t *raw = lxb_dom_node_text_content(child, &text_len);
            if (raw && text_len > 0) {
                FlexItem *fi = &items[item_count++];
                fi->node = child;
                fi->grow = 0; fi->shrink = 1;
                fi->cross_size = lh;
                fi->basis = 0; fi->final_size = 0;
            }
        }
        child = lxb_dom_node_next(child);
    }

    if (item_count == 0) {
        ctx->cursor_y += style->margin_bottom * lh;
        return;
    }

    /* Phase B: Flex-Algorithmus */
    float total_basis = 0;
    float total_gaps = (item_count > 1) ? gap * (float)(item_count - 1) : 0;
    for (int i = 0; i < item_count; i++)
        total_basis += items[i].basis;

    float remaining = avail_main - total_basis - total_gaps;

    if (remaining > 0) {
        /* Positiver Restplatz: flex-grow verteilen */
        float total_grow = 0;
        for (int i = 0; i < item_count; i++)
            total_grow += items[i].grow;
        if (total_grow > 0) {
            for (int i = 0; i < item_count; i++)
                items[i].final_size += remaining * (items[i].grow / total_grow);
        }
    } else if (remaining < 0) {
        /* Negativer Restplatz: flex-shrink verteilen */
        float total_shrink = 0;
        for (int i = 0; i < item_count; i++)
            total_shrink += items[i].shrink * items[i].basis;
        if (total_shrink > 0) {
            for (int i = 0; i < item_count; i++) {
                float share = (items[i].shrink * items[i].basis) / total_shrink;
                items[i].final_size += remaining * share;
                if (items[i].final_size < 0) items[i].final_size = 0;
            }
        }
    }

    /* Phase C: Positionierung (justify-content) */
    float total_used = total_gaps;
    for (int i = 0; i < item_count; i++)
        total_used += items[i].final_size;

    float start_offset = 0;
    float item_gap = gap;

    switch (css->justify_content) {
    case 0: /* flex-start */
        break;
    case 1: /* flex-end */
        start_offset = avail_main - total_used;
        break;
    case 2: /* center */
        start_offset = (avail_main - total_used) * 0.5f;
        break;
    case 3: /* space-between */
        if (item_count > 1)
            item_gap = gap + (avail_main - total_used) / (float)(item_count - 1);
        break;
    case 4: /* space-around */
        if (item_count > 0) {
            float sp = (avail_main - total_used) / (float)item_count;
            start_offset = sp * 0.5f;
            item_gap = gap + sp;
        }
        break;
    }
    if (start_offset < 0) start_offset = 0;

    /* Phase D: Kinder layouten */
    float pos = start_offset;
    float max_cross = 0;

    for (int i = 0; i < item_count; i++) {
        float child_x, child_y, child_w;

        if (is_column) {
            child_x = container_x;
            child_y = container_y + pos;
            child_w = avail_main;
        } else {
            child_x = container_x + pos;
            child_y = container_y;
            child_w = items[i].final_size;
        }

        /* Kind layouten */
        float old_bx = ctx->block_start_x;
        float old_cx = ctx->cursor_x;
        float old_cy = ctx->cursor_y;
        int old_als = ctx->at_line_start;

        ctx->block_start_x = child_x;
        ctx->cursor_x = child_x;
        ctx->cursor_y = child_y;
        ctx->at_line_start = 1;

        extern void layout_node(LayoutCtx *ctx, lxb_dom_node_t *node,
                                float r, float g, float b,
                                int is_link, const char *href,
                                int in_pre, float right_edge);

        layout_node(ctx, items[i].node, r, g, b, is_link, href, in_pre,
                    child_x + child_w);

        float child_bottom = ctx->cursor_y;
        if (!ctx->at_line_start) child_bottom += lh;
        float child_cross = child_bottom - child_y;
        if (child_cross > max_cross) max_cross = child_cross;

        ctx->block_start_x = old_bx;
        ctx->cursor_x = old_cx;
        ctx->at_line_start = old_als;

        /* Nur cursor_y behalten wenn column */
        if (!is_column) ctx->cursor_y = old_cy;

        pos += items[i].final_size + item_gap;
    }

    /* Container-Höhe aktualisieren */
    if (is_column) {
        /* cursor_y wurde durch die Kinder vorangetrieben */
    } else {
        ctx->cursor_y = container_y + max_cross;
    }

    /* Hintergrund-Box aktualisieren mit finaler Hoehe */
    if (css->has_bg && ctx->layout->count > 0) {
        /* Finde die BG-Box und aktualisiere ihre Hoehe */
        for (int i = ctx->layout->count - 1; i >= 0; i--) {
            LayoutBox *b = &ctx->layout->boxes[i];
            if (b->bg_a > 0 && !b->text && !b->is_hr &&
                b->y == container_y) {
                b->h = ctx->cursor_y - container_y;
                break;
            }
        }
    }

    ctx->cursor_x = ctx->block_start_x;
    ctx->at_line_start = 1;
    ctx->cursor_y += style->margin_bottom * lh;
}

/* ---- Rekursiver DOM-Walk ---- */

static void layout_node(LayoutCtx *ctx, lxb_dom_node_t *node,
                        float r, float g, float b,
                        int is_link, const char *href,
                        int in_pre, float right_edge);

static void layout_children(LayoutCtx *ctx, lxb_dom_node_t *parent,
                            float r, float g, float b,
                            int is_link, const char *href,
                            int in_pre, float right_edge)
{
    lxb_dom_node_t *child = lxb_dom_node_first_child(parent);
    while (child) {
        layout_node(ctx, child, r, g, b, is_link, href, in_pre, right_edge);
        child = lxb_dom_node_next(child);
    }
}

static void layout_node(LayoutCtx *ctx, lxb_dom_node_t *node,
                        float r, float g, float b,
                        int is_link, const char *href,
                        int in_pre, float right_edge)
{
    if (ctx->layout->count >= MAX_BOXES) return;

    /* Text-Knoten */
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t text_len;
        const lxb_char_t *raw = lxb_dom_node_text_content(node, &text_len);
        if (!raw || text_len == 0) return;

        if (in_pre) {
            layout_pre_text(ctx, (const char *)raw, text_len, r, g, b, right_edge);
        } else {
            char *collapsed = collapse_whitespace((const char *)raw, text_len);
            if (collapsed) {
                if (collapsed[0] != '\0')
                    layout_text(ctx, collapsed, ctx->font_scale, r, g, b, is_link, href, right_edge);
                free(collapsed);
            }
        }
        return;
    }

    /* Nur Element-Knoten */
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) return;

    lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
    DispMode disp = display_for_tag(tag);

    /* Tags die IMMER unsichtbar sind — CSS darf das nicht ueberschreiben */
    if (disp == DISP_NONE) return;

    TagStyle style = style_for_tag(tag);
    float lh = ctx->line_h;

    /* CSS Computed Styles als Overlay */
    lxb_dom_element_t *el = lxb_dom_interface_element(node);
    ComputedStyle css;
    css_engine_get_style(el, &css);

    if (css.has_display) {
        if (css.display == 0) disp = DISP_NONE;
        else if (css.display == 1) disp = DISP_BLOCK;
        else if (css.display == 2) disp = DISP_INLINE;
        else if (css.display == 4) disp = DISP_FLEX;
    }
    if (css.has_color) { style.r = css.color_r; style.g = css.color_g; style.b = css.color_b; }
    if (css.has_font_size) style.font_scale = css.font_size / 16.0f;
    if (css.has_margin_top) style.margin_top = css.margin_top / lh;
    if (css.has_margin_bottom) style.margin_bottom = css.margin_bottom / lh;
    if (css.has_margin_left) style.padding_left += css.margin_left;
    if (css.has_padding_left) style.padding_left += css.padding_left;

    /* CSS display:none fuer andere Elemente */
    if (disp == DISP_NONE) return;

    /* Farbe vererben oder überschreiben */
    float nr = r, ng = g, nb = b;
    int n_is_link = is_link;
    int n_in_pre = in_pre;

    /* CSS-Color hat hoechste Prioritaet */
    if (css.has_color) { nr = css.color_r; ng = css.color_g; nb = css.color_b; }

    /* href fuer Links vererben */
    const char *n_href = href;
    char *a_href_alloc = NULL;

    /* Bestimmte Tags setzen eigene Farbe */
    if (tag == LXB_TAG_A) {
        nr = style.r; ng = style.g; nb = style.b;
        n_is_link = 1;
        a_href_alloc = get_str_attr(lxb_dom_interface_element(node), "href");
        if (a_href_alloc) n_href = a_href_alloc;
    } else if (tag == LXB_TAG_CODE || tag == LXB_TAG_PRE ||
               tag == LXB_TAG_BLOCKQUOTE ||
               tag == LXB_TAG_H1 || tag == LXB_TAG_H2 || tag == LXB_TAG_H3 ||
               tag == LXB_TAG_H4 || tag == LXB_TAG_H5 || tag == LXB_TAG_H6) {
        nr = style.r; ng = style.g; nb = style.b;
    }

    if (tag == LXB_TAG_PRE) n_in_pre = 1;

    /* CSS text-decoration + text-align + font-scale in Kontext vererben */
    int old_text_decoration = ctx->text_decoration;
    float old_font_scale = ctx->font_scale;
    if (css.has_text_decoration) ctx->text_decoration = css.text_decoration;
    if (css.has_text_align) ctx->text_align = css.text_align;
    if (css.has_font_size || style.font_scale != 1.0f)
        ctx->font_scale = style.font_scale;

    /* ---- Spezialfälle ---- */

    if (tag == LXB_TAG_BR) {
        ctx->cursor_y += lh;
        ctx->cursor_x = ctx->block_start_x;
        ctx->at_line_start = 1;
        return;
    }

    if (tag == LXB_TAG_HR) {
        if (!ctx->at_line_start)
            ctx->cursor_y += lh;
        ctx->cursor_y += lh * 0.5f;
        emit_box(ctx, ctx->block_start_x, ctx->cursor_y,
                 right_edge - ctx->block_start_x, 1.0f,
                 NULL, 1.0f, 0, 0, 0,
                 0.40f, 0.40f, 0.45f, 1.0f,
                 1, 0, 0);
        ctx->cursor_y += lh * 0.5f;
        ctx->cursor_x = ctx->block_start_x;
        ctx->at_line_start = 1;
        return;
    }

    if (tag == LXB_TAG_IMG) {
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        char *src = get_str_attr(el, "src");

        /* Dimensionen aus Attributen */
        int attr_w = get_int_attr(el, "width");
        int attr_h = get_int_attr(el, "height");

        /* Bild im Cache suchen */
        const CachedImage *ci = src ? image_cache_get(src) : NULL;

        float iw, ih;
        GLuint tex = 0;

        if (ci) {
            tex = ci->texture;
            float nat_w = (float)ci->width;
            float nat_h = (float)ci->height;

            if (attr_w > 0 && attr_h > 0) {
                iw = (float)attr_w;
                ih = (float)attr_h;
            } else if (attr_w > 0) {
                iw = (float)attr_w;
                ih = iw * (nat_h / nat_w);
            } else if (attr_h > 0) {
                ih = (float)attr_h;
                iw = ih * (nat_w / nat_h);
            } else {
                iw = nat_w;
                ih = nat_h;
            }

            /* Auf verfuegbare Breite begrenzen */
            float max_w = ctx->avail_w - ctx->block_start_x;
            if (iw > max_w) {
                ih = ih * (max_w / iw);
                iw = max_w;
            }
        } else {
            /* Platzhalter */
            iw = (attr_w > 0) ? (float)attr_w : 150.0f;
            ih = (attr_h > 0) ? (float)attr_h : 80.0f;
            float max_w = ctx->avail_w - ctx->block_start_x;
            if (iw > max_w) iw = max_w;
        }

        if (!ctx->at_line_start)
            ctx->cursor_y += lh;

        int idx = ctx->layout->count;
        emit_box(ctx, ctx->cursor_x, ctx->cursor_y, iw, ih,
                 ci ? NULL : "[img]", 1.0f,
                 0.50f, 0.50f, 0.55f,
                 ci ? 0.0f : 0.18f, ci ? 0.0f : 0.18f, ci ? 0.0f : 0.20f,
                 ci ? 0.0f : 1.0f,
                 0, 0, ci ? 0 : 1);

        if (tex && idx < ctx->layout->count)
            ctx->layout->boxes[idx].image_texture = tex;

        ctx->cursor_y += ih + lh * 0.3f;
        ctx->cursor_x = ctx->block_start_x;
        ctx->at_line_start = 1;
        free(src);
        return;
    }

    /* ---- Flex-Container ---- */

    if (disp == DISP_FLEX) {
        layout_flex(ctx, node, &style, &css, nr, ng, nb,
                    n_is_link, n_href, n_in_pre, right_edge);
        ctx->font_scale = old_font_scale;
        ctx->text_decoration = old_text_decoration;
        free(a_href_alloc);
        return;
    }

    /* ---- Block-Elemente ---- */

    if (disp == DISP_BLOCK || disp == DISP_LIST_ITEM) {
        /* Pending Inline abschließen */
        if (!ctx->at_line_start) {
            ctx->cursor_y += lh;
            ctx->cursor_x = ctx->block_start_x;
            ctx->at_line_start = 1;
        }

        /* Margin top */
        ctx->cursor_y += style.margin_top * lh;

        /* Padding/Indent + width/max-width */
        float old_block_x = ctx->block_start_x;
        float old_right = right_edge;
        ctx->block_start_x += style.padding_left;
        if (css.has_padding_right)
            right_edge -= css.padding_right;
        ctx->cursor_x = ctx->block_start_x;
        float new_right = right_edge;

        /* CSS width begrenzt die rechte Kante */
        float parent_w = right_edge - old_block_x;
        float block_width = right_edge - ctx->block_start_x;

        if (css.has_width) {
            float w = css.width;
            if (css.width_pct >= 0)
                w = parent_w * css.width_pct / 100.0f;
            if (w >= 0) {
                block_width = w;
                new_right = ctx->block_start_x + w;
            }
        }
        if (css.has_max_width) {
            float mw = css.max_width;
            if (css.max_width_pct >= 0)
                mw = parent_w * css.max_width_pct / 100.0f;
            if (mw >= 0 && mw < block_width) {
                block_width = mw;
                new_right = ctx->block_start_x + mw;
            }
        }

        /* margin: auto — Zentrierung */
        if (css.margin_left_auto && css.margin_right_auto &&
            (css.has_width || css.has_max_width)) {
            float avail = right_edge - old_block_x;
            float offset = (avail - block_width) * 0.5f;
            if (offset > 0) {
                ctx->block_start_x = old_block_x + offset;
                ctx->cursor_x = ctx->block_start_x;
                new_right = ctx->block_start_x + block_width;
            }
        }

        /* Border zeichnen */
        if (css.has_border && css.border_width > 0) {
            float bw = new_right - old_block_x;
            if (bw < 0) bw = right_edge - old_block_x;
            emit_box(ctx, old_block_x, ctx->cursor_y, bw, css.border_width,
                     NULL, 1.0f, 0, 0, 0,
                     css.border_r, css.border_g, css.border_b, 1.0f,
                     0, 0, 0);
            ctx->cursor_y += css.border_width;
        }

        /* Background */
        int bg_box_idx = -1;
        if (css.has_bg) {
            bg_box_idx = ctx->layout->count;
            float bw = new_right - old_block_x;
            if (bw < 0) bw = right_edge - old_block_x;
            emit_box(ctx, old_block_x, ctx->cursor_y, bw, lh,
                     NULL, 1.0f, 0, 0, 0,
                     css.bg_r, css.bg_g, css.bg_b, css.bg_a,
                     0, 0, 0);
        }

        /* Padding top */
        if (css.has_padding_top)
            ctx->cursor_y += css.padding_top;

        /* Bullet für li */
        if (disp == DISP_LIST_ITEM) {
            float bw = renderer_text_width("- ");
            emit_box(ctx, ctx->block_start_x - bw, ctx->cursor_y, bw, lh,
                     "\xe2\x80\xa2", 1.0f, 0.55f, 0.55f, 0.60f,
                     0, 0, 0, 0, 0, 0, 0);
        }

        /* text-align fuer diesen Block merken */
        int old_text_align = ctx->text_align;
        if (css.has_text_align) ctx->text_align = css.text_align;
        int first_box = ctx->layout->count;

        /* Kinder layouten */
        layout_children(ctx, node, nr, ng, nb, n_is_link, n_href, n_in_pre, new_right);

        /* text-align anwenden: Zeilen horizontal verschieben */
        if (ctx->text_align == 2 || ctx->text_align == 1) {
            float block_w = new_right - ctx->block_start_x;
            float line_y = -1;
            float line_max_x = 0;
            int line_start = first_box;

            for (int i = first_box; i <= ctx->layout->count; i++) {
                float by = (i < ctx->layout->count) ? ctx->layout->boxes[i].y : -2;
                if (by != line_y && line_y >= 0) {
                    /* Zeile abschliessen: verschieben */
                    float line_w = line_max_x - ctx->block_start_x;
                    float shift = 0;
                    if (ctx->text_align == 2) /* center */
                        shift = (block_w - line_w) * 0.5f;
                    else if (ctx->text_align == 1) /* right */
                        shift = block_w - line_w;
                    if (shift > 0) {
                        for (int j = line_start; j < i; j++)
                            ctx->layout->boxes[j].x += shift;
                    }
                    line_start = i;
                    line_max_x = 0;
                }
                if (i < ctx->layout->count) {
                    LayoutBox *b = &ctx->layout->boxes[i];
                    line_y = b->y;
                    float right = b->x + b->w;
                    if (right > line_max_x) line_max_x = right;
                }
            }
        }
        ctx->text_align = old_text_align;

        /* Pending Inline abschließen */
        if (!ctx->at_line_start) {
            ctx->cursor_y += lh * style.font_scale;
            ctx->at_line_start = 1;
        }

        /* Padding bottom */
        if (css.has_padding_bottom)
            ctx->cursor_y += css.padding_bottom;

        /* Hintergrund-Box Hoehe aktualisieren */
        if (bg_box_idx >= 0 && bg_box_idx < ctx->layout->count) {
            ctx->layout->boxes[bg_box_idx].h = ctx->cursor_y -
                ctx->layout->boxes[bg_box_idx].y;
        }

        /* Border bottom */
        if (css.has_border && css.border_width > 0) {
            float bw = new_right - old_block_x;
            if (bw < 0) bw = right_edge - old_block_x;
            emit_box(ctx, old_block_x, ctx->cursor_y, bw, css.border_width,
                     NULL, 1.0f, 0, 0, 0,
                     css.border_r, css.border_g, css.border_b, 1.0f,
                     0, 0, 0);
            ctx->cursor_y += css.border_width;
        }

        /* Margin bottom */
        ctx->cursor_y += style.margin_bottom * lh;

        /* Indent zurücksetzen */
        ctx->block_start_x = old_block_x;
        ctx->cursor_x = ctx->block_start_x;
        right_edge = old_right;

        ctx->font_scale = old_font_scale;
        ctx->text_decoration = old_text_decoration;
        free(a_href_alloc);
        return;
    }

    /* ---- Inline-Elemente ---- */

    layout_children(ctx, node, nr, ng, nb, n_is_link, n_href, n_in_pre, right_edge);
    ctx->font_scale = old_font_scale;
    ctx->text_decoration = old_text_decoration;
    free(a_href_alloc);
}

/* ---- Public API ---- */

void html_layout_build(lxb_html_document_t *doc, float avail_w, Layout *out)
{
    memset(out, 0, sizeof(*out));
    if (!doc) return;

    lxb_dom_element_t *root = lxb_dom_document_element(&doc->dom_document);
    if (!root) return;

    LayoutCtx ctx = {0};
    ctx.layout = out;
    ctx.avail_w = avail_w;
    ctx.cursor_x = 0;
    ctx.cursor_y = 0;
    ctx.block_start_x = 0;
    ctx.line_h = renderer_line_height();
    ctx.at_line_start = 1;
    ctx.font_scale = 1.0f;

    layout_node(&ctx, lxb_dom_interface_node(root),
                0.80f, 0.82f, 0.84f, 0, NULL, 0, avail_w);
}

void html_layout_free(Layout *layout)
{
    if (!layout->boxes) return;
    for (int i = 0; i < layout->count; i++) {
        free(layout->boxes[i].text);
        free(layout->boxes[i].href);
    }
    free(layout->boxes);
    layout->boxes = NULL;
    layout->count = 0;
    layout->capacity = 0;
    layout->total_height = 0;
}
