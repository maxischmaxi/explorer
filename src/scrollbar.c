#include "scrollbar.h"
#include "renderer.h"

#define THUMB_MIN_HEIGHT  30.0f
#define FADE_DURATION      1.5f
#define FADE_START         0.8f

#define THUMB_R       0.55f
#define THUMB_G       0.55f
#define THUMB_B       0.58f
#define THUMB_HOVER_R 0.70f
#define THUMB_HOVER_G 0.70f
#define THUMB_HOVER_B 0.73f

static void draw_rounded_rect(float x, float y, float w, float h,
                               float r, float g, float b, float a,
                               int fb_w, int fb_h)
{
    float radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;

    renderer_draw_rect(x, y + radius, w, h - 2.0f * radius,
                       r, g, b, a, fb_w, fb_h);

    float inset = w * 0.15f;
    renderer_draw_rect(x + inset, y, w - 2.0f * inset, radius,
                       r, g, b, a * 0.85f, fb_w, fb_h);
    renderer_draw_rect(x + inset, y + h - radius, w - 2.0f * inset, radius,
                       r, g, b, a * 0.85f, fb_w, fb_h);
}

bool scrollbar_needed(const ScrollbarState *state)
{
    return state->content_height > state->viewport_height + 1.0f;
}

void scrollbar_touch(ScrollbarState *state, double now)
{
    state->last_scroll_time = (float)now;
}

void scrollbar_get_thumb(const ScrollbarState *state,
                         float *out_x, float *out_y,
                         float *out_w, float *out_h)
{
    float track_x = state->x - SCROLLBAR_WIDTH - SCROLLBAR_MARGIN;
    float track_y = state->y;
    float track_h = state->viewport_height;

    float visible_ratio = state->viewport_height / state->content_height;
    float thumb_h = track_h * visible_ratio;
    if (thumb_h < THUMB_MIN_HEIGHT) thumb_h = THUMB_MIN_HEIGHT;
    if (thumb_h > track_h) thumb_h = track_h;

    float max_scroll = state->content_height - state->viewport_height;
    float scroll_ratio = (max_scroll > 0) ? state->scroll_offset / max_scroll : 0;
    if (scroll_ratio < 0) scroll_ratio = 0;
    if (scroll_ratio > 1) scroll_ratio = 1;

    *out_x = track_x;
    *out_y = track_y + scroll_ratio * (track_h - thumb_h);
    *out_w = SCROLLBAR_WIDTH;
    *out_h = thumb_h;
}

bool scrollbar_hit_test(const ScrollbarState *state, float mx, float my)
{
    if (!scrollbar_needed(state)) return false;
    float track_x = state->x - SCROLLBAR_WIDTH - SCROLLBAR_MARGIN;
    float hit_zone = SCROLLBAR_WIDTH + 6.0f;
    return mx >= track_x - 3.0f && mx <= track_x + hit_zone &&
           my >= state->y && my <= state->y + state->viewport_height;
}

bool scrollbar_thumb_hit(const ScrollbarState *state, float mx, float my)
{
    if (!scrollbar_hit_test(state, mx, my)) return false;
    float tx, ty, tw, th;
    scrollbar_get_thumb(state, &tx, &ty, &tw, &th);
    return my >= ty && my <= ty + th;
}

float scrollbar_drag_to(const ScrollbarState *state, float my)
{
    float track_y = state->y;
    float track_h = state->viewport_height;

    float tx, ty, tw, th;
    scrollbar_get_thumb(state, &tx, &ty, &tw, &th);

    float thumb_top = my - state->drag_offset;
    float max_thumb_y = track_h - th;
    if (max_thumb_y <= 0) return 0;

    float ratio = (thumb_top - track_y) / max_thumb_y;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    float max_scroll = state->content_height - state->viewport_height;
    return ratio * max_scroll;
}

float scrollbar_click_track(const ScrollbarState *state, float my)
{
    float track_y = state->y;
    float track_h = state->viewport_height;
    float ratio = (my - track_y) / track_h;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    float max_scroll = state->content_height - state->viewport_height;
    return ratio * max_scroll;
}

void scrollbar_draw(const ScrollbarState *state, int fb_w, int fb_h)
{
    if (!scrollbar_needed(state))
        return;

    extern double glfwGetTime(void);
    float now = (float)glfwGetTime();

    float elapsed = now - state->last_scroll_time;

    /* Immer sichtbar bei Hover oder Drag */
    float alpha;
    if (state->hovered || state->dragging) {
        alpha = 0.70f;
    } else if (elapsed > FADE_DURATION + FADE_START) {
        return;
    } else {
        alpha = 0.55f;
        if (elapsed > FADE_START) {
            float fade_progress = (elapsed - FADE_START) / FADE_DURATION;
            alpha *= (1.0f - fade_progress);
        }
    }
    if (alpha < 0.01f) return;

    float tx, ty, tw, th;
    scrollbar_get_thumb(state, &tx, &ty, &tw, &th);

    float r = THUMB_R, g = THUMB_G, b = THUMB_B;
    if (state->hovered || state->dragging) {
        r = THUMB_HOVER_R; g = THUMB_HOVER_G; b = THUMB_HOVER_B;
    }

    draw_rounded_rect(tx, ty, tw, th, r, g, b, alpha, fb_w, fb_h);
}

bool scrollbar_is_animating(const ScrollbarState *state)
{
    if (!scrollbar_needed(state)) return false;
    if (state->hovered || state->dragging) return true;
    extern double glfwGetTime(void);
    float elapsed = (float)glfwGetTime() - state->last_scroll_time;
    return elapsed < (FADE_START + FADE_DURATION);
}
