#ifndef DEVTOOLS_H
#define DEVTOOLS_H

#include <stdbool.h>
#include "scrollbar.h"

void  devtools_init(void);
void  devtools_draw(float panel_x, float y_offset, int fb_w, int fb_h);
void  devtools_scroll(float delta);
float devtools_get_width(void);
void  devtools_set_width(float w);
bool  devtools_is_visible(void);
void  devtools_toggle(void);
ScrollbarState *devtools_get_scrollbar(void);
void  devtools_set_scroll(float offset);
void  devtools_free(void);

#endif
