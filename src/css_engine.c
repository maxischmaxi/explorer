#define _POSIX_C_SOURCE 200809L
#include "css_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>
#include <lexbor/css/property/const.h>
#include <lexbor/css/value/const.h>

#include "renderer.h"

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

/* ---- Farb-Konvertierungshilfen ---- */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float extract_alpha(const lxb_css_value_number_percentage_t *a)
{
    if (a->type == LXB_CSS_VALUE__NUMBER)
        return clampf((float)a->u.number.num, 0, 1);
    if (a->type == LXB_CSS_VALUE__PERCENTAGE)
        return clampf((float)(a->u.percentage.num / 100.0), 0, 1);
    return 1.0f; /* NONE oder unbekannt → opaque */
}

static float extract_hue(const lxb_css_value_hue_t *h)
{
    if (h->type == LXB_CSS_VALUE__NUMBER)
        return (float)h->u.number.num;
    /* Angle mit Unit */
    float deg = (float)h->u.angle.num;
    unsigned unit = (unsigned)h->u.angle.unit;
    if (unit == 0x18) deg *= (float)(180.0 / M_PI);  /* rad */
    else if (unit == 0x17) deg *= 0.9f;               /* grad */
    else if (unit == 0x19) deg *= 360.0f;              /* turn */
    /* 0x16 = deg → as-is */
    return deg;
}

static float extract_rgb_channel(const lxb_css_value_number_percentage_t *c)
{
    if (c->type == LXB_CSS_VALUE__NUMBER)
        return clampf((float)(c->u.number.num / 255.0), 0, 1);
    if (c->type == LXB_CSS_VALUE__PERCENTAGE)
        return clampf((float)(c->u.percentage.num / 100.0), 0, 1);
    return 0;
}

/* ---- HSL → RGB ---- */

static float hue_to_channel(float p, float q, float t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f)       return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b)
{
    /* h in degrees, s,l in [0,1] */
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    h /= 360.0f;

    if (s <= 0.0f) {
        *r = *g = *b = l;
        return;
    }

    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    *r = hue_to_channel(p, q, h + 1.0f/3.0f);
    *g = hue_to_channel(p, q, h);
    *b = hue_to_channel(p, q, h - 1.0f/3.0f);
}

/* ---- HWB → RGB ---- */

static void hwb_to_rgb(float h, float w, float bk, float *r, float *g, float *b)
{
    if (w + bk >= 1.0f) {
        float gray = w / (w + bk);
        *r = *g = *b = gray;
        return;
    }
    hsl_to_rgb(h, 1.0f, 0.5f, r, g, b);
    float scale = 1.0f - w - bk;
    *r = *r * scale + w;
    *g = *g * scale + w;
    *b = *b * scale + w;
}

/* ---- OKLab → sRGB ---- */

static float linear_to_srgb(float c)
{
    return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f/2.4f) - 0.055f;
}

static void oklab_to_rgb(float L, float a, float bv, float *r, float *g, float *b)
{
    float l_ = L + 0.3963377774f * a + 0.2158037573f * bv;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * bv;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * bv;

    float l3 = l_ * l_ * l_;
    float m3 = m_ * m_ * m_;
    float s3 = s_ * s_ * s_;

    float lr =  4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
    float lg = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
    float lb = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;

    *r = clampf(linear_to_srgb(lr), 0, 1);
    *g = clampf(linear_to_srgb(lg), 0, 1);
    *b = clampf(linear_to_srgb(lb), 0, 1);
}

/* ---- Universelle Farb-Extraktion ---- */

static void color_to_rgba(const lxb_css_value_color_t *color,
                           float *r, float *g, float *b, float *a, int *ok)
{
    *ok = 0;
    *a = 1.0f;
    if (!color) return;

    unsigned type = (unsigned)color->type;

    /* transparent */
    if (type == 0x0032) { /* LXB_CSS_VALUE_TRANSPARENT */
        *r = *g = *b = *a = 0;
        *ok = 1;
        return;
    }

    /* currentcolor — can't resolve here, caller uses text color */
    if (type == 0x0031) { /* LXB_CSS_VALUE_CURRENTCOLOR */
        *ok = 0;
        return;
    }

    /* hex (0x0033) */
    if (type == 0x0033) {
        const lxb_css_value_color_hex_t *hex = &color->u.hex;
        unsigned rv = hex->rgba.r, gv = hex->rgba.g, bv = hex->rgba.b;

        /* 3/4-stellig: Nibble expandieren (f → ff = 255) */
        if (hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_3 ||
            hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4) {
            rv *= 17; gv *= 17; bv *= 17;
        }

        *r = (float)rv / 255.0f;
        *g = (float)gv / 255.0f;
        *b = (float)bv / 255.0f;

        if (hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4 ||
            hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_8) {
            unsigned av = hex->rgba.a;
            if (hex->type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4)
                av *= 17;
            *a = (float)av / 255.0f;
        }
        *ok = 1;
        goto clamp;
    }

    /* rgb/rgba/color() (0x00db, 0x00dc, 0x00e4) */
    if (type == 0x00db || type == 0x00dc || type == 0x00e4) {
        const lxb_css_value_color_rgba_t *rgba = &color->u.rgb;
        *r = extract_rgb_channel(&rgba->r);
        *g = extract_rgb_channel(&rgba->g);
        *b = extract_rgb_channel(&rgba->b);
        *a = extract_alpha(&rgba->a);
        *ok = 1;
        goto clamp;
    }

    /* hsl/hsla (0x00dd, 0x00de) */
    if (type == 0x00dd || type == 0x00de) {
        const lxb_css_value_color_hsla_t *hsla = &color->u.hsl;
        float h = extract_hue(&hsla->h);
        float s = clampf((float)(hsla->s.percentage.num / 100.0), 0, 1);
        float l = clampf((float)(hsla->l.percentage.num / 100.0), 0, 1);
        hsl_to_rgb(h, s, l, r, g, b);
        *a = extract_alpha(&hsla->a);
        *ok = 1;
        goto clamp;
    }

    /* hwb (0x00df) */
    if (type == 0x00df) {
        const lxb_css_value_color_hsla_t *hwb = &color->u.hwb;
        float h = extract_hue(&hwb->h);
        float w = clampf((float)(hwb->s.percentage.num / 100.0), 0, 1);
        float bk = clampf((float)(hwb->l.percentage.num / 100.0), 0, 1);
        hwb_to_rgb(h, w, bk, r, g, b);
        *a = extract_alpha(&hwb->a);
        *ok = 1;
        goto clamp;
    }

    /* lab/oklab (0x00e0, 0x00e2) */
    if (type == 0x00e0 || type == 0x00e2) {
        const lxb_css_value_color_lab_t *lab = &color->u.lab;
        float L, av, bv;
        if (lab->l.type == LXB_CSS_VALUE__PERCENTAGE)
            L = (float)(lab->l.u.percentage.num / 100.0);
        else
            L = (float)lab->l.u.number.num;
        if (lab->a.type == LXB_CSS_VALUE__PERCENTAGE)
            av = (float)(lab->a.u.percentage.num / 100.0) * 0.4f;
        else
            av = (float)lab->a.u.number.num;
        if (lab->b.type == LXB_CSS_VALUE__PERCENTAGE)
            bv = (float)(lab->b.u.percentage.num / 100.0) * 0.4f;
        else
            bv = (float)lab->b.u.number.num;
        if (type == 0x00e2) /* oklab */
            oklab_to_rgb(L, av, bv, r, g, b);
        else
            oklab_to_rgb(L / 100.0f, av / 125.0f, bv / 125.0f, r, g, b);
        *a = extract_alpha(&lab->alpha);
        *ok = 1;
        goto clamp;
    }

    /* lch/oklch (0x00e1, 0x00e3) */
    if (type == 0x00e1 || type == 0x00e3) {
        const lxb_css_value_color_lch_t *lch = &color->u.lch;
        float L, C, H;
        if (lch->l.type == LXB_CSS_VALUE__PERCENTAGE)
            L = (float)(lch->l.u.percentage.num / 100.0);
        else
            L = (float)lch->l.u.number.num;
        if (lch->c.type == LXB_CSS_VALUE__PERCENTAGE)
            C = (float)(lch->c.u.percentage.num / 100.0) * 0.4f;
        else
            C = (float)lch->c.u.number.num;
        H = extract_hue(&lch->h);
        /* LCH → Lab: a = C*cos(H), b = C*sin(H) */
        float H_rad = H * (float)(M_PI / 180.0);
        float av = C * cosf(H_rad);
        float bv = C * sinf(H_rad);
        if (type == 0x00e3) /* oklch */
            oklab_to_rgb(L, av, bv, r, g, b);
        else
            oklab_to_rgb(L / 100.0f, av / 125.0f, bv / 125.0f, r, g, b);
        *a = extract_alpha(&lch->a);
        *ok = 1;
        goto clamp;
    }

    /* Default: Named colors / unbekannte Typen → hex-Fallback */
    {
        const lxb_css_value_color_hex_t *hex = &color->u.hex;
        *r = (float)(hex->rgba.r) / 255.0f;
        *g = (float)(hex->rgba.g) / 255.0f;
        *b = (float)(hex->rgba.b) / 255.0f;
        *ok = 1;
    }

clamp:
    *r = clampf(*r, 0, 1);
    *g = clampf(*g, 0, 1);
    *b = clampf(*b, 0, 1);
    *a = clampf(*a, 0, 1);
}

/* ---- CSS Custom Properties (Variables) ---- */

#define MAX_CUSTOM_PROPS 1024
#define MAX_PROP_NAME_LEN 128
#define MAX_PROP_VALUE_LEN 512
#define MAX_VAR_DEPTH 8

typedef struct {
    char name[MAX_PROP_NAME_LEN];
    char value[MAX_PROP_VALUE_LEN];
} CSSCustomProp;

typedef struct {
    CSSCustomProp props[MAX_CUSTOM_PROPS];
    int count;
} CSSCustomPropMap;

static int contains_var(const char *s, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (s[i] == 'v' && s[i+1] == 'a' && s[i+2] == 'r' && s[i+3] == '(')
            return 1;
    }
    return 0;
}

static const char *css_custom_prop_lookup(const CSSCustomPropMap *map,
                                           const char *name, size_t name_len)
{
    for (int i = map->count - 1; i >= 0; i--) {
        if (strlen(map->props[i].name) == name_len &&
            memcmp(map->props[i].name, name, name_len) == 0)
            return map->props[i].value;
    }
    return NULL;
}

static void css_custom_prop_set(CSSCustomPropMap *map,
                                 const char *name, size_t name_len,
                                 const char *value, size_t value_len)
{
    while (name_len > 0 && (name[0] == ' ' || name[0] == '\t'))
        { name++; name_len--; }
    while (name_len > 0 && (name[name_len-1] == ' ' || name[name_len-1] == '\t'))
        name_len--;
    while (value_len > 0 && (value[0] == ' ' || value[0] == '\t'))
        { value++; value_len--; }
    while (value_len > 0 && (value[value_len-1] == ' ' || value[value_len-1] == '\t'))
        value_len--;

    if (name_len == 0 || name_len >= MAX_PROP_NAME_LEN ||
        value_len == 0 || value_len >= MAX_PROP_VALUE_LEN)
        return;

    for (int i = 0; i < map->count; i++) {
        if (strlen(map->props[i].name) == name_len &&
            memcmp(map->props[i].name, name, name_len) == 0) {
            memcpy(map->props[i].value, value, value_len);
            map->props[i].value[value_len] = '\0';
            return;
        }
    }

    if (map->count >= MAX_CUSTOM_PROPS) return;
    memcpy(map->props[map->count].name, name, name_len);
    map->props[map->count].name[name_len] = '\0';
    memcpy(map->props[map->count].value, value, value_len);
    map->props[map->count].value[value_len] = '\0';
    map->count++;
}

static int is_root_selector(const char *sel, size_t len)
{
    for (size_t i = 0; i + 5 <= len; i++) {
        if (memcmp(sel + i, ":root", 5) == 0) return 1;
    }
    for (size_t i = 0; i + 4 <= len; i++) {
        if ((sel[i]=='h'||sel[i]=='H') && (sel[i+1]=='t'||sel[i+1]=='T') &&
            (sel[i+2]=='m'||sel[i+2]=='M') && (sel[i+3]=='l'||sel[i+3]=='L')) {
            int before_ok = (i == 0 || !((sel[i-1]>='a'&&sel[i-1]<='z') ||
                            (sel[i-1]>='A'&&sel[i-1]<='Z')));
            int after_ok = (i+4 >= len || !((sel[i+4]>='a'&&sel[i+4]<='z') ||
                           (sel[i+4]>='A'&&sel[i+4]<='Z')));
            if (before_ok && after_ok) return 1;
        }
    }
    return 0;
}

static void css_extract_custom_props(const char *css, size_t len,
                                      CSSCustomPropMap *map)
{
    size_t i = 0;
    size_t sel_start = 0;

    while (i < len) {
        /* CSS-Kommentare */
        if (i + 1 < len && css[i] == '/' && css[i+1] == '*') {
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
            if (i + 1 < len) i += 2;
            continue;
        }
        /* Strings */
        if (css[i] == '"' || css[i] == '\'') {
            char q = css[i++];
            while (i < len && css[i] != q) {
                if (css[i] == '\\' && i + 1 < len) i++;
                i++;
            }
            if (i < len) i++;
            continue;
        }
        /* ';' und '}' → neuer Selektor beginnt */
        if (css[i] == '}' || css[i] == ';') {
            i++;
            sel_start = i;
            continue;
        }
        /* '{' → Selektor pruefen */
        if (css[i] == '{') {
            /* @-Regeln: nur den aeusseren '{' ueberspringen */
            size_t tmp = sel_start;
            while (tmp < i && (css[tmp]==' '||css[tmp]=='\t'||
                               css[tmp]=='\n'||css[tmp]=='\r')) tmp++;
            if (tmp < i && css[tmp] == '@') {
                /* Dark-Mode @media-Bloecke komplett ueberspringen */
                int is_dark = 0;
                for (size_t k = tmp; k + 4 <= i; k++) {
                    if (memcmp(css + k, "dark", 4) == 0)
                        { is_dark = 1; break; }
                }
                if (is_dark) {
                    i++;
                    int depth = 1;
                    while (i < len && depth > 0) {
                        if (css[i] == '{') depth++;
                        else if (css[i] == '}') depth--;
                        i++;
                    }
                    sel_start = i;
                } else {
                    i++;
                    sel_start = i;
                }
                continue;
            }

            int is_root = is_root_selector(css + sel_start, i - sel_start);
            i++;

            if (!is_root) {
                int depth = 1;
                while (i < len && depth > 0) {
                    if (i+1 < len && css[i]=='/' && css[i+1]=='*') {
                        i += 2;
                        while (i+1 < len && !(css[i]=='*'&&css[i+1]=='/')) i++;
                        if (i+1 < len) i += 2;
                        continue;
                    }
                    if (css[i] == '{') depth++;
                    else if (css[i] == '}') depth--;
                    i++;
                }
                sel_start = i;
                continue;
            }

            /* :root-Block: Custom Properties extrahieren */
            while (i < len && css[i] != '}') {
                if (i+1 < len && css[i]=='/' && css[i+1]=='*') {
                    i += 2;
                    while (i+1 < len && !(css[i]=='*'&&css[i+1]=='/')) i++;
                    if (i+1 < len) i += 2;
                    continue;
                }
                if (css[i]==' '||css[i]=='\t'||css[i]=='\n'||css[i]=='\r') {
                    i++; continue;
                }

                if (i + 2 < len && css[i] == '-' && css[i+1] == '-') {
                    size_t name_start = i;
                    size_t j = i + 2;
                    while (j < len && css[j] != ':' && css[j] != '}' &&
                           css[j] != ';') j++;

                    if (j < len && css[j] == ':') {
                        size_t name_end = j;
                        j++;
                        size_t val_start = j;
                        int paren = 0;
                        while (j < len) {
                            if (css[j] == '(') paren++;
                            else if (css[j] == ')') { if (paren > 0) paren--; }
                            else if (paren == 0 && (css[j]==';'||css[j]=='}'))
                                break;
                            j++;
                        }
                        css_custom_prop_set(map, css + name_start,
                            name_end - name_start, css + val_start,
                            j - val_start);
                        if (j < len && css[j] == ';') j++;
                        i = j;
                        continue;
                    }
                }

                /* Nicht-Custom: bis ';' oder '}' ueberspringen */
                while (i < len && css[i] != ';' && css[i] != '}') i++;
                if (i < len && css[i] == ';') i++;
            }
            if (i < len && css[i] == '}') i++;
            sel_start = i;
            continue;
        }
        i++;
    }
}

/* Entfernt @media-Bloecke die "dark" enthalten aus dem CSS-Text.
   Verhindert dass lexbor @media (prefers-color-scheme:dark) auswertet. */
static char *css_strip_dark_media(const char *css, size_t len, size_t *out_len)
{
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0;
    size_t i = 0;

    while (i < len) {
        /* @media ... { suchen */
        if (css[i] == '@' && i + 6 < len &&
            memcmp(css + i, "@media", 6) == 0) {
            /* Bis zum '{' scannen */
            size_t j = i + 6;
            while (j < len && css[j] != '{') j++;
            if (j >= len) { out[o++] = css[i++]; continue; }

            /* Pruefen ob "dark" im @media-Header */
            int is_dark = 0;
            for (size_t k = i; k + 4 <= j; k++) {
                if (memcmp(css + k, "dark", 4) == 0)
                    { is_dark = 1; break; }
            }

            if (is_dark) {
                /* Gesamten Block ueberspringen */
                j++; /* '{' */
                int depth = 1;
                while (j < len && depth > 0) {
                    if (css[j] == '{') depth++;
                    else if (css[j] == '}') depth--;
                    j++;
                }
                i = j;
                continue;
            }
        }
        out[o++] = css[i++];
    }

    out[o] = '\0';
    *out_len = o;
    return out;
}

static char *css_substitute_vars(const char *css, size_t css_len,
                                  const CSSCustomPropMap *map, int depth)
{
    if (depth > MAX_VAR_DEPTH) return NULL;

    size_t out_cap = css_len * 2 + 256;
    char *out = malloc(out_cap);
    if (!out) return NULL;
    size_t out_len = 0;

#define ENSURE_CAP(need) do { \
    while (out_len + (need) >= out_cap) { \
        out_cap *= 2; \
        char *_t = realloc(out, out_cap); \
        if (!_t) { free(out); return NULL; } \
        out = _t; \
    } \
} while(0)

    size_t i = 0;
    while (i < css_len) {
        if (i + 4 <= css_len && memcmp(css + i, "var(", 4) == 0) {
            i += 4;
            while (i < css_len && (css[i]==' '||css[i]=='\t')) i++;

            /* Finde schliessende ')' und optionales ',' */
            size_t name_start = i;
            int paren = 1;
            size_t comma_pos = 0;
            int found_comma = 0;
            size_t j = i;

            while (j < css_len && paren > 0) {
                if (css[j] == '(') paren++;
                else if (css[j] == ')') { paren--; if (paren == 0) break; }
                else if (paren == 1 && css[j] == ',' && !found_comma) {
                    comma_pos = j;
                    found_comma = 1;
                }
                j++;
            }
            size_t close_paren = j;

            /* Name extrahieren */
            size_t name_end = found_comma ? comma_pos : close_paren;
            while (name_end > name_start &&
                   (css[name_end-1]==' '||css[name_end-1]=='\t')) name_end--;

            const char *val = css_custom_prop_lookup(map,
                css + name_start, name_end - name_start);

            if (val) {
                size_t val_len = strlen(val);
                char *resolved_val = NULL;
                const char *to_copy = val;
                size_t to_copy_len = val_len;

                if (contains_var(val, val_len) && depth < MAX_VAR_DEPTH) {
                    resolved_val = css_substitute_vars(val, val_len,
                                                       map, depth + 1);
                    if (resolved_val) {
                        to_copy = resolved_val;
                        to_copy_len = strlen(resolved_val);
                    }
                }
                ENSURE_CAP(to_copy_len);
                memcpy(out + out_len, to_copy, to_copy_len);
                out_len += to_copy_len;
                free(resolved_val);
            } else if (found_comma) {
                /* Fallback verwenden */
                size_t fb_start = comma_pos + 1;
                while (fb_start < close_paren &&
                       (css[fb_start]==' '||css[fb_start]=='\t')) fb_start++;
                size_t fb_len = close_paren - fb_start;

                if (fb_len > 0) {
                    char *resolved_fb = css_substitute_vars(
                        css + fb_start, fb_len, map, depth + 1);
                    if (resolved_fb) {
                        size_t rlen = strlen(resolved_fb);
                        ENSURE_CAP(rlen);
                        memcpy(out + out_len, resolved_fb, rlen);
                        out_len += rlen;
                        free(resolved_fb);
                    }
                }
            }
            /* kein Wert + kein Fallback → nichts einfuegen */

            i = close_paren;
            if (i < css_len) i++; /* ')' */
            continue;
        }

        ENSURE_CAP(1);
        out[out_len++] = css[i++];
    }

#undef ENSURE_CAP

    out[out_len] = '\0';
    return out;
}

static void css_resolve_nested_vars(CSSCustomPropMap *map)
{
    for (int pass = 0; pass < MAX_VAR_DEPTH; pass++) {
        int changed = 0;
        for (int i = 0; i < map->count; i++) {
            if (!contains_var(map->props[i].value,
                              strlen(map->props[i].value)))
                continue;
            char *resolved = css_substitute_vars(map->props[i].value,
                strlen(map->props[i].value), map, 0);
            if (resolved && strcmp(resolved, map->props[i].value) != 0) {
                size_t rlen = strlen(resolved);
                if (rlen < MAX_PROP_VALUE_LEN) {
                    memcpy(map->props[i].value, resolved, rlen + 1);
                    changed = 1;
                }
            }
            free(resolved);
        }
        if (!changed) break;
    }
}

static void css_resolve_inline_vars(lxb_dom_node_t *node,
                                     const CSSCustomPropMap *map)
{
    if (!node || map->count == 0) return;

    lxb_dom_node_t *child = node->first_child;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(child);
            size_t style_len = 0;
            const lxb_char_t *style = lxb_dom_element_get_attribute(
                el, (const lxb_char_t *)"style", 5, &style_len);

            if (style && style_len > 0 &&
                contains_var((const char *)style, style_len)) {
                char *resolved = css_substitute_vars(
                    (const char *)style, style_len, map, 0);
                if (resolved) {
                    lxb_dom_element_set_attribute(el,
                        (const lxb_char_t *)"style", 5,
                        (const lxb_char_t *)resolved, strlen(resolved));
                    free(resolved);
                }
            }
            css_resolve_inline_vars(child, map);
        }
        child = child->next;
    }
}

/* ---- CSS-Engine Apply ---- */

/* Minimaler User-Agent-Stylesheet (Browser-Defaults) */
static const char *ua_stylesheet =
    "html, body { background-color: #ffffff; color: #000000; }\n"
    "a { color: #0645ad; }\n"
    "a:visited { color: #0b0080; }\n"
    "table { border-collapse: collapse; }\n"
    "th { font-weight: bold; text-align: center; }\n"
    "sub, sup { font-size: 75%; }\n"
    "mark { background-color: #ffff00; color: #000000; }\n"
    "ins { text-decoration: underline; }\n"
    "del, s { text-decoration: line-through; }\n";

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

    /* User-Agent-Stylesheet zuerst anwenden */
    {
        lxb_css_stylesheet_t *ua = lxb_css_stylesheet_create(
            doc->dom_document.css->memory);
        if (ua) {
            status = lxb_css_stylesheet_parse(ua, doc->dom_document.css->parser,
                (const lxb_char_t *)ua_stylesheet, strlen(ua_stylesheet));
            if (status == LXB_STATUS_OK)
                lxb_dom_document_stylesheet_apply(&doc->dom_document, ua);
        }
    }

    /* Alle CSS-Resources parsen und anwenden */
    int sheet_count = 0;

    if (resources) {
        /* Custom Properties aus allen Stylesheets sammeln */
        static CSSCustomPropMap var_map;
        memset(&var_map, 0, sizeof(var_map));

        for (int i = 0; i < resources->count; i++) {
            if (resources->entries[i].type != RES_TYPE_CSS) continue;
            if (!resources->entries[i].data) continue;
            css_extract_custom_props(resources->entries[i].data,
                                     resources->entries[i].data_len, &var_map);
        }

        /* Verschachtelte var()-Referenzen in der Map aufloesen */
        if (var_map.count > 0)
            css_resolve_nested_vars(&var_map);

        if (var_map.count > 0)
            printf("  CSS: %d custom properties found\n", var_map.count);

        /* Stylesheets parsen — var() vorher substituieren */
        for (int i = 0; i < resources->count; i++) {
            if (resources->entries[i].type != RES_TYPE_CSS) continue;
            if (!resources->entries[i].data) continue;

            const char *css_text = resources->entries[i].data;
            size_t css_len = resources->entries[i].data_len;
            char *resolved_css = NULL;

            if (var_map.count > 0 && contains_var(css_text, css_len)) {
                resolved_css = css_substitute_vars(css_text, css_len,
                                                    &var_map, 0);
            }

            const char *parse_text = resolved_css ? resolved_css : css_text;
            size_t parse_len = resolved_css ? strlen(resolved_css) : css_len;

            /* @media dark Bloecke entfernen, damit lexbor sie nicht auswertet */
            char *stripped = css_strip_dark_media(parse_text, parse_len, &parse_len);
            if (stripped) {
                parse_text = stripped;
            }

            lxb_css_stylesheet_t *sst = lxb_css_stylesheet_create(
                doc->dom_document.css->memory);
            if (!sst) { free(resolved_css); free(stripped); continue; }

            status = lxb_css_stylesheet_parse(sst, doc->dom_document.css->parser,
                (const lxb_char_t *)parse_text, parse_len);
            free(resolved_css);
            free(stripped);

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

        /* Inline-Styles mit var() aufloesen */
        if (var_map.count > 0) {
            lxb_dom_node_t *root = lxb_dom_interface_node(
                lxb_dom_document_root(&doc->dom_document));
            if (root)
                css_resolve_inline_vars(root, &var_map);
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
    out->width_pct = -1.0f;
    out->max_width = -1.0f;
    out->max_width_pct = -1.0f;
    out->color_a = 1.0f;
    out->border_a = 1.0f;
    out->opacity = 1.0f;
    out->pos_top = -1.0f;
    out->pos_left = -1.0f;
    out->pos_right = -1.0f;
    out->pos_bottom = -1.0f;

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
        color_to_rgba(color, &out->color_r, &out->color_g, &out->color_b,
                      &out->color_a, &out->has_color);
    }

    /* Background-color */
    const lxb_css_value_color_t *bg =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BACKGROUND_COLOR);
    if (bg) {
        color_to_rgba(bg, &out->bg_r, &out->bg_g, &out->bg_b, &out->bg_a,
                      &out->has_bg);
        /* transparent (a=0) gilt nicht als gesetzter Hintergrund */
        if (out->has_bg && out->bg_a <= 0.0f)
            out->has_bg = 0;
    }

    /* Font-size */
    const lxb_css_value_length_percentage_t *fsp =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FONT_SIZE);
    if (fsp) {
        float v = length_perc_to_px(fsp, fs, fs);
        if (v > 0) { out->font_size = v; out->has_font_size = 1; fs = v; }
    }

    /* Font-family */
    const lxb_css_property_font_family_t *ff =
        lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_FONT_FAMILY);
    if (ff && ff->first) {
        const lxb_css_property_family_name_t *fn = ff->first;
        while (fn) {
            if (fn->generic) {
                /* Generische Familie: serif, sans-serif, monospace, etc. */
                const char *gname = NULL;
                switch ((unsigned)fn->u.type) {
                    case LXB_CSS_FONT_FAMILY_SERIF:
                    case LXB_CSS_FONT_FAMILY_UI_SERIF:
                        gname = "serif"; break;
                    case LXB_CSS_FONT_FAMILY_SANS_SERIF:
                    case LXB_CSS_FONT_FAMILY_UI_SANS_SERIF:
                    case LXB_CSS_FONT_FAMILY_SYSTEM_UI:
                        gname = "sans-serif"; break;
                    case LXB_CSS_FONT_FAMILY_MONOSPACE:
                    case LXB_CSS_FONT_FAMILY_UI_MONOSPACE:
                        gname = "monospace"; break;
                    default: break;
                }
                if (gname) {
                    uint8_t fid = renderer_resolve_font_family(gname);
                    out->font_id = fid;
                    out->has_font_id = 1;
                    break;
                }
            } else {
                /* Benannter Font: in Registry suchen */
                if (fn->u.str.data && fn->u.str.length > 0) {
                    char name[64];
                    size_t nlen = fn->u.str.length;
                    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                    memcpy(name, fn->u.str.data, nlen);
                    name[nlen] = '\0';
                    uint8_t fid = renderer_resolve_font_family(name);
                    if (fid != 0) {
                        out->font_id = fid;
                        out->has_font_id = 1;
                        break;
                    }
                }
            }
            fn = fn->next;
        }
    }

    /* Margins */
    const void *mt = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_TOP);
    if (mt) { float v = length_perc_to_px(mt, 0, fs); if (v >= 0) { out->margin_top = v; out->has_margin_top = 1; } }

    const void *mb = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_BOTTOM);
    if (mb) { float v = length_perc_to_px(mb, 0, fs); if (v >= 0) { out->margin_bottom = v; out->has_margin_bottom = 1; } }

    const void *ml = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_LEFT);
    if (ml) {
        const lxb_css_value_length_percentage_t *mlp = ml;
        if (mlp->type == LXB_CSS_VALUE_AUTO) {
            out->margin_left_auto = 1; out->has_margin_left = 1;
        } else {
            float v = length_perc_to_px(ml, 0, fs);
            if (v >= 0) { out->margin_left = v; out->has_margin_left = 1; }
        }
    }

    const void *mr = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MARGIN_RIGHT);
    if (mr) {
        const lxb_css_value_length_percentage_t *mrp = mr;
        if (mrp->type == LXB_CSS_VALUE_AUTO) {
            out->margin_right_auto = 1; out->has_margin_right = 1;
        } else {
            float v = length_perc_to_px(mr, 0, fs);
            if (v >= 0) { out->margin_right = v; out->has_margin_right = 1; }
        }
    }

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
        const lxb_css_value_length_percentage_t *wlp = wp;
        if (wlp->type == LXB_CSS_VALUE__PERCENTAGE) {
            out->width_pct = (float)wlp->u.percentage.num;
            out->has_width = 1;
        } else {
            float v = length_perc_to_px(wp, 0, fs);
            if (v >= 0) { out->width = v; out->has_width = 1; }
        }
    }

    const void *hp = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_HEIGHT);
    if (hp) {
        float v = length_perc_to_px(hp, 0, fs);
        if (v >= 0) { out->height = v; out->has_height = 1; }
    }

    const void *mwp = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_MAX_WIDTH);
    if (mwp) {
        const lxb_css_value_length_percentage_t *mwlp = mwp;
        if (mwlp->type == LXB_CSS_VALUE__PERCENTAGE) {
            out->max_width_pct = (float)mwlp->u.percentage.num;
            out->has_max_width = 1;
        } else {
            float v = length_perc_to_px(mwp, 0, fs);
            if (v >= 0) { out->max_width = v; out->has_max_width = 1; }
        }
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
                out->border_a = 1.0f;
                int ok = 0;
                color_to_rgba(&bt->color, &out->border_r, &out->border_g,
                              &out->border_b, &out->border_a, &ok);
            }
        }
    }
    /* Fallback: border-top-color separat */
    if (!out->has_border) {
        const lxb_css_value_color_t *btc =
            lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BORDER_TOP_COLOR);
        if (btc) {
            int ok = 0;
            color_to_rgba(btc, &out->border_r, &out->border_g, &out->border_b,
                          &out->border_a, &ok);
            if (ok) { out->has_border = 1; out->border_width = 1.0f; }
        }
    }

    /* ---- Opacity ---- */

    const void *opv = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_OPACITY);
    if (opv) {
        const lxb_css_value_number_type_t *n = opv;
        if (n->type == LXB_CSS_VALUE__NUMBER) {
            out->opacity = clampf((float)n->number.num, 0, 1);
            out->has_opacity = 1;
        }
    }

    /* Opacity auf Alpha-Werte anwenden */
    if (out->has_opacity) {
        out->bg_a *= out->opacity;
        out->color_a *= out->opacity;
        out->border_a *= out->opacity;
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

    /* ---- Position ---- */

    const void *pos = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_POSITION);
    if (pos) {
        const lxb_css_value_type_t *t = pos;
        out->has_position = 1;
        if (*t == 0x0146) out->position = 1;       /* relative */
        else if (*t == 0x0147) out->position = 2;   /* absolute */
        else if (*t == 0x0149) out->position = 3;    /* fixed */
    }

    /* Top / Left / Right / Bottom */
    const void *ptop = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_TOP);
    if (ptop) {
        const lxb_css_value_length_percentage_t *lp = ptop;
        if (lp->type != LXB_CSS_VALUE_AUTO) {
            float v = length_perc_to_px(ptop, 0, fs);
            out->pos_top = v; out->has_top = 1;
        }
    }

    const void *pleft = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_LEFT);
    if (pleft) {
        const lxb_css_value_length_percentage_t *lp = pleft;
        if (lp->type != LXB_CSS_VALUE_AUTO) {
            float v = length_perc_to_px(pleft, 0, fs);
            out->pos_left = v; out->has_left = 1;
        }
    }

    const void *pright = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_RIGHT);
    if (pright) {
        const lxb_css_value_length_percentage_t *lp = pright;
        if (lp->type != LXB_CSS_VALUE_AUTO) {
            float v = length_perc_to_px(pright, 0, fs);
            out->pos_right = v; out->has_right = 1;
        }
    }

    const void *pbottom = lxb_dom_element_css_property_by_id(el, LXB_CSS_PROPERTY_BOTTOM);
    if (pbottom) {
        const lxb_css_value_length_percentage_t *lp = pbottom;
        if (lp->type != LXB_CSS_VALUE_AUTO) {
            float v = length_perc_to_px(pbottom, 0, fs);
            out->pos_bottom = v; out->has_bottom = 1;
        }
    }
}
