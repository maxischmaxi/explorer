#define _POSIX_C_SOURCE 200809L
#include "css_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/css/property/const.h>
#include <lexbor/css/value/const.h>

/* ---- Hilfsfunktionen: CSS-Werte extrahieren ---- */

static float length_to_px(const lxb_css_value_length_t *len, float parent_font_size)
{
    double val = len->num;
    /* Unit IDs aus lexbor/css/unit/const.h */
    unsigned u = (unsigned)len->unit;
    if (u == 0x07) return (float)val;                         /* px */
    if (u == 0x0a) return (float)(val * parent_font_size);    /* em */
    if (u == 0x0e) return (float)(val * 16.0);                /* rem */
    if (u == 0x06) return (float)(val * 1.333);               /* pt */
    if (u == 0x02) return (float)(val * 37.795);              /* cm */
    if (u == 0x04) return (float)(val * 3.7795);              /* mm */
    if (u == 0x03) return (float)(val * 96.0);                /* in */
    return (float)val;
}

static float length_perc_to_px(const void *prop, float parent_size, float font_size)
{
    const lxb_css_value_length_percentage_t *lp = prop;
    if (lp->type == LXB_CSS_VALUE__LENGTH)
        return length_to_px(&lp->u.length, font_size);
    if (lp->type == LXB_CSS_VALUE__PERCENTAGE)
        return (float)(lp->u.percentage.num / 100.0) * parent_size;
    return -1;
}

static void color_to_rgb(const lxb_css_value_color_t *color,
                         float *r, float *g, float *b, int *ok)
{
    *ok = 0;
    if (!color) return;

    if (color->type == LXB_CSS_VALUE_COLOR) {
        /* rgb()/rgba() function */
        const lxb_css_value_color_rgba_t *rgba = &color->u.rgb;
        if (rgba->r.type == LXB_CSS_VALUE__NUMBER) {
            *r = (float)(rgba->r.u.number.num / 255.0);
            *g = (float)(rgba->g.u.number.num / 255.0);
            *b = (float)(rgba->b.u.number.num / 255.0);
        } else if (rgba->r.type == LXB_CSS_VALUE__PERCENTAGE) {
            *r = (float)(rgba->r.u.percentage.num / 100.0);
            *g = (float)(rgba->g.u.percentage.num / 100.0);
            *b = (float)(rgba->b.u.percentage.num / 100.0);
        }
        *ok = 1;
    } else {
        /* Hex or named color - stored in hex union */
        const lxb_css_value_color_hex_t *hex = &color->u.hex;
        *r = (float)((hex->rgba.r) / 255.0);
        *g = (float)((hex->rgba.g) / 255.0);
        *b = (float)((hex->rgba.b) / 255.0);
        *ok = 1;
    }

    /* Clamp */
    if (*r < 0) *r = 0;
    if (*r > 1) *r = 1;
    if (*g < 0) *g = 0;
    if (*g > 1) *g = 1;
    if (*b < 0) *b = 0;
    if (*b > 1) *b = 1;
}

/* ---- CSS-Engine Apply ---- */

void css_engine_apply(lxb_html_document_t *doc, ResourceCollection *resources)
{
    if (!doc) return;

    lxb_status_t status;

    /* CSS-Infrastruktur initialisieren */
    status = lxb_dom_document_css_init(&doc->dom_document, true);
    if (status != LXB_STATUS_OK) {
        printf("  CSS init failed\n");
        return;
    }

    /* Alle CSS-Resources parsen und anwenden */
    int sheet_count = 0;

    if (resources) {
        for (int i = 0; i < resources->count; i++) {
            if (resources->entries[i].type != RES_TYPE_CSS)
                continue;
            if (!resources->entries[i].data)
                continue;

            lxb_css_stylesheet_t *sst = lxb_css_stylesheet_create(
                doc->dom_document.css->memory);
            if (!sst) continue;

            status = lxb_css_stylesheet_parse(sst, doc->dom_document.css->parser,
                (const lxb_char_t *)resources->entries[i].data,
                resources->entries[i].data_len);
            if (status != LXB_STATUS_OK) {
                printf("  CSS parse failed: %s\n",
                       resources->entries[i].url ? resources->entries[i].url : "?");
                continue;
            }

            status = lxb_dom_document_stylesheet_apply(&doc->dom_document, sst);
            if (status == LXB_STATUS_OK) {
                sheet_count++;
            }
        }
    }

    /* Computed Styles berechnen (Cascade + Vererbung) */
    status = lxb_style_init(doc);
    if (status != LXB_STATUS_OK) {
        printf("  CSS style init failed\n");
    }

    printf("  CSS: %d stylesheets applied\n", sheet_count);
}

/* ---- Computed Style Lookup ---- */

void css_engine_get_style(lxb_dom_element_t *el, ComputedStyle *out)
{
    memset(out, 0, sizeof(*out));
    out->display = -1;
    out->font_size = 16.0f;
    out->flex_basis = -1.0f;
    out->flex_shrink = 1.0f;
    out->width = -1.0f;
    out->height = -1.0f;
    out->max_width = -1.0f;

    if (!el) return;

    /* Pruefen ob CSS-System auf dem Dokument initialisiert ist */
    lxb_dom_node_t *node = lxb_dom_interface_node(el);
    if (!node || !node->owner_document || !node->owner_document->css)
        return;

    float fs = 16.0f; /* default font size */

    /* Display */
    const lxb_css_property_display_t *disp =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_DISPLAY);
    if (disp) {
        out->has_display = 1;
        if (disp->a == LXB_CSS_VALUE_NONE) {
            out->display = 0;  /* NONE */
        } else if (disp->a == LXB_CSS_VALUE_BLOCK ||
                   disp->a == LXB_CSS_VALUE_FLOW_ROOT) {
            out->display = 1;  /* BLOCK */
        } else if (disp->a == LXB_CSS_VALUE_INLINE ||
                   disp->a == LXB_CSS_VALUE_INLINE_BLOCK) {
            out->display = 2;  /* INLINE */
        } else if (disp->a == LXB_CSS_VALUE_FLEX) {
            out->display = 4;  /* FLEX */
        }
    }

    /* Color */
    const lxb_css_value_color_t *color =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_COLOR);
    if (color) {
        color_to_rgb(color, &out->color_r, &out->color_g, &out->color_b,
                     &out->has_color);
    }

    /* Background-color */
    const lxb_css_value_color_t *bg =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BACKGROUND_COLOR);
    if (bg) {
        color_to_rgb(bg, &out->bg_r, &out->bg_g, &out->bg_b, &out->has_bg);
        if (out->has_bg) out->bg_a = 1.0f;
    }

    /* Font-size */
    const lxb_css_value_length_percentage_t *fsp =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FONT_SIZE);
    if (fsp) {
        float v = length_perc_to_px(fsp, fs, fs);
        if (v > 0) { out->font_size = v; out->has_font_size = 1; fs = v; }
    }

    /* Margins */
    const void *mt = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_TOP);
    if (mt) { float v = length_perc_to_px(mt, 0, fs); if (v >= 0) { out->margin_top = v; out->has_margin_top = 1; } }

    const void *mb = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_BOTTOM);
    if (mb) { float v = length_perc_to_px(mb, 0, fs); if (v >= 0) { out->margin_bottom = v; out->has_margin_bottom = 1; } }

    const void *ml = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_LEFT);
    if (ml) { float v = length_perc_to_px(ml, 0, fs); if (v >= 0) { out->margin_left = v; out->has_margin_left = 1; } }

    const void *mr = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_RIGHT);
    if (mr) { float v = length_perc_to_px(mr, 0, fs); if (v >= 0) { out->margin_right = v; out->has_margin_right = 1; } }

    /* Paddings */
    const void *pt = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_PADDING_TOP);
    if (pt) { float v = length_perc_to_px(pt, 0, fs); if (v >= 0) { out->padding_top = v; out->has_padding_top = 1; } }

    const void *pb = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_PADDING_BOTTOM);
    if (pb) { float v = length_perc_to_px(pb, 0, fs); if (v >= 0) { out->padding_bottom = v; out->has_padding_bottom = 1; } }

    const void *pl = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_PADDING_LEFT);
    if (pl) { float v = length_perc_to_px(pl, 0, fs); if (v >= 0) { out->padding_left = v; out->has_padding_left = 1; } }

    const void *pr = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_PADDING_RIGHT);
    if (pr) { float v = length_perc_to_px(pr, 0, fs); if (v >= 0) { out->padding_right = v; out->has_padding_right = 1; } }

    /* Flex container properties */
    const void *fd = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FLEX_DIRECTION);
    if (fd) {
        const lxb_css_value_type_t *t = fd;
        out->has_flex_direction = 1;
        if (*t == LXB_CSS_VALUE_ROW) out->flex_direction = 0;
        else if (*t == LXB_CSS_VALUE_ROW_REVERSE) out->flex_direction = 1;
        else if (*t == LXB_CSS_VALUE_COLUMN) out->flex_direction = 2;
        else if (*t == LXB_CSS_VALUE_COLUMN_REVERSE) out->flex_direction = 3;
    }

    const void *fw = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FLEX_WRAP);
    if (fw) {
        const lxb_css_value_type_t *t = fw;
        out->has_flex_wrap = 1;
        if (*t == LXB_CSS_VALUE_WRAP) out->flex_wrap = 1;
        else if (*t == LXB_CSS_VALUE_WRAP_REVERSE) out->flex_wrap = 2;
    }

    const void *jc = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_JUSTIFY_CONTENT);
    if (jc) {
        const lxb_css_value_type_t *t = jc;
        out->has_justify = 1;
        if (*t == LXB_CSS_VALUE_FLEX_START || *t == LXB_CSS_VALUE_START) out->justify_content = 0;
        else if (*t == LXB_CSS_VALUE_FLEX_END || *t == LXB_CSS_VALUE_END) out->justify_content = 1;
        else if (*t == LXB_CSS_VALUE_CENTER) out->justify_content = 2;
        else if (*t == LXB_CSS_VALUE_SPACE_BETWEEN) out->justify_content = 3;
        else if (*t == LXB_CSS_VALUE_SPACE_AROUND) out->justify_content = 4;
    }

    const void *ai = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_ALIGN_ITEMS);
    if (ai) {
        const lxb_css_value_type_t *t = ai;
        out->has_align_items = 1;
        if (*t == LXB_CSS_VALUE_STRETCH) out->align_items = 0;
        else if (*t == LXB_CSS_VALUE_FLEX_START || *t == LXB_CSS_VALUE_START) out->align_items = 1;
        else if (*t == LXB_CSS_VALUE_FLEX_END || *t == LXB_CSS_VALUE_END) out->align_items = 2;
        else if (*t == LXB_CSS_VALUE_CENTER) out->align_items = 3;
    }

    /* Flex item properties */
    const void *fg = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FLEX_GROW);
    if (fg) {
        const lxb_css_value_number_type_t *n = fg;
        if (n->type == LXB_CSS_VALUE__NUMBER) {
            out->flex_grow = (float)n->number.num;
            out->has_flex_grow = 1;
        }
    }

    const void *fsh = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FLEX_SHRINK);
    if (fsh) {
        const lxb_css_value_number_type_t *n = fsh;
        if (n->type == LXB_CSS_VALUE__NUMBER) {
            out->flex_shrink = (float)n->number.num;
            out->has_flex_shrink = 1;
        }
    }

    const void *fb = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FLEX_BASIS);
    if (fb) {
        float v = length_perc_to_px(fb, 0, fs);
        if (v >= 0) { out->flex_basis = v; out->has_flex_basis = 1; }
    }

    /* ---- Width / Height / Max-Width ---- */

    const void *wp = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_WIDTH);
    if (wp) {
        float v = length_perc_to_px(wp, 0, fs);
        if (v >= 0) { out->width = v; out->has_width = 1; }
    }

    const void *hp = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_HEIGHT);
    if (hp) {
        float v = length_perc_to_px(hp, 0, fs);
        if (v >= 0) { out->height = v; out->has_height = 1; }
    }

    const void *mwp = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MAX_WIDTH);
    if (mwp) {
        float v = length_perc_to_px(mwp, 0, fs);
        if (v >= 0) { out->max_width = v; out->has_max_width = 1; }
    }

    /* ---- Text-Align ---- */

    const void *ta = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_TEXT_ALIGN);
    if (ta) {
        const lxb_css_value_type_t *t = ta;
        out->has_text_align = 1;
        if (*t == LXB_CSS_VALUE_LEFT || *t == LXB_CSS_VALUE_START)
            out->text_align = 0;
        else if (*t == LXB_CSS_VALUE_RIGHT || *t == LXB_CSS_VALUE_END)
            out->text_align = 1;
        else if (*t == LXB_CSS_VALUE_CENTER)
            out->text_align = 2;
        else if (*t == LXB_CSS_VALUE_JUSTIFY)
            out->text_align = 3;
    }

    /* ---- Text-Decoration ---- */

    const void *td = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_TEXT_DECORATION_LINE);
    if (td) {
        const lxb_css_value_type_t *t = td;
        out->has_text_decoration = 1;
        if (*t == LXB_CSS_VALUE_NONE)
            out->text_decoration = 0;
        else if (*t == LXB_CSS_VALUE_UNDERLINE)
            out->text_decoration = 1;
        else if (*t == LXB_CSS_VALUE_LINE_THROUGH)
            out->text_decoration = 2;
    }
    if (!out->has_text_decoration) {
        const void *td2 = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_TEXT_DECORATION);
        if (td2) {
            const lxb_css_value_type_t *t = td2;
            out->has_text_decoration = 1;
            if (*t == LXB_CSS_VALUE_NONE)
                out->text_decoration = 0;
            else if (*t == LXB_CSS_VALUE_UNDERLINE)
                out->text_decoration = 1;
            else if (*t == LXB_CSS_VALUE_LINE_THROUGH)
                out->text_decoration = 2;
        }
    }

    /* ---- Border (vereinfacht: border-top als Repraesentant) ---- */

    const lxb_css_property_border_t *bt =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BORDER_TOP);
    if (bt && bt->style != LXB_CSS_VALUE_NONE) {
        if (bt->width.type == LXB_CSS_VALUE__LENGTH) {
            float v = length_to_px(&bt->width.length, fs);
            if (v > 0) {
                out->border_width = v;
                out->has_border = 1;
                out->border_r = 0.40f;
                out->border_g = 0.40f;
                out->border_b = 0.45f;
                int ok = 0;
                color_to_rgb(&bt->color, &out->border_r, &out->border_g,
                             &out->border_b, &ok);
            }
        }
    }
    /* Fallback: border-top-color separat */
    if (!out->has_border) {
        const lxb_css_value_color_t *btc =
            lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BORDER_TOP_COLOR);
        if (btc) {
            int ok = 0;
            color_to_rgb(btc, &out->border_r, &out->border_g, &out->border_b, &ok);
            if (ok) { out->has_border = 1; out->border_width = 1.0f; }
        }
    }

    /* ---- Overflow ---- */

    const void *ox = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_OVERFLOW_X);
    if (ox) {
        const lxb_css_value_type_t *t = ox;
        out->has_overflow = 1;
        if (*t == LXB_CSS_VALUE_HIDDEN) out->overflow = 1;
        else if (*t == LXB_CSS_VALUE_SCROLL) out->overflow = 2;
        else if (*t == LXB_CSS_VALUE_AUTO) out->overflow = 3;
    }
}
