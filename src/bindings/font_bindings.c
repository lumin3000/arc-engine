
#include "font_bindings.h"
#include "../log.h"
#include "quickjs.h"
#include "bedrock/gfx/font_manager.h"
#include <stdlib.h>
#include <string.h>

static struct font_manager *g_font_manager = NULL;

static JSValue js_font_load(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "font.load requires 2 arguments (path, font_id)");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    int32_t font_id;
    if (JS_ToInt32(ctx, &font_id, argv[1]) < 0) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    int result = font_manager_load_font(g_font_manager, path, font_id);
    JS_FreeCString(ctx, path);

    return JS_NewBool(ctx, result == 0);
}

static JSValue js_font_glyph(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "font.glyph requires 3 arguments (font_id, codepoint, size)");
    }

    int32_t font_id, codepoint, size;
    if (JS_ToInt32(ctx, &font_id, argv[0]) < 0 ||
        JS_ToInt32(ctx, &codepoint, argv[1]) < 0 ||
        JS_ToInt32(ctx, &size, argv[2]) < 0) {
        return JS_EXCEPTION;
    }

    FontGlyph g;
    const char *err = font_manager_glyph(g_font_manager, font_id, codepoint, size, &g);
    if (err) {
        return JS_ThrowInternalError(ctx, "font.glyph error: %s", err);
    }

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "u", JS_NewInt32(ctx, g.u));
    JS_SetPropertyStr(ctx, obj, "v", JS_NewInt32(ctx, g.v));
    JS_SetPropertyStr(ctx, obj, "w", JS_NewInt32(ctx, g.w));
    JS_SetPropertyStr(ctx, obj, "h", JS_NewInt32(ctx, g.h));
    JS_SetPropertyStr(ctx, obj, "offset_x", JS_NewInt32(ctx, g.offset_x));
    JS_SetPropertyStr(ctx, obj, "offset_y", JS_NewInt32(ctx, g.offset_y));
    JS_SetPropertyStr(ctx, obj, "advance_x", JS_NewInt32(ctx, g.advance_x));
    JS_SetPropertyStr(ctx, obj, "advance_y", JS_NewInt32(ctx, g.advance_y));

    return obj;
}

static JSValue js_font_metrics(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "font.metrics requires 2 arguments (font_id, size)");
    }

    int32_t font_id, size;
    if (JS_ToInt32(ctx, &font_id, argv[0]) < 0 ||
        JS_ToInt32(ctx, &size, argv[1]) < 0) {
        return JS_EXCEPTION;
    }

    int ascent, descent, line_gap;
    font_manager_metrics(g_font_manager, font_id, size, &ascent, &descent, &line_gap);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ascent", JS_NewInt32(ctx, ascent));
    JS_SetPropertyStr(ctx, obj, "descent", JS_NewInt32(ctx, descent));
    JS_SetPropertyStr(ctx, obj, "line_gap", JS_NewInt32(ctx, line_gap));

    return obj;
}

static JSValue js_font_flush(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;

    int dirty = font_manager_flush(g_font_manager);
    return JS_NewBool(ctx, dirty);
}

static JSValue js_font_sdf_mask(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;

    float mask = font_manager_sdf_mask(g_font_manager);
    return JS_NewFloat64(ctx, mask);
}

static JSValue js_font_sdf_distance(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "font.sdf_distance requires 1 argument (num_pixel)");
    }

    int32_t num_pixel;
    if (JS_ToInt32(ctx, &num_pixel, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    float dist = font_manager_sdf_distance(g_font_manager, (uint8_t)num_pixel);
    return JS_NewFloat64(ctx, dist);
}

static JSValue js_font_texture_size(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;

    return JS_NewInt32(ctx, FONT_MANAGER_TEXSIZE);
}

static JSValue js_font_measure_text(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "font.measure_text requires 3 arguments (font_id, text, size)");
    }

    int32_t font_id, size;
    if (JS_ToInt32(ctx, &font_id, argv[0]) < 0 ||
        JS_ToInt32(ctx, &size, argv[2]) < 0) {
        return JS_EXCEPTION;
    }

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text) return JS_EXCEPTION;

    int ascent, descent, line_gap;
    font_manager_metrics(g_font_manager, font_id, size, &ascent, &descent, &line_gap);

    float total_width = 0;
    const unsigned char *s = (const unsigned char *)text;

    while (*s) {
        uint32_t codepoint;

        if (*s < 0x80) {
            codepoint = *s++;
        } else if (*s < 0xE0) {
            codepoint = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if (*s < 0xF0) {
            codepoint = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            codepoint = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            s += 4;
        }

        if (codepoint == '\n') {
            continue;
        }

        FontGlyph g;
        const char *err = font_manager_glyph(g_font_manager, font_id, codepoint, size, &g);
        if (!err) {
            total_width += g.advance_x;
        }
    }

    JS_FreeCString(ctx, text);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, total_width));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewInt32(ctx, ascent - descent));

    return obj;
}

#include "bedrock/gfx/sdf_text_pipeline.h"

static JSValue js_font_draw_text(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    (void)this_val;

    if (argc < 6) {
        return JS_ThrowTypeError(ctx, "font.drawText requires 6 arguments (x, y, font_id, text, size, color)");
    }

    double x, y;
    int32_t font_id, size;
    uint32_t color;

    if (JS_ToFloat64(ctx, &x, argv[0]) < 0 ||
        JS_ToFloat64(ctx, &y, argv[1]) < 0 ||
        JS_ToInt32(ctx, &font_id, argv[2]) < 0 ||
        JS_ToInt32(ctx, &size, argv[4]) < 0) {
        return JS_EXCEPTION;
    }

    if (JS_IsNumber(argv[5])) {
        int64_t c;
        JS_ToInt64(ctx, &c, argv[5]);
        color = (uint32_t)c;
    } else {

        color = 0xFFFFFFFF;
    }

    const char *text = JS_ToCString(ctx, argv[3]);
    if (!text) return JS_EXCEPTION;

    float cur_x = (float)x;
    float cur_y = (float)y;
    const unsigned char *s = (const unsigned char *)text;

    while (*s) {
        uint32_t codepoint;

        if (*s < 0x80) {
            codepoint = *s++;
        } else if (*s < 0xE0) {
            codepoint = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if (*s < 0xF0) {
            codepoint = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            codepoint = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            s += 4;
        }

        if (codepoint == '\n') {

            cur_x = (float)x;
            int ascent, descent, line_gap;
            font_manager_metrics(g_font_manager, font_id, size, &ascent, &descent, &line_gap);
            cur_y += ascent - descent + line_gap;
            continue;
        }

        FontGlyph g, og;
        const char *err = font_manager_glyph_ex(g_font_manager, font_id, codepoint, size, &g, &og);
        if (!err) {

            sdf_text_add_glyph_ex(cur_x, cur_y, g.u, g.v, g.w, g.h, og.w, og.h, g.offset_x, g.offset_y, color);
            cur_x += g.advance_x;
        }
    }

    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

struct font_manager *get_global_font_manager(void) {
    return g_font_manager;
}

struct font_manager *font_bindings_get_manager(void) {
    return g_font_manager;
}

int js_init_font_module(JSContext *ctx) {

    if (!g_font_manager) {
        g_font_manager = (struct font_manager *)malloc(font_manager_sizeof());
        if (!g_font_manager) {
            LOG_ERROR("[FontBindings] Failed to allocate font_manager\n");
            return -1;
        }
        font_manager_init(g_font_manager);
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue font_obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, font_obj, "load",
                      JS_NewCFunction(ctx, js_font_load, "load", 2));
    JS_SetPropertyStr(ctx, font_obj, "glyph",
                      JS_NewCFunction(ctx, js_font_glyph, "glyph", 3));
    JS_SetPropertyStr(ctx, font_obj, "metrics",
                      JS_NewCFunction(ctx, js_font_metrics, "metrics", 2));
    JS_SetPropertyStr(ctx, font_obj, "flush",
                      JS_NewCFunction(ctx, js_font_flush, "flush", 0));
    JS_SetPropertyStr(ctx, font_obj, "sdf_mask",
                      JS_NewCFunction(ctx, js_font_sdf_mask, "sdf_mask", 0));
    JS_SetPropertyStr(ctx, font_obj, "sdf_distance",
                      JS_NewCFunction(ctx, js_font_sdf_distance, "sdf_distance", 1));
    JS_SetPropertyStr(ctx, font_obj, "texture_size",
                      JS_NewCFunction(ctx, js_font_texture_size, "texture_size", 0));
    JS_SetPropertyStr(ctx, font_obj, "measure_text",
                      JS_NewCFunction(ctx, js_font_measure_text, "measure_text", 3));
    JS_SetPropertyStr(ctx, font_obj, "drawText",
                      JS_NewCFunction(ctx, js_font_draw_text, "drawText", 6));

    JS_SetPropertyStr(ctx, font_obj, "TEXSIZE", JS_NewInt32(ctx, FONT_MANAGER_TEXSIZE));

    JS_SetPropertyStr(ctx, global, "font", font_obj);
    JS_FreeValue(ctx, global);

    LOG_VERBOSE("[FontBindings] font module loaded\n");
    return 0;
}

void js_shutdown_font_module(void) {
    if (g_font_manager) {
        font_manager_shutdown(g_font_manager);
        free(g_font_manager);
        g_font_manager = NULL;
    }
}
