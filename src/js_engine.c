#include "js_engine.h"

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <quickjs.h>

struct JSEngine {
    JSRuntime *rt;
    JSContext  *ctx;
};

JSEngine *js_engine_create(void)
{
    JSEngine *engine = calloc(1, sizeof(*engine));
    if (!engine)
        return NULL;

    engine->rt = JS_NewRuntime();
    if (!engine->rt) {
        free(engine);
        return NULL;
    }

    engine->ctx = JS_NewContext(engine->rt);
    if (!engine->ctx) {
        JS_FreeRuntime(engine->rt);
        free(engine);
        return NULL;
    }

    return engine;
}

/* JS_Eval ist die offizielle QuickJS-API zum Ausfuehren von JavaScript.
   Dies ist der Kernzweck einer Browser-JS-Engine. */
char *js_engine_eval(JSEngine *engine, const char *code)
{
    if (!engine || !code)
        return NULL;

    JSValue result = JS_Eval(engine->ctx, code, strlen(code), "<input>",
                             JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(engine->ctx);
        const char *str = JS_ToCString(engine->ctx, exc);
        char *err = NULL;
        if (str) {
            size_t len = strlen("Error: ") + strlen(str) + 1;
            err = malloc(len);
            if (err)
                snprintf(err, len, "Error: %s", str);
            JS_FreeCString(engine->ctx, str);
        }
        JS_FreeValue(engine->ctx, exc);
        JS_FreeValue(engine->ctx, result);
        return err;
    }

    const char *str = JS_ToCString(engine->ctx, result);
    char *output = NULL;
    if (str) {
        output = strdup(str);
        JS_FreeCString(engine->ctx, str);
    }

    JS_FreeValue(engine->ctx, result);
    return output;
}

void js_engine_destroy(JSEngine *engine)
{
    if (!engine)
        return;

    JS_FreeContext(engine->ctx);
    JS_FreeRuntime(engine->rt);
    free(engine);
}
