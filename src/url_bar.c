#define _POSIX_C_SOURCE 200809L
#include "url_bar.h"
#include "renderer.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <GLFW/glfw3.h>

#define BAR_MARGIN    8.0f
#define BAR_BG_R      0.18f
#define BAR_BG_G      0.18f
#define BAR_BG_B      0.20f
#define BAR_FOCUS_R   0.22f
#define BAR_FOCUS_G   0.22f
#define BAR_FOCUS_B   0.26f
#define TEXT_R        0.90f
#define TEXT_G        0.90f
#define TEXT_B        0.90f
#define CURSOR_R      0.55f
#define CURSOR_G      0.70f
#define CURSOR_B      1.00f
#define PLACEHOLDER_R 0.45f
#define PLACEHOLDER_G 0.45f
#define PLACEHOLDER_B 0.50f
#define SEL_R         0.25f
#define SEL_G         0.40f
#define SEL_B         0.65f

/* ---- Selection-Hilfen ---- */

static int sel_min(const URLBar *bar)
{
    if (bar->sel_start < 0) return bar->cursor;
    return bar->sel_start < bar->cursor ? bar->sel_start : bar->cursor;
}

static int sel_max(const URLBar *bar)
{
    if (bar->sel_start < 0) return bar->cursor;
    return bar->sel_start > bar->cursor ? bar->sel_start : bar->cursor;
}

static bool has_selection(const URLBar *bar)
{
    return bar->sel_start >= 0 && bar->sel_start != bar->cursor;
}

static void clear_selection(URLBar *bar)
{
    bar->sel_start = -1;
}

static void delete_selection(URLBar *bar)
{
    if (!has_selection(bar)) return;
    int lo = sel_min(bar);
    int hi = sel_max(bar);
    memmove(&bar->text[lo], &bar->text[hi],
            (size_t)(bar->len - hi + 1));
    bar->len -= (hi - lo);
    bar->cursor = lo;
    clear_selection(bar);
}

/* ---- Wort-Grenzen finden ---- */

static int word_left(const URLBar *bar, int pos)
{
    if (pos <= 0) return 0;
    pos--;
    /* Whitespace/Sonderzeichen ueberspringen */
    while (pos > 0 && !isalnum((unsigned char)bar->text[pos]))
        pos--;
    /* Wort-Zeichen ueberspringen */
    while (pos > 0 && isalnum((unsigned char)bar->text[pos - 1]))
        pos--;
    return pos;
}

static int word_right(const URLBar *bar, int pos)
{
    if (pos >= bar->len) return bar->len;
    /* Wort-Zeichen ueberspringen */
    while (pos < bar->len && isalnum((unsigned char)bar->text[pos]))
        pos++;
    /* Whitespace/Sonderzeichen ueberspringen */
    while (pos < bar->len && !isalnum((unsigned char)bar->text[pos]))
        pos++;
    return pos;
}

/* ---- Pixel-Position fuer Zeichenindex ---- */

static float char_x_pos(const URLBar *bar, int idx, float text_x)
{
    char tmp[URL_BAR_MAX_LEN];
    int n = idx < bar->len ? idx : bar->len;
    memcpy(tmp, bar->text, (size_t)n);
    tmp[n] = '\0';
    return text_x + renderer_text_width(tmp);
}

/* ---- Public API ---- */

void url_bar_init(URLBar *bar)
{
    memset(bar, 0, sizeof(*bar));
    bar->sel_start = -1;
    bar->padding = 10.0f;
    bar->height  = 36.0f;
    bar->focused = true;
}

void url_bar_draw(const URLBar *bar, float avail_w, int fb_w, int fb_h)
{
    float x = BAR_MARGIN;
    float y = BAR_MARGIN;
    float w = avail_w - 2.0f * BAR_MARGIN;
    float h = bar->height;

    /* Hintergrund */
    if (bar->focused)
        renderer_draw_rect(x, y, w, h, BAR_FOCUS_R, BAR_FOCUS_G, BAR_FOCUS_B, 1.0f, fb_w, fb_h);
    else
        renderer_draw_rect(x, y, w, h, BAR_BG_R, BAR_BG_G, BAR_BG_B, 1.0f, fb_w, fb_h);

    float text_x = x + bar->padding;
    float text_y = y + (h - renderer_line_height()) * 0.5f;

    if (bar->len == 0) {
        renderer_draw_text("Enter URL...", text_x, text_y,
                           PLACEHOLDER_R, PLACEHOLDER_G, PLACEHOLDER_B, fb_w, fb_h);
    } else {
        /* Selection-Highlight zeichnen */
        if (bar->focused && has_selection(bar)) {
            float sx1 = char_x_pos(bar, sel_min(bar), text_x);
            float sx2 = char_x_pos(bar, sel_max(bar), text_x);
            float sy = y + 5.0f;
            float sh = h - 10.0f;
            renderer_draw_rect(sx1, sy, sx2 - sx1, sh,
                               SEL_R, SEL_G, SEL_B, 0.7f, fb_w, fb_h);
        }

        renderer_draw_text(bar->text, text_x, text_y,
                           TEXT_R, TEXT_G, TEXT_B, fb_w, fb_h);
    }

    /* Cursor */
    if (bar->focused) {
        float cx = char_x_pos(bar, bar->cursor, text_x);
        float cy = y + 6.0f;
        float ch = h - 12.0f;
        renderer_draw_rect(cx, cy, 2.0f, ch,
                           CURSOR_R, CURSOR_G, CURSOR_B, 1.0f, fb_w, fb_h);
    }
}

void url_bar_char_input(URLBar *bar, unsigned int codepoint)
{
    if (!bar->focused || codepoint > 127)
        return;

    /* Selection loeschen falls vorhanden */
    if (has_selection(bar))
        delete_selection(bar);

    if (bar->len >= URL_BAR_MAX_LEN - 1)
        return;

    memmove(&bar->text[bar->cursor + 1], &bar->text[bar->cursor],
            (size_t)(bar->len - bar->cursor + 1));
    bar->text[bar->cursor] = (char)codepoint;
    bar->cursor++;
    bar->len++;
    bar->text[bar->len] = '\0';
}

bool url_bar_key_input(URLBar *bar, int key, int action, int mods,
                       GLFWwindow *window)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return false;
    if (!bar->focused)
        return false;

    int shift = mods & GLFW_MOD_SHIFT;
    int ctrl  = mods & GLFW_MOD_CONTROL;

    /* Makro: Selection-Anker setzen oder aufheben */
    #define BEGIN_MOVE() do { \
        if (shift) { if (bar->sel_start < 0) bar->sel_start = bar->cursor; } \
        else { clear_selection(bar); } \
    } while(0)

    switch (key) {

    /* ---- Navigation ---- */

    case GLFW_KEY_LEFT:
        BEGIN_MOVE();
        if (ctrl)
            bar->cursor = word_left(bar, bar->cursor);
        else if (!shift && has_selection(bar))
            bar->cursor = sel_min(bar);
        else if (bar->cursor > 0)
            bar->cursor--;
        if (!shift) clear_selection(bar);
        return false;

    case GLFW_KEY_RIGHT:
        BEGIN_MOVE();
        if (ctrl)
            bar->cursor = word_right(bar, bar->cursor);
        else if (!shift && has_selection(bar))
            bar->cursor = sel_max(bar);
        else if (bar->cursor < bar->len)
            bar->cursor++;
        if (!shift) clear_selection(bar);
        return false;

    case GLFW_KEY_HOME:
        BEGIN_MOVE();
        bar->cursor = 0;
        if (!shift) clear_selection(bar);
        return false;

    case GLFW_KEY_END:
        BEGIN_MOVE();
        bar->cursor = bar->len;
        if (!shift) clear_selection(bar);
        return false;

    /* ---- Loeschen ---- */

    case GLFW_KEY_BACKSPACE:
        if (has_selection(bar)) {
            delete_selection(bar);
        } else if (ctrl) {
            /* Ctrl+Backspace: Wort loeschen */
            int target = word_left(bar, bar->cursor);
            memmove(&bar->text[target], &bar->text[bar->cursor],
                    (size_t)(bar->len - bar->cursor + 1));
            bar->len -= (bar->cursor - target);
            bar->cursor = target;
        } else if (bar->cursor > 0) {
            memmove(&bar->text[bar->cursor - 1], &bar->text[bar->cursor],
                    (size_t)(bar->len - bar->cursor + 1));
            bar->cursor--;
            bar->len--;
        }
        return false;

    case GLFW_KEY_DELETE:
        if (has_selection(bar)) {
            delete_selection(bar);
        } else if (ctrl) {
            /* Ctrl+Delete: Wort nach rechts loeschen */
            int target = word_right(bar, bar->cursor);
            memmove(&bar->text[bar->cursor], &bar->text[target],
                    (size_t)(bar->len - target + 1));
            bar->len -= (target - bar->cursor);
        } else if (bar->cursor < bar->len) {
            memmove(&bar->text[bar->cursor], &bar->text[bar->cursor + 1],
                    (size_t)(bar->len - bar->cursor));
            bar->len--;
        }
        return false;

    /* ---- Clipboard ---- */

    case GLFW_KEY_C:
        if (ctrl && has_selection(bar)) {
            int lo = sel_min(bar), hi = sel_max(bar);
            char tmp[URL_BAR_MAX_LEN];
            memcpy(tmp, &bar->text[lo], (size_t)(hi - lo));
            tmp[hi - lo] = '\0';
            glfwSetClipboardString(window, tmp);
        }
        return false;

    case GLFW_KEY_X:
        if (ctrl && has_selection(bar)) {
            int lo = sel_min(bar), hi = sel_max(bar);
            char tmp[URL_BAR_MAX_LEN];
            memcpy(tmp, &bar->text[lo], (size_t)(hi - lo));
            tmp[hi - lo] = '\0';
            glfwSetClipboardString(window, tmp);
            delete_selection(bar);
        }
        return false;

    case GLFW_KEY_V:
        if (ctrl) {
            const char *clip = glfwGetClipboardString(window);
            if (clip) {
                if (has_selection(bar))
                    delete_selection(bar);
                size_t clen = strlen(clip);
                /* Nur ASCII, Newlines zu Spaces */
                for (size_t i = 0; i < clen && bar->len < URL_BAR_MAX_LEN - 1; i++) {
                    char c = clip[i];
                    if (c == '\n' || c == '\r') c = ' ';
                    if ((unsigned char)c > 127) continue;
                    memmove(&bar->text[bar->cursor + 1], &bar->text[bar->cursor],
                            (size_t)(bar->len - bar->cursor + 1));
                    bar->text[bar->cursor] = c;
                    bar->cursor++;
                    bar->len++;
                }
                bar->text[bar->len] = '\0';
            }
        }
        return false;

    /* ---- Select All ---- */

    case GLFW_KEY_A:
        if (ctrl) {
            bar->sel_start = 0;
            bar->cursor = bar->len;
            return false;
        }
        break;

    /* ---- Enter ---- */

    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        clear_selection(bar);
        return true;

    default:
        break;
    }

    #undef BEGIN_MOVE
    return false;
}

bool url_bar_click(URLBar *bar, float x, float y, int window_width,
                   int click_count)
{
    float bx = BAR_MARGIN;
    float by = BAR_MARGIN;
    float bw = (float)window_width - 2.0f * BAR_MARGIN;
    float bh = bar->height;

    if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
        bar->focused = true;

        if (click_count == 3) {
            /* Dreifachklick: alles selektieren */
            bar->sel_start = 0;
            bar->cursor = bar->len;
        } else if (click_count == 2) {
            /* Doppelklick: Wort selektieren */
            /* Cursor-Position aus x berechnen */
            float text_x = bx + bar->padding;
            int pos = 0;
            float px = text_x;
            char tmp[2] = {0, 0};
            for (int i = 0; i < bar->len; i++) {
                tmp[0] = bar->text[i];
                float cw = renderer_text_width(tmp);
                if (px + cw * 0.5f > x) break;
                px += cw;
                pos++;
            }
            bar->sel_start = word_left(bar, pos + 1);
            bar->cursor = word_right(bar, pos);
        } else {
            /* Einfachklick: Cursor positionieren */
            float text_x = bx + bar->padding;
            int pos = 0;
            float px = text_x;
            char tmp[2] = {0, 0};
            for (int i = 0; i < bar->len; i++) {
                tmp[0] = bar->text[i];
                float cw = renderer_text_width(tmp);
                if (px + cw * 0.5f > x) break;
                px += cw;
                pos++;
            }
            bar->cursor = pos;
            clear_selection(bar);
        }
        return true;
    }

    bar->focused = false;
    return false;
}

void url_bar_focus_select_all(URLBar *bar)
{
    bar->focused = true;
    bar->sel_start = 0;
    bar->cursor = bar->len;
}
