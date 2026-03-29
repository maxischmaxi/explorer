#ifndef URL_BAR_H
#define URL_BAR_H

#include <stdbool.h>

struct GLFWwindow;

#define URL_BAR_MAX_LEN 2048

typedef struct {
    char  text[URL_BAR_MAX_LEN];
    int   cursor;
    int   sel_start;  /* Selection-Anker (-1 = keine Selection) */
    int   len;
    bool  focused;
    float padding;
    float height;
} URLBar;

void url_bar_init(URLBar *bar);

void url_bar_draw(const URLBar *bar, float avail_w, int fb_w, int fb_h);

void url_bar_char_input(URLBar *bar, unsigned int codepoint);

/* Gibt true zurueck wenn Enter gedrueckt wurde. */
bool url_bar_key_input(URLBar *bar, int key, int action, int mods,
                       struct GLFWwindow *window);

/* click_count: 1=normal, 2=doppelklick (Wort), 3=dreifach (alles) */
bool url_bar_click(URLBar *bar, float x, float y, int window_width,
                   int click_count);

/* Fokussiert die Bar und selektiert den gesamten Text (Ctrl+L). */
void url_bar_focus_select_all(URLBar *bar);

#endif
