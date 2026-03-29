#ifndef SCROLLBAR_H
#define SCROLLBAR_H

#include <stdbool.h>

#define SCROLLBAR_WIDTH   7.0f
#define SCROLLBAR_MARGIN  2.0f

typedef struct {
    float scroll_offset;
    float content_height;
    float viewport_height;
    float x;                /* rechte Kante des Bereichs */
    float y;                /* obere Kante des Scrollbereichs */
    float last_scroll_time;
    bool  hovered;
    bool  dragging;
    float drag_offset;      /* Offset innerhalb des Thumbs beim Drag-Start */
} ScrollbarState;

/* Zeichnet die Scrollbar. */
void scrollbar_draw(const ScrollbarState *state, int fb_w, int fb_h);

void scrollbar_touch(ScrollbarState *state, double now);

bool scrollbar_needed(const ScrollbarState *state);

/* Berechnet Thumb-Position und -Groesse. */
void scrollbar_get_thumb(const ScrollbarState *state,
                         float *thumb_x, float *thumb_y,
                         float *thumb_w, float *thumb_h);

/* Hit-Test: ist (mx, my) ueber der Scrollbar-Track-Area? */
bool scrollbar_hit_test(const ScrollbarState *state, float mx, float my);

/* Hit-Test: ist (mx, my) ueber dem Thumb? */
bool scrollbar_thumb_hit(const ScrollbarState *state, float mx, float my);

/* Berechnet neuen scroll_offset aus Maus-Y-Position waehrend Drag. */
float scrollbar_drag_to(const ScrollbarState *state, float my);

/* Berechnet neuen scroll_offset bei Klick auf Track (jump to position). */
float scrollbar_click_track(const ScrollbarState *state, float my);

#endif
