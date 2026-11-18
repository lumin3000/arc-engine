// splat.c — see splat.h.
//
// Rendering follows the ink-studio CPU reference renderer (cli/render.js) for the
// orthographic case: per-splat covariance projection with a 0.15 px^2 low-pass, 3-sigma
// quads, exp() falloff cut at -4.5, solid splats sharpened with smoothstep(0.38, 0.62),
// back-to-front alpha blending, and a normalised depth blended alongside for the resolve.

#include "splat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../external/sokol/c/sokol_app.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../log.h"
#include "splat_shader.h"

#define SPLAT_FATAL(...)          \
    do {                          \
        LOG_ERROR(__VA_ARGS__);   \
        exit(1);                  \
    } while (0)

// ---------------------------------------------------------------------------
// Loading

#define SPLAT_HEAD_BYTES 188

static uint32_t rd_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float rd_f32(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

Splat_Cloud *splat_cloud_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) SPLAT_FATAL("[splat] cannot open %s\n", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < SPLAT_HEAD_BYTES) SPLAT_FATAL("[splat] %s: file too small (%ld bytes)\n", path, size);
    uint8_t *buf = malloc((size_t)size);
    if (!buf) SPLAT_FATAL("[splat] %s: out of memory (%ld bytes)\n", path, size);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) SPLAT_FATAL("[splat] %s: short read\n", path);
    fclose(f);
    if (memcmp(buf, "ARSP", 4) != 0) SPLAT_FATAL("[splat] %s: bad magic\n", path);
    if (rd_u32(buf + 4) != 1) SPLAT_FATAL("[splat] %s: unsupported version %u\n", path, rd_u32(buf + 4));

    Splat_Cloud *c = calloc(1, sizeof(*c));
    c->count = rd_u32(buf + 8);
    c->pal_n = rd_u32(buf + 12);
    c->bone_n = rd_u32(buf + 16);
    c->frame_n = rd_u32(buf + 20);
    c->fps = rd_f32(buf + 24);
    c->radius = rd_f32(buf + 28);
    for (int i = 0; i < 3; i++) {
        c->bmin[i] = rd_f32(buf + 32 + i * 4);
        c->bmax[i] = rd_f32(buf + 44 + i * 4);
    }
    c->has_cam = rd_u32(buf + 56) != 0;
    for (int i = 0; i < 16; i++) {
        c->cam[i] = rd_f32(buf + 60 + i * 4);
        c->cam_view[i] = rd_f32(buf + 124 + i * 4);
    }
    if (c->pal_n == 0 || c->pal_n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] %s: palette size %u\n", path, c->pal_n);
    size_t off = SPLAT_HEAD_BYTES;
    size_t need = off + (size_t)c->pal_n * 4 + (size_t)c->count * SPLAT_FLOATS * 4 +
                  (size_t)c->frame_n * c->bone_n * 12 * 4;
    if ((size_t)size != need) SPLAT_FATAL("[splat] %s: size %ld, header implies %zu\n", path, size, need);
    for (uint32_t i = 0; i < c->pal_n; i++) c->pal[i] = rd_u32(buf + off + i * 4);
    off += (size_t)c->pal_n * 4;
    size_t data_bytes = (size_t)c->count * SPLAT_FLOATS * 4;
    c->data = malloc(data_bytes);
    if (!c->data) SPLAT_FATAL("[splat] %s: out of memory for %u splats\n", path, c->count);
    memcpy(c->data, buf + off, data_bytes);
    off += data_bytes;
    if (c->frame_n * c->bone_n > 0) {
        size_t bb = (size_t)c->frame_n * c->bone_n * 12 * 4;
        c->bones = malloc(bb);
        memcpy(c->bones, buf + off, bb);
    }
    free(buf);
    LOG_INFO("[splat] loaded %s: %u splats, %u colours, %u bones x %u frames\n", path, c->count, c->pal_n,
             c->bone_n, c->frame_n);
    return c;
}

void splat_cloud_free(Splat_Cloud *c) {
    if (!c) return;
    free(c->data);
    free(c->bones);
    free(c);
}

Splat_View splat_view_from_cloud_camera(const Splat_Cloud *c) {
    if (!c->has_cam) SPLAT_FATAL("[splat] cloud has no reference camera\n");
    Splat_View v = {0};
    memcpy(v.view, c->cam_view, sizeof(v.view));
    v.eye_dist = c->cam[6];
    v.ortho_half_h = c->cam[7];
    v.near_dist = c->cam[9];
    v.dither = c->cam[11];
    v.art_w = (int)c->cam[12];
    v.art_h = (int)c->cam[13];
    v.radius = c->radius;
    v.outline = 0.0f;
    return v;
}

// ---------------------------------------------------------------------------
// GPU state

typedef struct {
    int w, h;
    sg_image color, depth;           // depth: normalised depth in .r (second colour target)
    sg_view color_att, depth_att;
    sg_view color_tex, depth_tex;
} Splat_Target;

static struct {
    bool ready;
    sg_shader splat_shd, resolve_shd, blit_shd;
    sg_pipeline splat_pip, resolve_pip, blit_pip;
    sg_sampler nearest;
    sg_buffer instances;
    uint32_t count;
    Splat_View view;
    bool have_view;
    float pal[SPLAT_MAX_PALETTE][4];
    int pal_n;
    Splat_Target samples, native;
    sg_image art;
    sg_view art_att, art_tex;
    int art_w, art_h;
    int last_scale;
} S;

static void target_destroy(Splat_Target *t) {
    if (t->w == 0) return;
    sg_destroy_view(t->color_att);
    sg_destroy_view(t->depth_att);
    sg_destroy_view(t->color_tex);
    sg_destroy_view(t->depth_tex);
    sg_destroy_image(t->color);
    sg_destroy_image(t->depth);
    memset(t, 0, sizeof(*t));
}

static sg_image make_target_image(int w, int h, const char *label) {
    return sg_make_image(&(sg_image_desc){
        .usage = {.color_attachment = true},
        .width = w,
        .height = h,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .sample_count = 1,
        .label = label,
    });
}

static void target_make(Splat_Target *t, int w, int h, const char *label) {
    target_destroy(t);
    t->w = w;
    t->h = h;
    t->color = make_target_image(w, h, label);
    t->depth = make_target_image(w, h, label);
    t->color_att = sg_make_view(&(sg_view_desc){.color_attachment = {.image = t->color}});
    t->depth_att = sg_make_view(&(sg_view_desc){.color_attachment = {.image = t->depth}});
    t->color_tex = sg_make_view(&(sg_view_desc){.texture = {.image = t->color}});
    t->depth_tex = sg_make_view(&(sg_view_desc){.texture = {.image = t->depth}});
}

static void ensure_targets(int aw, int ah) {
    if (S.art_w == aw && S.art_h == ah) return;
    target_make(&S.samples, aw * 2, ah * 2, "splat-samples");
    target_make(&S.native, aw, ah, "splat-native");
    if (S.art_w) {
        sg_destroy_view(S.art_att);
        sg_destroy_view(S.art_tex);
        sg_destroy_image(S.art);
    }
    S.art = make_target_image(aw, ah, "splat-art");
    S.art_att = sg_make_view(&(sg_view_desc){.color_attachment = {.image = S.art}});
    S.art_tex = sg_make_view(&(sg_view_desc){.texture = {.image = S.art}});
    S.art_w = aw;
    S.art_h = ah;
}

void splat_init(void) {
    if (S.ready) return;
    S.splat_shd = sg_make_shader(splat_shader_desc(sg_query_backend()));
    S.resolve_shd = sg_make_shader(resolve_shader_desc(sg_query_backend()));
    S.blit_shd = sg_make_shader(blit_shader_desc(sg_query_backend()));

    sg_blend_state over = {
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    S.splat_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.splat_shd,
        .layout = {
            .buffers[0] = {.stride = SPLAT_FLOATS * 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE},
            .attrs = {
                [ATTR_splat_a_pos] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 0},
                [ATTR_splat_a_cov0] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 16},
                [ATTR_splat_a_cov1] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 32},
            },
        },
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .depth = {.pixel_format = SG_PIXELFORMAT_NONE},
        .color_count = 2,
        .colors = {
            [0] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [1] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
        },
        .sample_count = 1,
        .label = "splat-pipeline",
    });
    S.resolve_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.resolve_shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .depth = {.pixel_format = SG_PIXELFORMAT_NONE},
        .colors[0] = {.pixel_format = SG_PIXELFORMAT_RGBA8},
        .sample_count = 1,
        .label = "splat-resolve-pipeline",
    });
    S.blit_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.blit_shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .label = "splat-blit-pipeline",
    });
    S.nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST,
        .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "splat-nearest",
    });
    S.ready = true;
}

typedef struct {
    float key;
    uint32_t idx;
} Sort_Item;

static int cmp_key(const void *a, const void *b) {
    float ka = ((const Sort_Item *)a)->key, kb = ((const Sort_Item *)b)->key;
    if (ka < kb) return -1;
    if (ka > kb) return 1;
    uint32_t ia = ((const Sort_Item *)a)->idx, ib = ((const Sort_Item *)b)->idx;
    return ia < ib ? -1 : ia > ib;
}

void splat_upload(const Splat_Cloud *c, const float V[16]) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_upload before splat_init\n");
    if (c->count == 0) SPLAT_FATAL("[splat] empty cloud\n");
    // back to front: view-space z ascending (more negative = farther), as cli/render.js sorts
    Sort_Item *items = malloc(sizeof(Sort_Item) * c->count);
    for (uint32_t i = 0; i < c->count; i++) {
        const float *d = c->data + (size_t)i * SPLAT_FLOATS;
        items[i].key = V[2] * d[0] + V[6] * d[1] + V[10] * d[2] + V[14];
        items[i].idx = i;
    }
    qsort(items, c->count, sizeof(Sort_Item), cmp_key);
    size_t bytes = (size_t)c->count * SPLAT_FLOATS * 4;
    float *sorted = malloc(bytes);
    for (uint32_t i = 0; i < c->count; i++)
        memcpy(sorted + (size_t)i * SPLAT_FLOATS, c->data + (size_t)items[i].idx * SPLAT_FLOATS, SPLAT_FLOATS * 4);
    free(items);
    if (S.count) sg_destroy_buffer(S.instances);
    S.instances = sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = {.ptr = sorted, .size = bytes},
        .label = "splat-instances",
    });
    free(sorted);
    S.count = c->count;
    splat_set_palette(c->pal, (int)c->pal_n);
}

void splat_set_palette(const uint32_t *pal, int n) {
    if (n <= 0 || n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] palette size %d\n", n);
    for (int i = 0; i < n; i++) {
        S.pal[i][0] = (float)((pal[i] >> 16) & 255) / 255.0f;
        S.pal[i][1] = (float)((pal[i] >> 8) & 255) / 255.0f;
        S.pal[i][2] = (float)(pal[i] & 255) / 255.0f;
        S.pal[i][3] = 1.0f;
    }
    S.pal_n = n;
}

void splat_set_view(const Splat_View *v) {
    if (v->art_w <= 0 || v->art_h <= 0) SPLAT_FATAL("[splat] art grid %dx%d\n", v->art_w, v->art_h);
    S.view = *v;
    S.have_view = true;
}

static void draw_splats(const Splat_Target *t) {
    const Splat_View *v = &S.view;
    vs_splat_params_t p;
    memcpy(p.view.v, v->view, sizeof(p.view.v));
    p.cam = (Splat_Vec4){{v->ortho_half_h, (float)t->w / (float)t->h, (float)t->w, (float)t->h}};
    p.dep = (Splat_Vec4){{v->eye_dist, v->radius, (float)t->h / 2.0f / v->ortho_half_h, v->near_dist}};
    float *bg = S.pal[0];
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {bg[0], bg[1], bg[2], 0.0f}},
            .colors[1] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {1.0f, 0.0f, 0.0f, 0.0f}},
        },
        .attachments = {.colors = {t->color_att, t->depth_att}},
        .label = "splat-pass",
    });
    sg_apply_pipeline(S.splat_pip);
    sg_apply_bindings(&(sg_bindings){.vertex_buffers[0] = S.instances});
    sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
    sg_draw(0, 4, (int)S.count);
    sg_end_pass();
}

void splat_render_offscreen(void) {
    if (!S.ready || !S.have_view || S.count == 0) return;
    ensure_targets(S.view.art_w, S.view.art_h);
    draw_splats(&S.samples);
    draw_splats(&S.native);

    const float flip = sg_query_features().origin_top_left ? 1.0f : 0.0f;
    fs_resolve_params_t rp;
    memset(&rp, 0, sizeof(rp));
    memcpy(rp.pal, S.pal, sizeof(float) * 4 * (size_t)S.pal_n);
    rp.opts = (Splat_Vec4){{(float)S.pal_n, S.view.dither, flip, S.view.outline}};
    rp.size = (Splat_Vec4){{(float)S.art_w, (float)S.art_h, (float)S.samples.w, (float)S.samples.h}};
    vs_quad_params_t qp = {.quad = {{flip, 0, 0, 0}}};
    sg_begin_pass(&(sg_pass){
        .action = {.colors[0] = {.load_action = SG_LOADACTION_DONTCARE}},
        .attachments = {.colors[0] = S.art_att},
        .label = "splat-resolve-pass",
    });
    sg_apply_pipeline(S.resolve_pip);
    sg_apply_bindings(&(sg_bindings){
        .views = {
            [VIEW_u_scene] = S.samples.color_tex,
            [VIEW_u_depth] = S.samples.depth_tex,
            [VIEW_u_native] = S.native.color_tex,
        },
        .samplers[SMP_u_smp] = S.nearest,
    });
    sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
    sg_apply_uniforms(UB_fs_resolve_params, &SG_RANGE(rp));
    sg_draw(0, 4, 1);
    sg_end_pass();
}

void splat_composite(Splat_Show show) {
    if (!S.ready || !S.have_view || S.count == 0 || S.art_w == 0) return;
    sg_view tex = S.art_tex;
    int tw = S.art_w, th = S.art_h;
    if (show == SPLAT_SHOW_NATIVE) {
        tex = S.native.color_tex;
    } else if (show == SPLAT_SHOW_SAMPLES) {
        tex = S.samples.color_tex;
        tw = S.samples.w;
        th = S.samples.h;
    }
    const int sw = sapp_width(), sh = sapp_height();
    int k = sw / tw < sh / th ? sw / tw : sh / th;
    if (k < 1) k = 1;
    S.last_scale = k;
    // the pass's storage origin decides how a target is read back onto the screen
    vs_quad_params_t qp = {.quad = {{sg_query_features().origin_top_left ? 1.0f : 0.0f, 0, 0, 0}}};
    sg_apply_viewport(0, 0, tw * k, th * k, true);
    sg_apply_pipeline(S.blit_pip);
    sg_apply_bindings(&(sg_bindings){.views[VIEW_blit_tex] = tex, .samplers[SMP_blit_smp] = S.nearest});
    sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
    sg_draw(0, 4, 1);
    sg_apply_viewport(0, 0, sw, sh, true);
}

Splat_Stats splat_stats(void) {
    return (Splat_Stats){.splats = S.count, .art_w = S.art_w, .art_h = S.art_h, .scale = S.last_scale};
}
