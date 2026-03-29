#ifndef CONTENT_VIEW_H
#define CONTENT_VIEW_H

#include <stddef.h>
#include "scrollbar.h"

/* Setzt HTML-Inhalt zum Rendern (wird intern geparst). */
void content_view_set_html(const char *html, size_t len);

/* Setzt die maximale Breite fuer Layout (logische Pixel). */
void content_view_set_max_width(float w);

/* Zeichnet den Content-Bereich unterhalb von y_offset.
   content_w: verfuegbare Breite in logischen Pixeln.
   fb_w/fb_h: volle Framebuffer-Groesse fuer Projektion. */
void content_view_draw(float y_offset, float content_w, int fb_w, int fb_h);

/* Scrollt (positiv = runter, negativ = hoch). */
void content_view_scroll(float delta);

/* Scrollt eine Seite (viewport_h in logischen Pixeln). dir: 1=runter, -1=hoch. */
void content_view_scroll_page(float viewport_h, int dir);

/* Scrollt zum Anfang (pos=0) oder Ende (pos=1). */
void content_view_scroll_to(int pos);

/* Gibt die href-URL des Links an Position (x, y) zurueck, oder NULL. */
const char *content_view_link_at(float x, float y);

/* Setzt den Hover-Index fuer Link-Highlighting. -1 = kein Hover. */
void content_view_set_hover(float x, float y);

/* Gibt Zugriff auf die Scrollbar-State fuer Mausinteraktion. */
ScrollbarState *content_view_get_scrollbar(void);

/* Setzt scroll_offset direkt (fuer Scrollbar-Drag). */
void content_view_set_scroll(float offset);

/* Gibt das aktuelle DOM-Dokument zurueck (fuer CSS-Engine). */
struct lxb_html_document *content_view_get_doc(void);

/* Markiert Layout als dirty (erzwingt Neuberechnung). */
void content_view_mark_dirty(void);

void content_view_free(void);

#endif
