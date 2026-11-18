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
    v.lod_force = -1;
    return v;
}

Splat_View splat_view_ortho(float yaw, float pitch, const float target[3], float art_world, int art_w, int art_h,
                            float radius, float dither) {
    if (art_world <= 0.0f || art_w <= 0 || art_h <= 0) SPLAT_FATAL("[splat] bad ortho view parameters\n");
    const float fov = 38.0f * 3.14159265358979f / 180.0f;
    const float oh = 0.5f * art_world * (float)art_h;
    const float dist = oh / tanf(fov / 2.0f);
    const float eye_d = dist + 2.0f * radius + 4.0f;
    const float cp = cosf(pitch), sp = sinf(pitch);
    // axes exactly as cli/render.js viewMatrix
    float zx = cp * sinf(yaw), zy = sp, zz = cp * cosf(yaw);
    const float zl = sqrtf(zx * zx + zy * zy + zz * zz);
    zx /= zl; zy /= zl; zz /= zl;
    float xx = zz, xz = -zx;
    const float xy = 0.0f, xl = sqrtf(xx * xx + xz * xz);
    xx /= xl; xz /= xl;
    const float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    // snap the target onto the art-pixel lattice of the view plane
    const double tx = (double)target[0] * xx + (double)target[1] * xy + (double)target[2] * xz;
    const double ty = (double)target[0] * yx + (double)target[1] * yy + (double)target[2] * yz;
    // the lattice is offset by an irregular fraction of an art pixel so that pixel centres do not
    // fall on the round world positions sculpted solids are sampled on (those ties flip under
    // float rounding when the view moves)
    const double lat_off = 0.375;
    const double nx = floor(tx / art_world - lat_off + 0.5), ny = floor(ty / art_world - lat_off + 0.5);
    const double ddx = (nx + lat_off) * art_world - tx, ddy = (ny + lat_off) * art_world - ty;
    const double T[3] = {target[0] + xx * ddx + yx * ddy, target[1] + xy * ddx + yy * ddy, target[2] + xz * ddx + yz * ddy};
    const double e[3] = {T[0] + eye_d * zx, T[1] + eye_d * zy, T[2] + eye_d * zz};
    // the depth row: panning along the view plane must not change it, so take it from the
    // quantised depth of the target rather than from the snapped eye (rounding would move
    // pixels across the resolve's depth thresholds)
    const double tz = (double)target[0] * zx + (double)target[1] * zy + (double)target[2] * zz;
    const double tzq = floor(tz * 1024.0 + 0.5) / 1024.0;
    Splat_View v = {0};
    const float m[16] = {
        xx, yx, zx, 0,
        xy, yy, zy, 0,
        xz, yz, zz, 0,
        (float)-(xx * e[0] + xy * e[1] + xz * e[2]),
        (float)-(yx * e[0] + yy * e[1] + yz * e[2]),
        (float)-(tzq + eye_d),
        1,
    };
    memcpy(v.view, m, sizeof(m));
    v.ortho_half_h = oh;
    v.eye_dist = eye_d;
    // depth is normalised around the target over the visible depth range (8-bit target),
    // never beyond the scene radius
    v.radius = fminf(radius, 2.0f * oh + 8.0f);
    v.near_dist = fmaxf(0.4f, 0.15f * dist);
    v.outline_step = 0.35f;
    v.lod_force = -1;
    v.art_w = art_w;
    v.art_h = art_h;
    v.dither = dither;
    // art pixel (0,0) (lower-left, y-up) sits at lattice index (nx - art_w/2, ny - art_h/2)
    const long px0 = (long)nx - art_w / 2, py0 = (long)ny - art_h / 2;
    v.phase_x = (int)(((px0 % 4) + 4) % 4);
    v.phase_y = (int)(((py0 % 4) + 4) % 4);
    return v;
}

// ---------------------------------------------------------------------------
// GPU state

typedef struct {
    int w, h;
    sg_image color, depth;           // depth: normalised depth in .r (second colour target)
    sg_image zbuf;                   // hardware depth
    sg_view color_att, depth_att, zbuf_att;
    sg_view color_tex, depth_tex;
} Splat_Target;

typedef struct {
    sg_buffer buf;
    uint32_t count;
    int bone_n;
} Splat_Skin_Model;

typedef struct {
    uint32_t start[SPLAT_LOD_LEVELS], count[SPLAT_LOD_LEVELS];   // splat ranges per level
    float lo[SPLAT_LOD_LEVELS][3], hi[SPLAT_LOD_LEVELS][3];      // per-level bounds, grown by that level's largest extent
} Splat_Chunk_Range;

struct Splat_Chunks {
    float size, x0, z0;
    int nx, nz;
    float **data;            // per chunk splat rows
    uint32_t *count, *cap;
    uint64_t total;
};

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
    sg_shader skin_shd;
    sg_pipeline skin_pip;
    sg_image bone_img;
    sg_view bone_tex;
    float *bone_data;              // SPLAT_MAX_SKIN_INSTANCES x (SPLAT_MAX_BONES * 12)
    Splat_Skin_Model models[16];
    int model_n;
    int inst_model[SPLAT_MAX_SKIN_INSTANCES];
    int inst_n;
    float scene_radius;
    Splat_Chunk_Range *chunks;    // NULL: the static cloud is one range
    int chunk_n;
    int chunks_drawn, skin_drawn, lod_level;
    uint64_t splats_drawn;
    uint64_t lod_splats[SPLAT_LOD_LEVELS];
    float inst_bounds[SPLAT_MAX_SKIN_INSTANCES][4];
} S;

static void target_destroy(Splat_Target *t) {
    if (t->w == 0) return;
    sg_destroy_view(t->color_att);
    sg_destroy_view(t->depth_att);
    sg_destroy_view(t->color_tex);
    sg_destroy_view(t->depth_tex);
    sg_destroy_view(t->zbuf_att);
    sg_destroy_image(t->color);
    sg_destroy_image(t->depth);
    sg_destroy_image(t->zbuf);
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
    t->zbuf = sg_make_image(&(sg_image_desc){
        .usage = {.depth_stencil_attachment = true},
        .width = w,
        .height = h,
        .pixel_format = SG_PIXELFORMAT_DEPTH,
        .sample_count = 1,
        .label = label,
    });
    t->zbuf_att = sg_make_view(&(sg_view_desc){.depth_stencil_attachment = {.image = t->zbuf}});
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
        // back-to-front order already resolves the static cloud; the depth test only keeps
        // what is drawn later (skinned instances) behind what stands in front of it
        .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
        .color_count = 2,
        .colors = {
            [0] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [1] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
        },
        .sample_count = 1,
        .label = "splat-pipeline",
    });
    S.skin_shd = sg_make_shader(splat_skin_shader_desc(sg_query_backend()));
    S.skin_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.skin_shd,
        .layout = {
            .buffers[0] = {.stride = SPLAT_FLOATS * 4, .step_func = SG_VERTEXSTEP_PER_INSTANCE},
            .attrs = {
                [ATTR_splat_skin_a_pos] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 0},
                [ATTR_splat_skin_a_cov0] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 16},
                [ATTR_splat_skin_a_cov1] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 32},
                [ATTR_splat_skin_a_bone] = {.format = SG_VERTEXFORMAT_FLOAT4, .offset = 48},
            },
        },
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
        .color_count = 2,
        .colors = {
            [0] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [1] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
        },
        .sample_count = 1,
        .label = "splat-skin-pipeline",
    });
    S.bone_img = sg_make_image(&(sg_image_desc){
        .usage = {.stream_update = true},
        .width = SPLAT_MAX_BONES * 3,
        .height = SPLAT_MAX_SKIN_INSTANCES,
        .pixel_format = SG_PIXELFORMAT_RGBA32F,
        .label = "splat-bones",
    });
    S.bone_tex = sg_make_view(&(sg_view_desc){.texture = {.image = S.bone_img}});
    S.bone_data = calloc((size_t)SPLAT_MAX_SKIN_INSTANCES * SPLAT_MAX_BONES * 12, sizeof(float));
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
    // a single cloud replaces any chunk world
    free(S.chunks);
    S.chunks = NULL;
    S.chunk_n = 0;
    S.instances = sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = {.ptr = sorted, .size = bytes},
        .label = "splat-instances",
    });
    free(sorted);
    S.count = c->count;
    S.scene_radius = c->radius;
    splat_set_palette(c->pal, (int)c->pal_n);
}

Splat_Chunks *splat_chunks_new(float chunk_size, float x0, float z0, int nx, int nz) {
    if (chunk_size <= 0 || nx <= 0 || nz <= 0) SPLAT_FATAL("[splat] bad chunk grid\n");
    Splat_Chunks *ch = calloc(1, sizeof(*ch));
    ch->size = chunk_size;
    ch->x0 = x0;
    ch->z0 = z0;
    ch->nx = nx;
    ch->nz = nz;
    ch->data = calloc((size_t)nx * nz, sizeof(float *));
    ch->count = calloc((size_t)nx * nz, sizeof(uint32_t));
    ch->cap = calloc((size_t)nx * nz, sizeof(uint32_t));
    return ch;
}

void splat_chunks_add(Splat_Chunks *ch, const Splat_Cloud *proto, const float off[3]) {
    if (proto->bone_n) SPLAT_FATAL("[splat] skinned cloud cannot be placed into static chunks\n");
    for (uint32_t i = 0; i < proto->count; i++) {
        const float *d = proto->data + (size_t)i * SPLAT_FLOATS;
        const float x = d[0] + off[0], z = d[2] + off[2];
        int cx = (int)floorf((x - ch->x0) / ch->size), cz = (int)floorf((z - ch->z0) / ch->size);
        if (cx < 0 || cz < 0 || cx >= ch->nx || cz >= ch->nz) SPLAT_FATAL("[splat] splat at %.2f,%.2f outside the chunk grid\n", x, z);
        const int k = cz * ch->nx + cx;
        if (ch->count[k] == ch->cap[k]) {
            ch->cap[k] = ch->cap[k] ? ch->cap[k] * 2 : 4096;
            ch->data[k] = realloc(ch->data[k], (size_t)ch->cap[k] * SPLAT_FLOATS * sizeof(float));
            if (!ch->data[k]) SPLAT_FATAL("[splat] out of memory growing chunk %d\n", k);
        }
        float *o = ch->data[k] + (size_t)ch->count[k] * SPLAT_FLOATS;
        memcpy(o, d, SPLAT_FLOATS * sizeof(float));
        o[0] = x;
        o[1] = d[1] + off[1];
        o[2] = z;
        ch->count[k]++;
    }
    ch->total += proto->count;
}

uint64_t splat_chunks_count(const Splat_Chunks *ch) { return ch->total; }

typedef struct {
    float key;
    int idx;
} Chunk_Order;

static int cmp_chunk(const void *a, const void *b) {
    float ka = ((const Chunk_Order *)a)->key, kb = ((const Chunk_Order *)b)->key;
    return ka < kb ? -1 : ka > kb ? 1 : ((const Chunk_Order *)a)->idx - ((const Chunk_Order *)b)->idx;
}

// LOD merge: splats grouped by grid cell; each group becomes one moment-matched splat.
// The in-plane spread of a cell full of small splats is about cell^2 / 12 per axis, which leaves
// gaps once the solid cut keeps only the core (exp(pw) > 0.5, i.e. r < 1.18 sigma); the spread
// is widened by LOD_INFLATE so merged neighbours close up again.
#define LOD_INFLATE 4.0f

typedef struct {
    uint64_t key;
    uint32_t idx;
} Cell_Item;

static int cmp_cell(const void *a, const void *b) {
    uint64_t ka = ((const Cell_Item *)a)->key, kb = ((const Cell_Item *)b)->key;
    return ka < kb ? -1 : ka > kb;
}

// merges `src` (n rows) on cells of `cell`, writes rows to `dst` (capacity n), returns count
static uint32_t lod_merge(const float *src, uint32_t n, float cell, float *dst) {
    Cell_Item *it = malloc(sizeof(Cell_Item) * n);
    for (uint32_t i = 0; i < n; i++) {
        const float *d = src + (size_t)i * SPLAT_FLOATS;
        const int64_t cx = (int64_t)floorf(d[0] / cell) + (1 << 20), cy = (int64_t)floorf(d[1] / cell) + (1 << 20),
                      cz = (int64_t)floorf(d[2] / cell) + (1 << 20);
        it[i].key = ((uint64_t)cx << 42) | ((uint64_t)cy << 21) | (uint64_t)cz;
        it[i].idx = i;
    }
    qsort(it, n, sizeof(Cell_Item), cmp_cell);
    uint32_t out = 0;
    for (uint32_t a = 0; a < n;) {
        uint32_t b = a;
        while (b < n && it[b].key == it[a].key) b++;
        double w = 0, m[3] = {0, 0, 0}, col[3] = {0, 0, 0}, al = 0;
        for (uint32_t k = a; k < b; k++) {
            const float *d = src + (size_t)it[k].idx * SPLAT_FLOATS;
            const double wk = 1.0;
            w += wk;
            for (int q = 0; q < 3; q++) m[q] += wk * d[q];
            const uint32_t pk = (uint32_t)d[7];
            col[0] += wk * (pk % 256);
            col[1] += wk * ((pk / 256) % 256);
            col[2] += wk * (pk / 65536);
            if (d[3] > al) al = d[3];
        }
        for (int q = 0; q < 3; q++) m[q] /= w;
        double c[6] = {0, 0, 0, 0, 0, 0};  // c00 c01 c02 c11 c12 c22
        for (uint32_t k = a; k < b; k++) {
            const float *d = src + (size_t)it[k].idx * SPLAT_FLOATS;
            const double dx = d[0] - m[0], dy = d[1] - m[1], dz = d[2] - m[2];
            c[0] += d[4] + dx * dx;
            c[1] += d[5] + dx * dy;
            c[2] += d[6] + dx * dz;
            c[3] += d[8] + dy * dy;
            c[4] += d[9] + dy * dz;
            c[5] += d[10] + dz * dz;
        }
        float *o = dst + (size_t)out * SPLAT_FLOATS;
        memset(o, 0, SPLAT_FLOATS * sizeof(float));
        o[0] = (float)m[0];
        o[1] = (float)m[1];
        o[2] = (float)m[2];
        o[3] = (float)al;
        // the cell's own extent bounds the spread a single member contributes
        const double base = (double)cell * cell / 12.0;
        const double f = LOD_INFLATE / w;
        o[4] = (float)(c[0] * f + (b - a == 1 ? base : 0));
        o[5] = (float)(c[1] * f);
        o[6] = (float)(c[2] * f);
        o[8] = (float)(c[3] * f + (b - a == 1 ? base : 0));
        o[9] = (float)(c[4] * f);
        o[10] = (float)(c[5] * f + (b - a == 1 ? base : 0));
        const uint32_t r = (uint32_t)lround(col[0] / w), g = (uint32_t)lround(col[1] / w), bl = (uint32_t)lround(col[2] / w);
        o[7] = (float)(r + g * 256 + bl * 65536);
        o[11] = 0;
        out++;
        a = b;
    }
    free(it);
    return out;
}

void splat_chunks_upload(Splat_Chunks *ch, const float V[16], const uint32_t *pal, int pal_n, float radius) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_chunks_upload before splat_init\n");
    if (ch->total == 0) SPLAT_FATAL("[splat] empty chunk world\n");
    if (ch->total * SPLAT_FLOATS * 4 > 0x7fffffffull) SPLAT_FATAL("[splat] %llu splats exceed one 2 GB buffer\n", (unsigned long long)ch->total);
    const int n = ch->nx * ch->nz;
    Chunk_Order *order = malloc(sizeof(Chunk_Order) * (size_t)n);
    int used = 0;
    for (int k = 0; k < n; k++) {
        if (!ch->count[k]) continue;
        const float cx = ch->x0 + ((float)(k % ch->nx) + 0.5f) * ch->size, cz = ch->z0 + ((float)(k / ch->nx) + 0.5f) * ch->size;
        order[used].key = V[2] * cx + V[10] * cz + V[14];
        order[used].idx = k;
        used++;
    }
    qsort(order, (size_t)used, sizeof(Chunk_Order), cmp_chunk);
    // level sizes first (merging is deterministic, so it runs once here and once below)
    float **lev = calloc((size_t)n * SPLAT_LOD_LEVELS, sizeof(float *));
    uint32_t *lev_n = calloc((size_t)n * SPLAT_LOD_LEVELS, sizeof(uint32_t));
    uint64_t total = 0;
    memset(S.lod_splats, 0, sizeof(S.lod_splats));
    for (int c = 0; c < used; c++) {
        const int k = order[c].idx;
        lev[k * SPLAT_LOD_LEVELS] = ch->data[k];
        lev_n[k * SPLAT_LOD_LEVELS] = ch->count[k];
        for (int l = 1; l < SPLAT_LOD_LEVELS; l++) {
            const float *src = lev[k * SPLAT_LOD_LEVELS + l - 1];
            const uint32_t sn = lev_n[k * SPLAT_LOD_LEVELS + l - 1];
            float *dst = malloc((size_t)sn * SPLAT_FLOATS * sizeof(float));
            const uint32_t dn = lod_merge(src, sn, SPLAT_LOD_CELL0 * (float)(1 << (l - 1)), dst);
            lev[k * SPLAT_LOD_LEVELS + l] = realloc(dst, (size_t)dn * SPLAT_FLOATS * sizeof(float));
            lev_n[k * SPLAT_LOD_LEVELS + l] = dn;
        }
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
            total += lev_n[k * SPLAT_LOD_LEVELS + l];
            S.lod_splats[l] += lev_n[k * SPLAT_LOD_LEVELS + l];
        }
    }
    if (total * SPLAT_FLOATS * 4 > 0x7fffffffull) SPLAT_FATAL("[splat] %llu splats with levels exceed one 2 GB buffer\n", (unsigned long long)total);
    const size_t bytes = (size_t)total * SPLAT_FLOATS * 4;
    float *all = malloc(bytes);
    if (!all) SPLAT_FATAL("[splat] out of memory for %zu bytes\n", bytes);
    free(S.chunks);
    S.chunks = calloc((size_t)used, sizeof(Splat_Chunk_Range));
    S.chunk_n = used;
    uint32_t at = 0;
    Sort_Item *items = NULL;
    uint32_t items_cap = 0;
    for (int c = 0; c < used; c++) {
        const int k = order[c].idx;
        Splat_Chunk_Range *r = &S.chunks[c];
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
            float *lo = r->lo[l], *hi = r->hi[l];
            for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
            float ext = 0;
            const float *src = lev[k * SPLAT_LOD_LEVELS + l];
            const uint32_t cnt = lev_n[k * SPLAT_LOD_LEVELS + l];
            if (cnt > items_cap) {
                items_cap = cnt;
                items = realloc(items, sizeof(Sort_Item) * cnt);
            }
            r->start[l] = at;
            r->count[l] = cnt;
            for (uint32_t i = 0; i < cnt; i++) {
                const float *d = src + (size_t)i * SPLAT_FLOATS;
                items[i].key = V[2] * d[0] + V[6] * d[1] + V[10] * d[2] + V[14];
                items[i].idx = i;
                for (int a = 0; a < 3; a++) {
                    if (d[a] < lo[a]) lo[a] = d[a];
                    if (d[a] > hi[a]) hi[a] = d[a];
                }
                const float e = 3.0f * sqrtf(fmaxf(d[4], fmaxf(d[8], d[10])));
                if (e > ext) ext = e;
            }
            for (int a = 0; a < 3; a++) { lo[a] -= ext; hi[a] += ext; }
            qsort(items, cnt, sizeof(Sort_Item), cmp_key);
            for (uint32_t i = 0; i < cnt; i++)
                memcpy(all + (size_t)(at + i) * SPLAT_FLOATS, src + (size_t)items[i].idx * SPLAT_FLOATS, SPLAT_FLOATS * 4);
            at += cnt;
            free(lev[k * SPLAT_LOD_LEVELS + l]);
        }
        ch->data[k] = NULL;
    }
    free(lev);
    free(lev_n);
    free(items);
    free(order);
    if (S.count) sg_destroy_buffer(S.instances);
    S.instances = sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = {.ptr = all, .size = bytes},
        .label = "splat-chunks",
    });
    free(all);
    S.count = (uint32_t)ch->total;
    S.scene_radius = radius;
    splat_set_palette(pal, pal_n);
    LOG_INFO("[splat] uploaded %llu splats in %d chunks with %d levels: %llu rows (%.1f MB); per level %llu %llu %llu %llu %llu %llu\n",
             (unsigned long long)ch->total, used, SPLAT_LOD_LEVELS, (unsigned long long)total, (double)bytes / 1048576.0,
             (unsigned long long)S.lod_splats[0], (unsigned long long)S.lod_splats[1], (unsigned long long)S.lod_splats[2],
             (unsigned long long)S.lod_splats[3], (unsigned long long)S.lod_splats[4], (unsigned long long)S.lod_splats[5]);
    free(ch->data);
    free(ch->count);
    free(ch->cap);
    free(ch);
}

// does a world-space box reach the target? (orthographic projection with the pass's view)
static bool box_visible(const float lo[3], const float hi[3], const float *V, double focal, float cx, float cy, int w, int h) {
    float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
    for (int i = 0; i < 8; i++) {
        const float p[3] = {(i & 1) ? hi[0] : lo[0], (i & 2) ? hi[1] : lo[1], (i & 4) ? hi[2] : lo[2]};
        const float sx = (float)(focal * (V[0] * p[0] + V[4] * p[1] + V[8] * p[2])) + cx;
        const float sy = (float)(focal * (V[1] * p[0] + V[5] * p[1] + V[9] * p[2])) + cy;
        mnx = fminf(mnx, sx); mxx = fmaxf(mxx, sx);
        mny = fminf(mny, sy); mxy = fmaxf(mxy, sy);
    }
    return mxx >= -2.0f && mnx <= (float)w + 2.0f && mxy >= -2.0f && mny <= (float)h + 2.0f;
}

int splat_skin_add_model(const Splat_Cloud *c) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_skin_add_model before splat_init\n");
    if (S.model_n >= (int)(sizeof(S.models) / sizeof(S.models[0]))) SPLAT_FATAL("[splat] too many skinned models\n");
    if (c->bone_n == 0 || c->bone_n > SPLAT_MAX_BONES) SPLAT_FATAL("[splat] skinned model needs 1..%d bones, has %u\n", SPLAT_MAX_BONES, c->bone_n);
    for (uint32_t i = 0; i < c->count; i++) {
        const float *d = c->data + (size_t)i * SPLAT_FLOATS;
        if ((int)lroundf(d[11]) != 12) SPLAT_FATAL("[splat] skinned model splat %u is kind %d, not 12\n", i, (int)lroundf(d[11]));
        if (d[12] < 0 || d[12] >= (float)c->bone_n || d[13] < 0 || d[13] >= (float)c->bone_n)
            SPLAT_FATAL("[splat] skinned model splat %u has bone %g/%g of %u\n", i, d[12], d[13], c->bone_n);
    }
    Splat_Skin_Model *m = &S.models[S.model_n];
    m->buf = sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = {.ptr = c->data, .size = (size_t)c->count * SPLAT_FLOATS * 4},
        .label = "splat-skin-model",
    });
    m->count = c->count;
    m->bone_n = (int)c->bone_n;
    if (S.pal_n == 0) splat_set_palette(c->pal, (int)c->pal_n);
    if (c->radius > S.scene_radius) S.scene_radius = c->radius;
    return S.model_n++;
}

int splat_skin_add_instance(int model) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    if (S.inst_n >= SPLAT_MAX_SKIN_INSTANCES) SPLAT_FATAL("[splat] more than %d skinned instances\n", SPLAT_MAX_SKIN_INSTANCES);
    S.inst_model[S.inst_n] = model;
    return S.inst_n++;
}

void splat_skin_set_bones(int instance, const float *bones, int bone_n) {
    if (instance < 0 || instance >= S.inst_n) SPLAT_FATAL("[splat] bad skinned instance %d\n", instance);
    if (bone_n != S.models[S.inst_model[instance]].bone_n) SPLAT_FATAL("[splat] instance %d: %d bones given, model has %d\n", instance, bone_n, S.models[S.inst_model[instance]].bone_n);
    float *row = S.bone_data + (size_t)instance * SPLAT_MAX_BONES * 12;
    for (int b = 0; b < bone_n; b++) {
        const float *m = bones + b * 12;
        float *t = row + b * 12;  // three texels: [r00 r01 r02 tx] [r10 r11 r12 ty] [r20 r21 r22 tz]
        t[0] = m[0]; t[1] = m[1]; t[2] = m[2]; t[3] = m[9];
        t[4] = m[3]; t[5] = m[4]; t[6] = m[5]; t[7] = m[10];
        t[8] = m[6]; t[9] = m[7]; t[10] = m[8]; t[11] = m[11];
    }
}

void splat_skin_set_bounds(int instance, const float center[3], float radius) {
    if (instance < 0 || instance >= S.inst_n) SPLAT_FATAL("[splat] bad skinned instance %d\n", instance);
    S.inst_bounds[instance][0] = center[0];
    S.inst_bounds[instance][1] = center[1];
    S.inst_bounds[instance][2] = center[2];
    S.inst_bounds[instance][3] = radius;
}

void splat_bones_place(float *out, const float *frame, int bone_n, float yaw, const float pos[3]) {
    const float c = cosf(yaw), s = sinf(yaw);
    // P = rotation about +y by yaw (maps +x to (c, 0, -s)), then translation pos
    const float P[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    for (int b = 0; b < bone_n; b++) {
        const float *m = frame + b * 12;
        float *o = out + b * 12;
        for (int r = 0; r < 3; r++) {
            for (int k = 0; k < 3; k++)
                o[r * 3 + k] = P[r * 3] * m[k] + P[r * 3 + 1] * m[3 + k] + P[r * 3 + 2] * m[6 + k];
            o[9 + r] = P[r * 3] * m[9] + P[r * 3 + 1] * m[10] + P[r * 3 + 2] * m[11] + pos[r];
        }
    }
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
    // screen position = focal * (axis . world) + C, with C carrying the view translation
    // (same as cli/render.js: ((p.x / (oh * asp)) * 0.5 + 0.5) * W, since W / (2 oh asp) = focal)
    const double focal = (double)t->h / 2.0 / (double)v->ortho_half_h;
    const float cx = (float)(focal * (double)v->view[12] + (double)t->w / 2.0);
    const float cy = (float)(focal * (double)v->view[13] + (double)t->h / 2.0);
    p.cam = (Splat_Vec4){{cx, cy, (float)t->w, (float)t->h}};
    p.dep = (Splat_Vec4){{v->eye_dist, v->radius, (float)focal, v->near_dist}};
    const float zrad = S.scene_radius > v->radius ? S.scene_radius : v->radius;
    p.zr = (Splat_Vec4){{v->eye_dist - zrad - 1.0f, 1.0f / (2.0f * zrad + 2.0f), 0, 0}};
    fs_splat_params_t fp = {.frag = {{cx, cy, (float)t->h, sg_query_features().origin_top_left ? 1.0f : 0.0f}}};
    float *bg = S.pal[0];
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {bg[0], bg[1], bg[2], 0.0f}},
            .colors[1] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {1.0f, 0.0f, 0.0f, 0.0f}},
        },
        .attachments = {.colors = {t->color_att, t->depth_att}, .depth_stencil = t->zbuf_att},
        .label = "splat-pass",
    });
    if (S.count) {
        fp.mode = (Splat_Vec4){{v->static_cut ? 1.0f : 0.0f, 0, 0, 0}};
        sg_apply_pipeline(S.splat_pip);
        sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
        sg_apply_uniforms(UB_fs_splat_params, &SG_RANGE(fp));
        if (!S.chunks) {
            sg_apply_bindings(&(sg_bindings){.vertex_buffers[0] = S.instances});
            sg_draw(0, 4, (int)S.count);
            S.splats_drawn = S.count;
        } else {
            // coarsest level whose merge cell is no larger than one pixel of this target
            const float px_world = (float)(1.0 / focal);
            int lvl = 0;
            for (int l = 1; l < SPLAT_LOD_LEVELS; l++)
                if (SPLAT_LOD_CELL0 * (float)(1 << (l - 1)) <= px_world) lvl = l;
            if (v->lod_force >= 0) lvl = v->lod_force < SPLAT_LOD_LEVELS ? v->lod_force : SPLAT_LOD_LEVELS - 1;
            S.lod_level = lvl;
            int drawn = 0;
            uint64_t sd = 0;
            for (int c = 0; c < S.chunk_n; c++) {
                const Splat_Chunk_Range *r = &S.chunks[c];
                if (!r->count[lvl] || !box_visible(r->lo[lvl], r->hi[lvl], v->view, focal, cx, cy, t->w, t->h)) continue;
                sg_apply_bindings(&(sg_bindings){
                    .vertex_buffers[0] = S.instances,
                    .vertex_buffer_offsets[0] = (int)((size_t)r->start[lvl] * SPLAT_FLOATS * 4),
                });
                sg_draw(0, 4, (int)r->count[lvl]);
                drawn++;
                sd += r->count[lvl];
            }
            S.chunks_drawn = drawn;
            S.splats_drawn = sd;
        }
    }
    if (S.inst_n) {
        fp.mode = (Splat_Vec4){{1, 0, 0, 0}};
        sg_apply_pipeline(S.skin_pip);
        int sk = 0;
        for (int i = 0; i < S.inst_n; i++) {
            const float *b = S.inst_bounds[i];
            if (b[3] > 0) {
                const float lo[3] = {b[0] - b[3], b[1] - b[3], b[2] - b[3]}, hi[3] = {b[0] + b[3], b[1] + b[3], b[2] + b[3]};
                if (!box_visible(lo, hi, v->view, focal, cx, cy, t->w, t->h)) continue;
            }
            sk++;
            const Splat_Skin_Model *m = &S.models[S.inst_model[i]];
            p.zr.v[2] = (float)i;
            sg_apply_bindings(&(sg_bindings){
                .vertex_buffers[0] = m->buf,
                .views[VIEW_bone_tex] = S.bone_tex,
                .samplers[SMP_bone_smp] = S.nearest,
            });
            sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
            sg_apply_uniforms(UB_fs_splat_params, &SG_RANGE(fp));
            sg_draw(0, 4, (int)m->count);
        }
        S.skin_drawn = sk;
    }
    sg_end_pass();
}

void splat_render_offscreen(void) {
    if (!S.ready || !S.have_view || (S.count == 0 && S.inst_n == 0)) return;
    ensure_targets(S.view.art_w, S.view.art_h);
    if (S.inst_n) {
        sg_update_image(S.bone_img, &(sg_image_data){.mip_levels[0] = {
            .ptr = S.bone_data,
            .size = (size_t)SPLAT_MAX_SKIN_INSTANCES * SPLAT_MAX_BONES * 12 * sizeof(float),
        }});
    }
    draw_splats(&S.samples);
    draw_splats(&S.native);

    const float flip = sg_query_features().origin_top_left ? 1.0f : 0.0f;
    fs_resolve_params_t rp;
    memset(&rp, 0, sizeof(rp));
    memcpy(rp.pal, S.pal, sizeof(float) * 4 * (size_t)S.pal_n);
    rp.opts = (Splat_Vec4){{(float)S.pal_n, S.view.dither, flip, S.view.outline}};
    rp.size = (Splat_Vec4){{(float)S.art_w, (float)S.art_h, (float)S.samples.w, (float)S.samples.h}};
    rp.phase = (Splat_Vec4){{(float)S.view.phase_x, (float)S.view.phase_y,
                             S.view.outline_step / (2.0f * S.view.radius), 0}};
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
    if (!S.ready || !S.have_view || (S.count == 0 && S.inst_n == 0) || S.art_w == 0) return;
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
    if (S.view.scale > 0) k = show == SPLAT_SHOW_SAMPLES ? S.view.scale / 2 : S.view.scale;
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
    uint32_t sk = 0;
    for (int i = 0; i < S.inst_n; i++) sk += S.models[S.inst_model[i]].count;
    return (Splat_Stats){.splats = S.count, .skinned_splats = sk, .skin_instances = S.inst_n,
                         .chunks = S.chunk_n, .chunks_drawn = S.chunks_drawn, .splats_drawn = S.splats_drawn,
                         .skin_drawn = S.skin_drawn, .lod_level = S.lod_level,
                         .art_w = S.art_w, .art_h = S.art_h, .scale = S.last_scale};
}
