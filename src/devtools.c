#define _POSIX_C_SOURCE 200809L
#include "devtools.h"
#include "renderer.h"
#include "net_log.h"
#include "scrollbar.h"

#include <stdio.h>
#include <string.h>

#include <GLFW/glfw3.h>

#define PANEL_DEFAULT_WIDTH 320.0f
#define PANEL_MIN_WIDTH     150.0f
#define PANEL_PADDING       8.0f
#define HEADER_HEIGHT       28.0f
#define COL_HEADER_HEIGHT   22.0f
#define ROW_HEIGHT_MULT     1.3f

/* Farben */
#define BG_R  0.14f
#define BG_G  0.14f
#define BG_B  0.16f
#define DIV_R 0.30f
#define DIV_G 0.30f
#define DIV_B 0.35f
#define HDR_R 0.75f
#define HDR_G 0.75f
#define HDR_B 0.80f
#define COL_R 0.50f
#define COL_G 0.50f
#define COL_B 0.55f
#define ROW_ALT_R 0.12f
#define ROW_ALT_G 0.12f
#define ROW_ALT_B 0.14f

static float panel_width;
static float scroll_offset;
static bool  visible = true;
static ScrollbarState dt_scrollbar;

void devtools_init(void)
{
    panel_width   = PANEL_DEFAULT_WIDTH;
    scroll_offset = 0;
    visible       = true;
}

float devtools_get_width(void)
{
    return visible ? panel_width : 0.0f;
}

bool devtools_is_visible(void)
{
    return visible;
}

void devtools_toggle(void)
{
    visible = !visible;
}

void devtools_set_width(float w)
{
    if (w < PANEL_MIN_WIDTH) w = PANEL_MIN_WIDTH;
    panel_width = w;
}

void devtools_scroll(float delta)
{
    float lh = renderer_line_height() * ROW_HEIGHT_MULT;
    NetLog *log = net_log_get();

    scroll_offset += delta * lh * 3.0f;

    float max_scroll = (float)log->count * lh;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    if (scroll_offset < 0) scroll_offset = 0;

    scrollbar_touch(&dt_scrollbar, glfwGetTime());
}

/* Formatiert Bytes als menschenlesbare Groesse in buf. */
static void format_bytes(size_t bytes, char *buf, size_t buflen)
{
    if (bytes < 1024)
        snprintf(buf, buflen, "%zuB", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, buflen, "%.1fKB", (double)bytes / 1024.0);
    else
        snprintf(buf, buflen, "%.1fMB", (double)bytes / (1024.0 * 1024.0));
}

/* Kuerzt text so, dass er in max_w Pixel passt. Schreibt in buf. */
static void truncate_text(const char *text, float max_w, char *buf, size_t buflen)
{
    float dots_w = renderer_text_width("...");

    if (renderer_text_width(text) <= max_w) {
        strncpy(buf, text, buflen - 1);
        buf[buflen - 1] = '\0';
        return;
    }

    size_t len = strlen(text);
    char tmp[2] = {0, 0};
    float w = 0;
    size_t i;

    for (i = 0; i < len && i < buflen - 4; i++) {
        tmp[0] = text[i];
        float cw = renderer_text_width(tmp);
        if (w + cw + dots_w > max_w)
            break;
        w += cw;
        buf[i] = text[i];
    }
    buf[i]     = '.';
    buf[i + 1] = '.';
    buf[i + 2] = '.';
    buf[i + 3] = '\0';
}

/* Waehlt Farbe nach HTTP-Statuscode. */
static void status_color(long code, int failed, float *r, float *g, float *b)
{
    if (failed || code == 0) {
        *r = 0.90f; *g = 0.30f; *b = 0.30f;
    } else if (code >= 200 && code < 300) {
        *r = 0.40f; *g = 0.80f; *b = 0.40f;
    } else if (code >= 300 && code < 400) {
        *r = 0.80f; *g = 0.80f; *b = 0.30f;
    } else {
        *r = 0.90f; *g = 0.30f; *b = 0.30f;
    }
}

void devtools_draw(float panel_x, float y_offset, int fb_w, int fb_h)
{
    float lh = renderer_line_height();
    float row_h = lh * ROW_HEIGHT_MULT;
    float panel_h = (float)fb_h - y_offset;
    NetLog *log = net_log_get();

    /* Hintergrund */
    renderer_draw_rect(panel_x, y_offset, panel_width, panel_h,
                       BG_R, BG_G, BG_B, 1.0f, fb_w, fb_h);

    /* Divider */
    renderer_draw_rect(panel_x, y_offset, 2.0f, panel_h,
                       DIV_R, DIV_G, DIV_B, 1.0f, fb_w, fb_h);

    float cx = panel_x + PANEL_PADDING;
    float cy = y_offset + 4.0f;

    /* Header: "Network" */
    renderer_draw_text("Network", cx, cy, HDR_R, HDR_G, HDR_B, fb_w, fb_h);
    cy += HEADER_HEIGHT;

    /* Trennlinie unter Header */
    renderer_draw_rect(panel_x, cy - 2.0f, panel_width, 1.0f,
                       DIV_R, DIV_G, DIV_B, 1.0f, fb_w, fb_h);

    /* Spaltenbreiten berechnen */
    float avail_w = panel_width - 2.0f * PANEL_PADDING;
    float status_w = 45.0f;
    float size_w   = 55.0f;
    float time_w   = 55.0f;
    float url_w    = avail_w - status_w - size_w - time_w;
    if (url_w < 40.0f) url_w = 40.0f;

    float col_status_x = cx + url_w;
    float col_size_x   = col_status_x + status_w;
    float col_time_x   = col_size_x + size_w;

    /* Spaltenüberschriften */
    renderer_draw_text("URL",    cx,           cy, COL_R, COL_G, COL_B, fb_w, fb_h);
    renderer_draw_text("Status", col_status_x, cy, COL_R, COL_G, COL_B, fb_w, fb_h);
    renderer_draw_text("Size",   col_size_x,   cy, COL_R, COL_G, COL_B, fb_w, fb_h);
    renderer_draw_text("Time",   col_time_x,   cy, COL_R, COL_G, COL_B, fb_w, fb_h);
    cy += COL_HEADER_HEIGHT;

    /* Trennlinie unter Spaltenüberschriften */
    renderer_draw_rect(panel_x, cy - 2.0f, panel_width, 1.0f,
                       DIV_R, DIV_G, DIV_B, 0.5f, fb_w, fb_h);

    /* Zeilen-Bereich */
    float rows_top = cy;
    float rows_h = (float)fb_h - rows_top;
    int visible_rows = (int)(rows_h / row_h) + 2;

    int first = (int)(scroll_offset / row_h);
    if (first < 0) first = 0;
    int last = first + visible_rows;
    if (last > log->count) last = log->count;

    for (int i = first; i < last; i++) {
        float ry = rows_top + (float)i * row_h - scroll_offset;

        if (ry + row_h < rows_top || ry > (float)fb_h)
            continue;

        /* Alternating row background */
        if (i % 2 == 0) {
            renderer_draw_rect(panel_x + 2.0f, ry, panel_width - 2.0f, row_h,
                               ROW_ALT_R, ROW_ALT_G, ROW_ALT_B, 1.0f, fb_w, fb_h);
        }

        NetLogEntry *e = &log->entries[i];

        /* URL (gekuerzt) */
        char url_buf[256];
        truncate_text(e->url, url_w - 4.0f, url_buf, sizeof(url_buf));

        float text_y = ry + (row_h - lh) * 0.5f;

        float sr, sg, sb;
        status_color(e->status_code, e->failed, &sr, &sg, &sb);

        renderer_draw_text(url_buf, cx, text_y, 0.75f, 0.75f, 0.78f, fb_w, fb_h);

        /* Redirect-Ziel anzeigen */
        if (e->redirect_url[0]) {
            char redir_buf[260];
            snprintf(redir_buf, sizeof(redir_buf), "-> %.253s", e->redirect_url);
            char redir_trunc[256];
            truncate_text(redir_buf, url_w - 4.0f, redir_trunc, sizeof(redir_trunc));
            renderer_draw_text(redir_trunc, cx, text_y + lh * 0.55f,
                               0.50f, 0.50f, 0.55f, fb_w, fb_h);
        }

        /* Status */
        char status_buf[16];
        if (e->failed)
            snprintf(status_buf, sizeof(status_buf), "ERR");
        else
            snprintf(status_buf, sizeof(status_buf), "%ld", e->status_code);
        renderer_draw_text(status_buf, col_status_x, text_y, sr, sg, sb, fb_w, fb_h);

        /* Size */
        char size_buf[32];
        format_bytes(e->response_bytes, size_buf, sizeof(size_buf));
        renderer_draw_text(size_buf, col_size_x, text_y, 0.60f, 0.60f, 0.63f, fb_w, fb_h);

        /* Time */
        char time_buf[32];
        if (e->elapsed_ms < 1000.0)
            snprintf(time_buf, sizeof(time_buf), "%.0fms", e->elapsed_ms);
        else
            snprintf(time_buf, sizeof(time_buf), "%.1fs", e->elapsed_ms / 1000.0);
        renderer_draw_text(time_buf, col_time_x, text_y, 0.60f, 0.60f, 0.63f, fb_w, fb_h);
    }

    /* Leerer Zustand */
    if (log->count == 0) {
        renderer_draw_text("No requests yet.", cx, rows_top + 4.0f,
                           COL_R, COL_G, COL_B, fb_w, fb_h);
    }

    /* Scrollbar */
    dt_scrollbar.scroll_offset   = scroll_offset;
    dt_scrollbar.content_height  = (float)log->count * row_h;
    dt_scrollbar.viewport_height = rows_h;
    dt_scrollbar.x               = panel_x + panel_width;
    dt_scrollbar.y               = rows_top;
    scrollbar_draw(&dt_scrollbar, fb_w, fb_h);
}

ScrollbarState *devtools_get_scrollbar(void)
{
    return &dt_scrollbar;
}

void devtools_set_scroll(float offset)
{
    scroll_offset = offset;
    NetLog *log = net_log_get();
    float lh = renderer_line_height() * ROW_HEIGHT_MULT;
    float max_scroll = (float)log->count * lh;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    if (scroll_offset < 0) scroll_offset = 0;
}

void devtools_free(void)
{
    scroll_offset = 0;
}
