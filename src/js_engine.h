#ifndef JS_ENGINE_H
#define JS_ENGINE_H

typedef struct JSEngine JSEngine;

/* Erstellt eine neue JS-Engine-Instanz */
JSEngine *js_engine_create(void);

/* Fuehrt JavaScript-Code aus und gibt das Ergebnis als String zurueck.
   Der Aufrufer muss den zurueckgegebenen String mit free() freigeben.
   Gibt NULL zurueck bei Fehler. */
char *js_engine_eval(JSEngine *engine, const char *code);

/* Gibt die JS-Engine-Instanz frei */
void js_engine_destroy(JSEngine *engine);

#endif
