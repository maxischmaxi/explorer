#ifndef CSS_ENGINE_H
#define CSS_ENGINE_H

#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include "resource_fetch.h"

typedef struct {
    float margin_top, margin_right, margin_bottom, margin_left;
    float padding_top, padding_right, padding_bottom, padding_left;
    float color_r, color_g, color_b;
    float bg_r, bg_g, bg_b, bg_a;
    float font_size;
    int   display;       /* -1=nicht gesetzt, 0=NONE, 1=BLOCK, 2=INLINE, 3=LIST_ITEM, 4=FLEX */
    /* Flex container */
    int   flex_direction;    /* 0=row, 1=row-reverse, 2=column, 3=column-reverse */
    int   flex_wrap;         /* 0=nowrap, 1=wrap */
    int   justify_content;   /* 0=flex-start, 1=flex-end, 2=center, 3=space-between, 4=space-around */
    int   align_items;       /* 0=stretch, 1=flex-start, 2=flex-end, 3=center */
    float gap;
    /* Flex item */
    float flex_grow;
    float flex_shrink;
    float flex_basis;        /* -1 = auto */
    /* Sizing */
    float width, height;     /* -1 = auto */
    float width_pct;         /* -1 = nicht gesetzt, sonst 0..100 */
    float max_width;         /* -1 = none */
    float max_width_pct;     /* -1 = nicht gesetzt */
    /* Text */
    int   text_align;        /* 0=left, 1=right, 2=center, 3=justify */
    int   text_decoration;   /* Bitmask: 1=underline, 2=line-through, 4=overline */
    /* Border (vereinfacht: alle Seiten gleich) */
    float border_width;
    float border_r, border_g, border_b;
    /* Overflow */
    int   overflow;          /* 0=visible, 1=hidden, 2=scroll, 3=auto */
    /* Has-Flags */
    int   has_color;
    int   has_bg;
    int   margin_left_auto, margin_right_auto;
    int   has_margin_top, has_margin_bottom, has_margin_left, has_margin_right;
    int   has_padding_top, has_padding_bottom, has_padding_left, has_padding_right;
    int   has_font_size;
    int   has_display;
    int   has_flex_direction, has_flex_wrap, has_justify, has_align_items;
    int   has_gap, has_flex_grow, has_flex_shrink, has_flex_basis;
    int   has_width, has_height, has_max_width;
    int   has_text_align, has_text_decoration;
    int   has_border;
    int   has_overflow;
} ComputedStyle;

/* Parst alle CSS-Quellen und berechnet Computed Styles fuers DOM. */
void css_engine_apply(lxb_html_document_t *doc, ResourceCollection *resources);

/* Liest berechnete Style-Properties fuer ein Element. */
void css_engine_get_style(lxb_dom_element_t *el, ComputedStyle *out);

#endif
