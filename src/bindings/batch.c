// batch.c - batch rendering API
//
// JS writes vertex data into shared buffer, then calls flush to submit.
//
// Vertex format (12 floats per vertex):
//   [0-1]  x, y         position (world coords)
//   [2-3]  u, v         texture UV (atlas space)
//   [4-7]  r, g, b, a   color
//   [8-11] uv_rect.x/y/z/w  atlas uv_rect
//
// API:
//   batch.init(max_quads)
//   batch.get_buffer()    -> Float32Array
//   batch.set_count(n)
//   batch.get_count()
//   batch.flush([zlayer, texIdx, customViewId]) -> submit to GPU
//   batch.add_quad(x, y, w, h, u0, v0, u1, v1, r, g, b, a)
//   batch.max_quads()
//   batch.floats_per_quad()

#include <stdio.h>
#include "../log.h"
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"
#include "bedrock/gfx/draw.h"
#include "bedrock/gfx/generated_shader.h"
#include "bedrock/gfx/render.h"
#include "bedrock/utils/utils.h"
#include "bedrock/helpers.h"
#include "bedrock/gfx/camera.h"

#include "../../external/stb/stb_image.h"
#include "../../external/sokol/c/sokol_gfx.h"

#define BATCH_FLOATS_PER_VERTEX 12
#define BATCH_VERTICES_PER_QUAD 4
#define BATCH_FLOATS_PER_QUAD (BATCH_FLOATS_PER_VERTEX * BATCH_VERTICES_PER_QUAD)
#define BATCH_MAX_QUADS_LIMIT 65536

static float *g_batch_buffer = NULL;
static int g_max_quads = 0;
static int g_quad_count = 0;

static JSValue js_batch_init(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "batch.init requires (max_quads)");
    }

    int max_quads;
    if (JS_ToInt32(ctx, &max_quads, argv[0]) < 0) return JS_EXCEPTION;

    if (max_quads <= 0 || max_quads > BATCH_MAX_QUADS_LIMIT) {
        return JS_ThrowRangeError(ctx, "max_quads must be 1-%d", BATCH_MAX_QUADS_LIMIT);
    }

    if (g_batch_buffer) {
        free(g_batch_buffer);
    }

    g_max_quads = max_quads;
    g_quad_count = 0;
    g_batch_buffer = (float *)calloc(max_quads * BATCH_FLOATS_PER_QUAD, sizeof(float));

    if (!g_batch_buffer) {
        return JS_ThrowOutOfMemory(ctx);
    }

    LOG_VERBOSE("[batch] initialized with %d quads capacity\n", max_quads);
    return JS_UNDEFINED;
}

static JSValue js_batch_get_buffer(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    if (!g_batch_buffer || g_max_quads <= 0) {
        return JS_NULL;
    }

    size_t byte_length = (size_t)g_max_quads * BATCH_FLOATS_PER_QUAD * sizeof(float);

    JSValue ab = JS_NewArrayBuffer(ctx, (uint8_t *)g_batch_buffer, byte_length,
                                    NULL, NULL, 1);
    if (JS_IsException(ab)) {
        return ab;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "Float32Array");
    JSValue args[1] = { ab };
    JSValue result = JS_CallConstructor(ctx, ctor, 1, args);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, ab);

    return result;
}

static JSValue js_batch_set_count(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;

    int count;
    if (JS_ToInt32(ctx, &count, argv[0]) < 0) return JS_EXCEPTION;

    if (count < 0) count = 0;
    if (count > g_max_quads) count = g_max_quads;
    g_quad_count = count;

    return JS_UNDEFINED;
}

static JSValue js_batch_get_count(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    return JS_NewInt32(ctx, g_quad_count);
}

// flush: submit batch to render system with full 3D MVP transform
static JSValue js_batch_flush(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    if (!g_batch_buffer || g_quad_count <= 0) {
        return JS_NewInt32(ctx, 0);
    }

    int zlayer = ZLayer_playspace;
    int tex_idx = 0;
    int custom_view_id = 0;
    if (argc >= 1) {
        JS_ToInt32(ctx, &zlayer, argv[0]);
        if (zlayer < 0 || zlayer >= ZLAYER_COUNT) zlayer = ZLayer_playspace;
    }
    if (argc >= 2) {
        JS_ToInt32(ctx, &tex_idx, argv[1]);
    }
    if (argc >= 3) {
        JS_ToInt32(ctx, &custom_view_id, argv[2]);
    }

    extern Draw_Frame draw_frame;
    Quad_Array *quads = &draw_frame.quads[zlayer];

    // compute VP from same source as graphics_draw_mesh
    Matrix4 view, vp_buf;
    matrix4_inverse(draw_frame.coord_space.camera, view);
    matrix4_mul(draw_frame.coord_space.proj, view, vp_buf);
    const float *vp = vp_buf;

    int needed = quads->count + g_quad_count;
    if (needed > quads->capacity) {
        int new_cap = quads->capacity;
        while (new_cap < needed) new_cap *= 2;
        Quad *new_data = (Quad *)realloc(quads->data, new_cap * sizeof(Quad));
        if (!new_data) {
            g_quad_count = 0;
            return JS_NewInt32(ctx, 0);
        }
        quads->data = new_data;
        quads->capacity = new_cap;
    }

    for (int i = 0; i < g_quad_count; i++) {
        float *q = &g_batch_buffer[i * BATCH_FLOATS_PER_QUAD];
        Quad *dst = &quads->data[quads->count + i];

        for (int v = 0; v < BATCH_VERTICES_PER_QUAD; v++) {
            float *src = &q[v * BATCH_FLOATS_PER_VERTEX];
            Vertex *vert = &(*dst)[v];

            vert->pos[0] = src[0];
            vert->pos[1] = 0.3f;
            vert->pos[2] = src[1];

            vert->col[0] = src[4];
            vert->col[1] = src[5];
            vert->col[2] = src[6];
            vert->col[3] = src[7];

            vert->uv[0] = src[2];
            vert->uv[1] = src[3];

            vert->_pad0 = 0;
            vert->quad_flags = 0;
            vert->_padding[0] = 0;
            vert->_padding[1] = 0;

            vert->uv_rect[0] = 0;
            vert->uv_rect[1] = 0;
            vert->uv_rect[2] = 0;
            vert->uv_rect[3] = 0;
        }
    }

    int start_index = quads->count;
    quads->count += g_quad_count;
    draw_frame.total_quad_count += g_quad_count;

    Batch_Array *batches = &draw_frame.batches[zlayer];
    Vec4 white = {1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 zero = {0.0f, 0.0f, 0.0f, 0.0f};

    Render_Batch new_batch;
    memset(&new_batch, 0, sizeof(Render_Batch));
    new_batch.start_quad_index = start_index;
    new_batch.quad_count = g_quad_count;
    new_batch.tex_index = (uint8_t)tex_idx;
    memcpy(new_batch.col_override, white, sizeof(Vec4));
    memcpy(new_batch.col_override_2, white, sizeof(Vec4));
    memcpy(new_batch.params, zero, sizeof(Vec4));
    new_batch.use_mvp = true;
    memcpy(new_batch.mvp, vp, sizeof(Matrix4));
    // custom texture view (game-defined target)
    if (custom_view_id != 0) {
        new_batch.custom_view.id = (uint32_t)custom_view_id;
    }
    batch_array_push(batches, new_batch);

    int drawn = g_quad_count;
    g_quad_count = 0;

    return JS_NewInt32(ctx, drawn);
}

// add_quad: convenience function, add quad with atlas UV
// batch.add_quad(x, y, w, h, u0, v0, u1, v1, r, g, b, a)
static JSValue js_batch_add_quad(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    if (!g_batch_buffer || g_quad_count >= g_max_quads) {
        return JS_FALSE;
    }

    if (argc < 12) {
        return JS_ThrowTypeError(ctx, "batch.add_quad requires (x, y, w, h, u0, v0, u1, v1, r, g, b, a)");
    }

    double x, y, w, h, u0, v0, u1, v1, r, g, b, a;
    JS_ToFloat64(ctx, &x, argv[0]);
    JS_ToFloat64(ctx, &y, argv[1]);
    JS_ToFloat64(ctx, &w, argv[2]);
    JS_ToFloat64(ctx, &h, argv[3]);
    JS_ToFloat64(ctx, &u0, argv[4]);
    JS_ToFloat64(ctx, &v0, argv[5]);
    JS_ToFloat64(ctx, &u1, argv[6]);
    JS_ToFloat64(ctx, &v1, argv[7]);
    JS_ToFloat64(ctx, &r, argv[8]);
    JS_ToFloat64(ctx, &g, argv[9]);
    JS_ToFloat64(ctx, &b, argv[10]);
    JS_ToFloat64(ctx, &a, argv[11]);

    float *q = &g_batch_buffer[g_quad_count * BATCH_FLOATS_PER_QUAD];

    float fu0 = (float)u0, fv0 = (float)v0, fu1 = (float)u1, fv1 = (float)v1;
    float fr = (float)r, fg = (float)g, fb = (float)b, fa = (float)a;

    // vertex 0: top-left
    q[0]  = (float)x;       q[1]  = (float)y;
    q[2]  = fu0;            q[3]  = fv0;
    q[4]  = fr; q[5] = fg;  q[6]  = fb; q[7] = fa;
    q[8]  = 0; q[9] = 0; q[10] = 0; q[11] = 0;

    // vertex 1: top-right
    q[12] = (float)(x + w); q[13] = (float)y;
    q[14] = fu1;            q[15] = fv0;
    q[16] = fr; q[17] = fg; q[18] = fb; q[19] = fa;
    q[20] = 0; q[21] = 0; q[22] = 0; q[23] = 0;

    // vertex 2: bottom-right
    q[24] = (float)(x + w); q[25] = (float)(y + h);
    q[26] = fu1;            q[27] = fv1;
    q[28] = fr; q[29] = fg; q[30] = fb; q[31] = fa;
    q[32] = 0; q[33] = 0; q[34] = 0; q[35] = 0;

    // vertex 3: bottom-left
    q[36] = (float)x;       q[37] = (float)(y + h);
    q[38] = fu0;            q[39] = fv1;
    q[40] = fr; q[41] = fg; q[42] = fb; q[43] = fa;
    q[44] = 0; q[45] = 0; q[46] = 0; q[47] = 0;

    g_quad_count++;
    return JS_TRUE;
}

static JSValue js_batch_max_quads(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    return JS_NewInt32(ctx, g_max_quads);
}

static JSValue js_batch_floats_per_quad(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    return JS_NewInt32(ctx, BATCH_FLOATS_PER_QUAD);
}

// batch.load_atlas(path) - load PNG and replace VIEW_tex0 atlas texture
static JSValue js_batch_load_atlas(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "batch.load_atlas requires (path)");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    FILE *f = fopen(path, "rb");
    if (!f) {
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "batch.load_atlas: cannot open %s", path);
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *png_data = malloc(file_size);
    fread(png_data, 1, file_size, f);
    fclose(f);

    stbi_set_flip_vertically_on_load(1);
    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(png_data, (int)file_size, &w, &h, &channels, 4);
    free(png_data);

    if (!pixels) {
        JS_FreeCString(ctx, path);
        return JS_ThrowInternalError(ctx, "batch.load_atlas: stbi_load failed");
    }

    // destroy old atlas texture and view, create new ones
    extern Render_State render_state;
    extern Atlas atlas;

    sg_destroy_view(render_state.bind.views[VIEW_tex0]);
    sg_destroy_image(atlas.sg_image);

    atlas.w = w;
    atlas.h = h;
    atlas.sg_image = sg_make_image(&(sg_image_desc){
        .width = w,
        .height = h,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = { .ptr = pixels, .size = (size_t)(w * h * 4) },
        .label = "batch-atlas",
    });

    render_state.bind.views[VIEW_tex0] = sg_make_view(
        &(sg_view_desc){ .texture.image = atlas.sg_image, .label = "batch-atlas-view" });

    stbi_image_free(pixels);

    LOG_INFO("[batch] atlas loaded: %s (%dx%d)\n", path, w, h);
    JS_FreeCString(ctx, path);

    return JS_UNDEFINED;
}

int js_init_batch_module(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue batch_obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, batch_obj, "init",
                      JS_NewCFunction(ctx, js_batch_init, "init", 1));
    JS_SetPropertyStr(ctx, batch_obj, "get_buffer",
                      JS_NewCFunction(ctx, js_batch_get_buffer, "get_buffer", 0));
    JS_SetPropertyStr(ctx, batch_obj, "set_count",
                      JS_NewCFunction(ctx, js_batch_set_count, "set_count", 1));
    JS_SetPropertyStr(ctx, batch_obj, "get_count",
                      JS_NewCFunction(ctx, js_batch_get_count, "get_count", 0));
    JS_SetPropertyStr(ctx, batch_obj, "flush",
                      JS_NewCFunction(ctx, js_batch_flush, "flush", 0));
    JS_SetPropertyStr(ctx, batch_obj, "add_quad",
                      JS_NewCFunction(ctx, js_batch_add_quad, "add_quad", 12));
    JS_SetPropertyStr(ctx, batch_obj, "max_quads",
                      JS_NewCFunction(ctx, js_batch_max_quads, "max_quads", 0));
    JS_SetPropertyStr(ctx, batch_obj, "floats_per_quad",
                      JS_NewCFunction(ctx, js_batch_floats_per_quad, "floats_per_quad", 0));
    JS_SetPropertyStr(ctx, batch_obj, "load_atlas",
                      JS_NewCFunction(ctx, js_batch_load_atlas, "load_atlas", 1));

    JS_SetPropertyStr(ctx, batch_obj, "FLOATS_PER_QUAD", JS_NewInt32(ctx, BATCH_FLOATS_PER_QUAD));
    JS_SetPropertyStr(ctx, batch_obj, "FLOATS_PER_VERTEX", JS_NewInt32(ctx, BATCH_FLOATS_PER_VERTEX));
    JS_SetPropertyStr(ctx, batch_obj, "VERTICES_PER_QUAD", JS_NewInt32(ctx, BATCH_VERTICES_PER_QUAD));

    JS_SetPropertyStr(ctx, global, "batch", batch_obj);
    JS_FreeValue(ctx, global);

    LOG_VERBOSE("[batch] module loaded\n");
    return 0;
}
