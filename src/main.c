#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "js_engine.h"
#include "renderer.h"
#include "url_bar.h"
#include "http.h"
#include "resource_fetch.h"
#include "content_view.h"
#include "net_log.h"
#include "devtools.h"
#include "image_cache.h"
#include "fetch_manager.h"
#include "css_engine.h"
#include <lexbor/html/html.h>

#define WINDOW_TITLE  "Explorer Browser"
#define WINDOW_WIDTH  1024
#define WINDOW_HEIGHT 768

#define FONT_PATH "/usr/share/fonts/noto/NotoSans-Regular.ttf"
#define FONT_SIZE 16

#define DIVIDER_HIT_ZONE 6.0f
#define MULTI_CLICK_TIME 0.4

static URLBar             url_bar;
static JSEngine          *js;
static ResourceCollection resources;
static PendingQueue       pending;
static GLFWcursor        *cursor_hresize;
static GLFWcursor        *cursor_hand;
static int                dragging_divider;
static int                dragging_scrollbar; /* 1=content, 2=devtools */
static char               last_url[2048];
static const char        *hovered_link;  /* zeigt in LayoutBox, nicht freigeben */
static int                css_needs_apply; /* 1 wenn neue CSS-Sheets geladen, warten auf Fetch-Ende */

/* Multiclick-Tracking */
static double last_click_time;
static int    click_count;

static void update_content_width(int win_w)
{
    float dt_w = devtools_get_width();
    float content_w = (float)win_w - dt_w;
    content_view_set_max_width(content_w);
}

static void navigate(const char *url)
{
    printf("Fetching: %s\n", url);

    /* URL fuer Reload merken */
    strncpy(last_url, url, sizeof(last_url) - 1);
    last_url[sizeof(last_url) - 1] = '\0';

    net_log_clear();
    fetch_manager_abort();
    resource_collection_free(&resources);
    pending_queue_free(&pending);
    image_cache_clear();
    css_needs_apply = 0;

    HTTPResponse resp;
    int rc = http_get(url, &resp);
    if (rc != 0) {
        const char *err;
        if (rc == HTTP_ERR_TOO_MANY_REDIRECTS)
            err = "<html><body><h1>ERR_TOO_MANY_REDIRECTS</h1>"
                  "<p>Too many redirects (limit: 20).</p></body></html>";
        else
            err = "<html><body><p>Error: Could not fetch URL.</p></body></html>";
        content_view_set_html(err, strlen(err));
        return;
    }

    printf("HTML: HTTP %ld, %zu bytes\n", resp.status_code, resp.len);

    content_view_set_html(resp.data, resp.len);
    resource_extract_urls(resp.data, resp.len, url, &pending);
    fetch_manager_start(&pending);
    http_response_free(&resp);
}

static void reload(void)
{
    if (last_url[0])
        navigate(last_url);
}

static void stop_loading(void)
{
    pending_queue_free(&pending);
    printf("Loading stopped.\n");
}

static void error_callback(int error, const char *description)
{
    (void)error;
    fprintf(stderr, "GLFW Error: %s\n", description);
}

static void key_callback(GLFWwindow *window, int key, int scancode,
                          int action, int mods)
{
    (void)scancode;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    int ctrl  = mods & GLFW_MOD_CONTROL;
    int shift = mods & GLFW_MOD_SHIFT;

    /* ---- Globale Shortcuts (immer aktiv) ---- */

    /* Escape: URL-Bar defokussieren, oder Laden stoppen, oder App schließen */
    if (key == GLFW_KEY_ESCAPE) {
        if (url_bar.focused) {
            url_bar.focused = false;
        } else if (pending.next < pending.count) {
            stop_loading();
        } else {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        return;
    }

    /* F12 / Ctrl+Shift+I: DevTools togglen */
    if (key == GLFW_KEY_F12 ||
        (key == GLFW_KEY_I && ctrl && shift)) {
        devtools_toggle();
        int ww, wh;
        glfwGetWindowSize(window, &ww, &wh);
        update_content_width(ww);
        return;
    }

    /* F5 / Ctrl+R: Reload */
    if (key == GLFW_KEY_F5 || (key == GLFW_KEY_R && ctrl)) {
        reload();
        return;
    }

    /* Ctrl+L: URL-Bar fokussieren + alles selektieren */
    if (key == GLFW_KEY_L && ctrl) {
        url_bar_focus_select_all(&url_bar);
        return;
    }

    /* Ctrl+Q: Beenden */
    if (key == GLFW_KEY_Q && ctrl) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    /* ---- Seiten-Scroll (nur wenn URL-Bar nicht fokussiert) ---- */

    if (!url_bar.focused) {
        float content_y = 8.0f + url_bar.height + 8.0f;
        int ww, wh;
        glfwGetWindowSize(window, &ww, &wh);
        float viewport_h = (float)wh - content_y;

        switch (key) {
        case GLFW_KEY_SPACE:
            if (shift)
                content_view_scroll_page(viewport_h, -1);
            else
                content_view_scroll_page(viewport_h, 1);
            return;
        case GLFW_KEY_PAGE_DOWN:
            content_view_scroll_page(viewport_h, 1);
            return;
        case GLFW_KEY_PAGE_UP:
            content_view_scroll_page(viewport_h, -1);
            return;
        case GLFW_KEY_HOME:
            content_view_scroll_to(0);
            return;
        case GLFW_KEY_END:
            content_view_scroll_to(1);
            return;
        case GLFW_KEY_DOWN:
            content_view_scroll(1.0f);
            return;
        case GLFW_KEY_UP:
            content_view_scroll(-1.0f);
            return;
        default:
            break;
        }
    }

    /* ---- URL-Bar Input ---- */

    if (url_bar_key_input(&url_bar, key, action, mods, window)) {
        navigate(url_bar.text);
    }
}

static void char_callback(GLFWwindow *window, unsigned int codepoint)
{
    (void)window;
    url_bar_char_input(&url_bar, codepoint);
}

static void mouse_button_callback(GLFWwindow *window, int button,
                                   int action, int mods)
{
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    if (action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        int ww, wh;
        glfwGetWindowSize(window, &ww, &wh);

        /* Multiclick erkennen */
        double now = glfwGetTime();
        if (now - last_click_time < MULTI_CLICK_TIME)
            click_count++;
        else
            click_count = 1;
        last_click_time = now;
        if (click_count > 3) click_count = 1;

        /* Scrollbar-Klick pruefen */
        ScrollbarState *cv_sb = content_view_get_scrollbar();
        ScrollbarState *dt_sb = devtools_get_scrollbar();

        if (scrollbar_thumb_hit(cv_sb, (float)mx, (float)my)) {
            float tx, ty, tw, th;
            scrollbar_get_thumb(cv_sb, &tx, &ty, &tw, &th);
            cv_sb->dragging = true;
            cv_sb->drag_offset = (float)my - ty;
            dragging_scrollbar = 1;
            scrollbar_touch(cv_sb, glfwGetTime());
            return;
        }
        if (scrollbar_hit_test(cv_sb, (float)mx, (float)my)) {
            float offset = scrollbar_click_track(cv_sb, (float)my);
            content_view_set_scroll(offset);
            scrollbar_touch(cv_sb, glfwGetTime());
            return;
        }

        if (devtools_is_visible()) {
            if (scrollbar_thumb_hit(dt_sb, (float)mx, (float)my)) {
                float tx, ty, tw, th;
                scrollbar_get_thumb(dt_sb, &tx, &ty, &tw, &th);
                dt_sb->dragging = true;
                dt_sb->drag_offset = (float)my - ty;
                dragging_scrollbar = 2;
                scrollbar_touch(dt_sb, glfwGetTime());
                return;
            }
            if (scrollbar_hit_test(dt_sb, (float)mx, (float)my)) {
                float offset = scrollbar_click_track(dt_sb, (float)my);
                devtools_set_scroll(offset);
                scrollbar_touch(dt_sb, glfwGetTime());
                return;
            }

            /* DevTools Divider */
            float divider_x = (float)ww - devtools_get_width();
            if (mx >= divider_x - DIVIDER_HIT_ZONE &&
                mx <= divider_x + DIVIDER_HIT_ZONE) {
                dragging_divider = 1;
                return;
            }
        }

        /* Link-Klick */
        if (click_count == 1) {
            const char *href = content_view_link_at((float)mx, (float)my);
            if (href) {
                /* Relative URL aufloesen */
                if (href[0] == '/' && last_url[0]) {
                    /* Root-relativ */
                    char *p = strstr(last_url, "://");
                    if (p) {
                        p += 3;
                        char *slash = strchr(p, '/');
                        size_t origin_len = slash ? (size_t)(slash - last_url) : strlen(last_url);
                        size_t hlen = strlen(href);
                        char *abs = malloc(origin_len + hlen + 1);
                        if (abs) {
                            memcpy(abs, last_url, origin_len);
                            memcpy(abs + origin_len, href, hlen + 1);
                            /* URL in die Bar setzen */
                            strncpy(url_bar.text, abs, URL_BAR_MAX_LEN - 1);
                            url_bar.len = (int)strlen(url_bar.text);
                            url_bar.cursor = url_bar.len;
                            navigate(abs);
                            free(abs);
                            return;
                        }
                    }
                }
                /* Absolut oder relativ */
                strncpy(url_bar.text, href, URL_BAR_MAX_LEN - 1);
                url_bar.len = (int)strlen(url_bar.text);
                url_bar.cursor = url_bar.len;
                navigate(href);
                return;
            }
        }

        url_bar_click(&url_bar, (float)mx, (float)my, ww, click_count);
    } else if (action == GLFW_RELEASE) {
        if (dragging_scrollbar) {
            ScrollbarState *sb = (dragging_scrollbar == 1)
                ? content_view_get_scrollbar()
                : devtools_get_scrollbar();
            sb->dragging = false;
            dragging_scrollbar = 0;
        }
        if (dragging_divider) {
            dragging_divider = 0;
            int ww2, wh2;
            glfwGetWindowSize(window, &ww2, &wh2);
            update_content_width(ww2);
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double mx, double my)
{
    int ww, wh;
    glfwGetWindowSize(window, &ww, &wh);

    /* Scrollbar-Drag */
    if (dragging_scrollbar) {
        double now = glfwGetTime();
        if (dragging_scrollbar == 1) {
            ScrollbarState *sb = content_view_get_scrollbar();
            float offset = scrollbar_drag_to(sb, (float)my);
            content_view_set_scroll(offset);
            scrollbar_touch(sb, now);
        } else {
            ScrollbarState *sb = devtools_get_scrollbar();
            float offset = scrollbar_drag_to(sb, (float)my);
            devtools_set_scroll(offset);
            scrollbar_touch(sb, now);
        }
        glfwPostEmptyEvent();
        return;
    }

    if (dragging_divider) {
        float new_width = (float)ww - (float)mx;
        float max_w = (float)ww * 0.7f;
        if (new_width > max_w) new_width = max_w;
        devtools_set_width(new_width);
        glfwPostEmptyEvent();
        return;
    }

    /* Hover-Erkennung fuer Scrollbars */
    ScrollbarState *cv_sb = content_view_get_scrollbar();
    ScrollbarState *dt_sb = devtools_get_scrollbar();
    bool prev_cv = cv_sb->hovered;
    bool prev_dt = dt_sb->hovered;
    cv_sb->hovered = scrollbar_hit_test(cv_sb, (float)mx, (float)my);
    dt_sb->hovered = devtools_is_visible() &&
                     scrollbar_hit_test(dt_sb, (float)mx, (float)my);

    /* Redraw triggern bei Hover-Aenderung */
    if (cv_sb->hovered != prev_cv || dt_sb->hovered != prev_dt) {
        if (cv_sb->hovered) scrollbar_touch(cv_sb, glfwGetTime());
        if (dt_sb->hovered) scrollbar_touch(dt_sb, glfwGetTime());
        glfwPostEmptyEvent();
    }

    /* Resize-Cursor */
    if (devtools_is_visible()) {
        float divider_x = (float)ww - devtools_get_width();
        if (mx >= divider_x - DIVIDER_HIT_ZONE &&
            mx <= divider_x + DIVIDER_HIT_ZONE) {
            glfwSetCursor(window, cursor_hresize);
            content_view_set_hover(-1, -1);
            return;
        }
    }

    /* Link-Hover */
    content_view_set_hover((float)mx, (float)my);
    hovered_link = content_view_link_at((float)mx, (float)my);
    if (hovered_link) {
        glfwSetCursor(window, cursor_hand);
        glfwPostEmptyEvent();
    } else {
        glfwSetCursor(window, NULL);
    }
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)xoffset;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int ww, wh;
    glfwGetWindowSize(window, &ww, &wh);

    if (devtools_is_visible()) {
        float divider_x = (float)ww - devtools_get_width();
        if ((float)mx >= divider_x) {
            devtools_scroll((float)-yoffset);
            return;
        }
    }
    content_view_scroll((float)-yoffset);
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)window;
    glViewport(0, 0, width, height);
}

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                           WINDOW_TITLE, NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    cursor_hresize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    cursor_hand    = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    int fb_w, fb_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    int win_w, win_h;
    glfwGetWindowSize(window, &win_w, &win_h);
    float dpi_scale = (float)fb_w / (float)win_w;
    printf("DPI scale: %.1f\n", dpi_scale);

    if (renderer_init(FONT_PATH, FONT_SIZE, dpi_scale) != 0) {
        fprintf(stderr, "Failed to init renderer\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    url_bar_init(&url_bar);
    devtools_init();
    fetch_manager_init();

    js = js_engine_create();
    if (!js)
        fprintf(stderr, "Warning: Failed to create JS engine\n");

    update_content_width(win_w);

    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        int ww, wh;
        glfwGetWindowSize(window, &ww, &wh);

        float dt_w = devtools_get_width();
        float content_w = (float)ww - dt_w;

        glClear(GL_COLOR_BUFFER_BIT);

        url_bar_draw(&url_bar, content_w, fw, fh);

        float content_y = 8.0f + url_bar.height + 8.0f;
        content_view_draw(content_y, content_w, fw, fh);

        if (devtools_is_visible()) {
            float panel_x = content_w;
            devtools_draw(panel_x, content_y, fw, fh);
        }

        /* Link-URL Overlay links unten (wie Chrome) */
        if (hovered_link) {
            float lh = renderer_line_height();
            float pad = 6.0f;
            float text_w = renderer_text_width(hovered_link);
            float max_tw = content_w * 0.6f;
            char trunc_buf[512];
            const char *display_url = hovered_link;
            if (text_w > max_tw) {
                size_t len = strlen(hovered_link);
                size_t cut = len;
                float w = 0;
                float dots_w = renderer_text_width("...");
                char tmp[2] = {0, 0};
                for (size_t ci = 0; ci < len && ci < sizeof(trunc_buf) - 4; ci++) {
                    tmp[0] = hovered_link[ci];
                    w += renderer_text_width(tmp);
                    if (w + dots_w > max_tw) { cut = ci; break; }
                }
                memcpy(trunc_buf, hovered_link, cut);
                trunc_buf[cut] = '.'; trunc_buf[cut+1] = '.'; trunc_buf[cut+2] = '.'; trunc_buf[cut+3] = '\0';
                display_url = trunc_buf;
                text_w = renderer_text_width(display_url);
            }
            float box_w = text_w + 2.0f * pad;
            float box_h = lh + 2.0f * pad;
            float box_x = 0;
            float box_y = (float)wh - box_h;
            renderer_draw_rect(box_x, box_y, box_w, box_h,
                               0.16f, 0.16f, 0.18f, 0.92f, fw, fh);
            renderer_draw_rect(box_x, box_y, box_w, 1.0f,
                               0.30f, 0.30f, 0.35f, 0.8f, fw, fh);
            renderer_draw_rect(box_x + box_w, box_y, 1.0f, box_h,
                               0.30f, 0.30f, 0.35f, 0.8f, fw, fh);
            renderer_draw_text(display_url, box_x + pad, box_y + pad,
                               0.70f, 0.72f, 0.75f, fw, fh);
        }

        glfwSwapBuffers(window);

        /* Paralleles Resource-Fetching */
        int prev_count = resources.count;
        int remaining = fetch_manager_poll(&resources);

        /* Neue Ressourcen verarbeiten */
        int need_relayout = 0;
        int got_new_css = 0;
        for (int i = prev_count; i < resources.count; i++) {
            if (resources.entries[i].type == RES_TYPE_IMAGE &&
                resources.entries[i].data) {
                image_cache_add(resources.entries[i].url,
                                resources.entries[i].data,
                                resources.entries[i].data_len);
                need_relayout = 1;
            }
            if (resources.entries[i].type == RES_TYPE_CSS) {
                css_needs_apply = 1;
            }
        }

        /* CSS erst anwenden wenn alle Fetches fertig sind */
        if (css_needs_apply && remaining == 0) {
            lxb_html_document_t *doc = content_view_get_doc();
            if (doc) {
                css_engine_apply(doc, &resources);
                need_relayout = 1;
            }
            css_needs_apply = 0;
        }

        if (need_relayout) {
            content_view_mark_dirty();
        }

        if (remaining > 0) {
            /* Solange Transfers laufen: ~60 FPS fuer responsive UI */
            glfwWaitEventsTimeout(0.016);
        } else {
            glfwWaitEvents();
        }
    }

    js_engine_destroy(js);
    fetch_manager_cleanup();
    resource_collection_free(&resources);
    pending_queue_free(&pending);
    image_cache_clear();
    devtools_free();
    content_view_free();
    renderer_cleanup();

    if (cursor_hresize) glfwDestroyCursor(cursor_hresize);
    if (cursor_hand)    glfwDestroyCursor(cursor_hand);
    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
