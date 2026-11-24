// Portions follow ink-studio by siliconjungle (MIT, Copyright (c) 2026 siliconjungle; full
// notice in LICENSE.ink-studio next to this file): the splat data layout, the orthographic
// projection and falloff of cli/render.js.
// splat.c — see splat.h.
//
// Rendering follows the ink-studio CPU reference renderer (cli/render.js) for the
// orthographic case: per-splat covariance projection with a 0.15 px^2 low-pass, 3-sigma
// quads, exp() falloff cut at -4.5, solid splats sharpened with smoothstep(0.38, 0.62),
// back-to-front alpha blending, and a normalised depth blended alongside for the resolve.

#include "splat.h"
#include "artmesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    if (rd_u32(buf + 4) != 1 && rd_u32(buf + 4) != 2) SPLAT_FATAL("[splat] %s: unsupported version %u\n", path, rd_u32(buf + 4));

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

// Arc-style front plates have the eyes one quarter of the way down and the collar at
// two fifths. Give the brow less space and the face more; below the collar the plate is 1:1.
static int portrait_row(int row, int height) {
    const float t = (float)row / (float)(height - 1);
    float src = t;
    if (t < 0.16f) src = t * (0.25f / 0.16f);
    else if (t < 0.40f) src = 0.25f + (t - 0.16f) * (0.15f / 0.24f);
    return (int)lroundf(src * (float)(height - 1));
}

// see splat.h; the dab shape follows ink-studio's builder disc(): Sigma = st^2 I + (sn^2 - st^2) n n^T
static Splat_Cloud *cloud_from_views(const uint8_t *const views[4], int img_w, int img_h, int step,
                                    float world_per_px, int alpha_cut, int split_row,
                                    const uint32_t *pal, int pal_n, bool front_panel) {
    if (step < 1 || img_w < 2 || img_h < 2 || pal_n < 1 || pal_n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] bad from_views parameters\n");
    const float st = 0.95f * world_per_px * (float)step, sn = 0.51f * st, pi2 = 6.2831853f;
    // The portrait side plate is shallower so a diagonal heading still shows both cheeks.
    const float profile_depth = front_panel ? 0.63f : 1.0f;
    // per sampled row: centre and half-width of the opaque run in each view (half < 0 = no ring)
    const int rows = img_h / step + 1;
    float *mid = malloc(sizeof(float) * 4 * (size_t)rows), *half = malloc(sizeof(float) * 4 * (size_t)rows);
    for (int ri = 0; ri < rows; ri++) {
        const int j = ri * step;
        int ok = j > 0 && j < img_h;
        const int src_j = front_panel && ok ? portrait_row(j, img_h) : j;
        for (int v = 0; v < 4 && ok; v++) {
            int l = img_w, r = -1;
            for (int k = 0; k < img_w; k++)
                if (views[v][((size_t)src_j * img_w + k) * 4 + 3] > alpha_cut) {
                    if (k < l) l = k;
                    r = k;
                }
            mid[ri * 4 + v] = 0.5f * (float)(l + r);
            half[ri * 4 + v] = 0.5f * (float)(r - l);
            if (r < 0 || half[ri * 4 + v] < 0.6f) ok = 0;
        }
        if (!ok) half[ri * 4] = -1.0f;
    }
    size_t cap = 4096, n = 0;
    float *d = malloc(cap * SPLAT_FLOATS * sizeof(float));
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    // one dab: position, normal, colour (RGB packed low byte red), bone
    #define FV_PUSH(P, NV, RGB, BONE) do { \
        if (n == cap) { cap *= 2; d = realloc(d, cap * SPLAT_FLOATS * sizeof(float)); if (!d) SPLAT_FATAL("[splat] from_views: out of memory\n"); } \
        float *o = d + n * SPLAT_FLOATS; const float dd = sn * sn - st * st; \
        o[0] = (P)[0]; o[1] = (P)[1]; o[2] = (P)[2]; o[3] = 4.0f; \
        o[4] = st * st + dd * (NV)[0] * (NV)[0]; o[5] = dd * (NV)[0] * (NV)[1]; o[6] = dd * (NV)[0] * (NV)[2]; o[7] = (float)(RGB); \
        o[8] = st * st + dd * (NV)[1] * (NV)[1]; o[9] = dd * (NV)[1] * (NV)[2]; o[10] = st * st + dd * (NV)[2] * (NV)[2]; o[11] = 12.0f; \
        o[12] = (BONE); o[13] = (BONE); o[14] = 0.0f; o[15] = 0.0f; \
        for (int c_ = 0; c_ < 3; c_++) { if ((P)[c_] < lo[c_]) lo[c_] = (P)[c_]; if ((P)[c_] > hi[c_]) hi[c_] = (P)[c_]; } \
        n++; } while (0)
    // the lowest ring gets a sole: the figure is a shell, and one that is laid down or carried shows
    // its open underside
    int last = -1;
    for (int ri = 1; ri < rows; ri++)
        if (half[ri * 4] >= 0) last = ri;
    for (int ri = 1; ri < rows; ri++) {
        if (half[ri * 4] < 0) continue;
        const int j = ri * step;
        const int src_j = front_panel ? portrait_row(j, img_h) : j;
        const float *M = mid + ri * 4, *H = half + ri * 4;
        const float rx = 0.5f * (H[0] + H[2]) * world_per_px, rz = 0.5f * (H[1] + H[3]) * world_per_px * profile_depth;
        const float y = (float)(img_h - 1 - j) * world_per_px;
        const float bone = j < split_row ? 1.0f : 0.0f;
        // the ring above (one sampled row up the image): where it is narrower, this row's top is
        // seen from above and gets a ledge of upward dabs between the two outlines
        const int up = ri - 1;
        float urx = 0.0f, urz = 0.0f;
        if (up >= 1 && half[up * 4] >= 0) {
            urx = 0.5f * (half[up * 4] + half[up * 4 + 2]) * world_per_px;
            urz = 0.5f * (half[up * 4 + 1] + half[up * 4 + 3]) * world_per_px * profile_depth;
        }
        // dabs round the ring about one dab width apart
        int ring = (int)ceilf(pi2 * fmaxf(rx, rz) / (0.9f * st));
        // Keep the diagonals in register from row to row; a coarse cheek still needs
        // enough samples across the painted plate for the two eyes to stay separate.
        if (front_panel && src_j < img_h * 2 / 5) ring = ((ring * 3 / 2 + 7) / 8) * 8;
        ring = ring < 16 ? 16 : ring > 256 ? 256 : ring;
        for (int k = 0; k < ring; k++) {
            const float a = pi2 * (float)k / (float)ring, dx = cosf(a), dz = sinf(a);
            // the facing front/back view and side view, and where the point falls across each (-1..1)
            const int fv = dz < 0 ? 0 : 2, sv = dx > 0 ? 1 : 3;
            // A rounded rectangular section gives both eyes room on a near-flat cheek.
            // x^4 + z^4 = 1; the generic shell remains elliptical.
            const float ux = dx * dx, uz = dz * dz;
            const float gain = front_panel ? 1.0f / sqrtf(sqrtf(ux * ux + uz * uz)) : 1.0f;
            const float ft = (dz < 0 ? dx : -dx) * gain, stt = (dx > 0 ? dz : -dz) * gain;
            // On a portrait keep each eye, fringe and collar on the same painted panel.
            // Feather into the true profile only at the temple. Back views retain their own plate.
            float w = dx * dx;
            if (front_panel) {
                const float t = fminf(1.0f, fmaxf(0.0f, (fabsf(dx) - 0.80f) / 0.17f));
                w = t * t * (3.0f - 2.0f * t);
            }
            const int fk = (int)lroundf(M[fv] + H[fv] * ft), sk = (int)lroundf(M[sv] + H[sv] * stt);
            const uint8_t *fp = views[fv] + ((size_t)src_j * img_w + fk) * 4, *sp = views[sv] + ((size_t)src_j * img_w + sk) * 4;
            if ((float)fp[3] * (1.0f - w) + (float)sp[3] * w < (float)alpha_cut) continue;
            uint32_t rgb = 0;
            for (int c = 0; c < 3; c++) {
                const uint32_t cc = (uint32_t)lroundf((float)fp[c] * (1.0f - w) + (float)sp[c] * w);
                rgb |= (cc > 255 ? 255 : cc) << (8 * c);
            }
            const float nx = (front_panel ? dx * ux : dx) * rz, nz = (front_panel ? dz * uz : dz) * rx;
            const float nl = sqrtf(nx * nx + nz * nz);
            const float nv[3] = {nl > 0 ? nx / nl : dx, 0.0f, nl > 0 ? nz / nl : dz};
            const float p[3] = {gain * rx * dx, y, gain * rz * dz};
            FV_PUSH(p, nv, rgb, bone);
            // ledge: from the outline above (or the axis at the top) out to this ring
            const float in_r = gain * sqrtf(urx * urx * dx * dx + urz * urz * dz * dz), out_r = gain * sqrtf(rx * rx * dx * dx + rz * rz * dz * dz);
            const float upn[3] = {0.0f, 1.0f, 0.0f};
            // a ledge takes its colour a little further down the views: the outline runs along the
            // silhouette's top edge, and a dark rim would otherwise be laid over the whole top
            uint32_t lrgb = 0;
            if (out_r - st > in_r + 0.5f * st) {
                const int jj = src_j + 6 < img_h ? src_j + 6 : img_h - 1;
                const uint8_t *fq = views[fv] + ((size_t)jj * img_w + fk) * 4, *sq = views[sv] + ((size_t)jj * img_w + sk) * 4;
                for (int c = 0; c < 3; c++) {
                    const uint32_t cc = (uint32_t)lroundf((float)fq[c] * (1.0f - w) + (float)sq[c] * w);
                    lrgb |= (cc > 255 ? 255 : cc) << (8 * c);
                }
            }
            for (float rr = out_r - st; rr > in_r + 0.5f * st; rr -= st) {
                const float f = rr / out_r, q[3] = {p[0] * f, y, p[2] * f};
                FV_PUSH(q, upn, lrgb, bone);
                if (k % 2 && rr < out_r * 0.6f) break;  // fewer dabs towards the middle, where rings are dense
            }
            if (ri == last) {
                // sole: downward dabs from this ring in to the axis, darkened (it is in shade). The colour is
                // taken a little further up the views: the lowest row is the silhouette's dark outline
                const int jj = src_j - 10 > 0 ? src_j - 10 : src_j;
                const uint8_t *fq = views[fv] + ((size_t)jj * img_w + fk) * 4, *sq = views[sv] + ((size_t)jj * img_w + sk) * 4;
                const int lit = (float)fq[3] * (1.0f - w) + (float)sq[3] * w >= (float)alpha_cut;
                uint32_t srgb = 0;
                for (int c = 0; c < 3; c++) {
                    const float cc = lit ? (float)fq[c] * (1.0f - w) + (float)sq[c] * w : (float)((rgb >> (8 * c)) & 255u);
                    srgb |= (uint32_t)(cc * 0.72f) << (8 * c);
                }
                const float dn[3] = {0.0f, -1.0f, 0.0f};
                for (float rr = out_r - st; rr > 0.5f * st; rr -= st) {
                    const float f = rr / out_r, q[3] = {p[0] * f, y, p[2] * f};
                    FV_PUSH(q, dn, srgb, bone);
                    if (k % 2 && rr < out_r * 0.6f) break;
                }
            }
        }
    }
    #undef FV_PUSH
    free(mid);
    free(half);
    if (n == 0) {
        free(d);
        return NULL;
    }
    Splat_Cloud *c = calloc(1, sizeof(Splat_Cloud));
    c->count = (uint32_t)n;
    c->data = d;
    c->pal_n = (uint32_t)pal_n;
    memcpy(c->pal, pal, sizeof(uint32_t) * (size_t)pal_n);
    c->bone_n = 2;
    memcpy(c->bmin, lo, sizeof(lo));
    memcpy(c->bmax, hi, sizeof(hi));
    float r2 = 0;
    for (int k = 0; k < 8; k++) {
        const float x = (k & 1) ? hi[0] : lo[0], yy = (k & 2) ? hi[1] : lo[1], z = (k & 4) ? hi[2] : lo[2];
        const float q = x * x + yy * yy + z * z;
        if (q > r2) r2 = q;
    }
    c->radius = sqrtf(r2);
    return c;
}

Splat_Cloud *splat_cloud_from_views(const uint8_t *const views[4], int img_w, int img_h, int step,
                                    float world_per_px, int alpha_cut, int split_row,
                                    const uint32_t *pal, int pal_n) {
    return cloud_from_views(views, img_w, img_h, step, world_per_px, alpha_cut, split_row, pal, pal_n, false);
}

Splat_Cloud *splat_cloud_from_portrait(const uint8_t *const views[4], int img_w, int img_h, int step,
                                    float world_per_px, int alpha_cut, int split_row,
                                    const uint32_t *pal, int pal_n) {
    return cloud_from_views(views, img_w, img_h, step, world_per_px, alpha_cut, split_row, pal, pal_n, true);
}

Splat_Cloud *splat_cloud_from_icon(const uint8_t *rgba, int img_w, int img_h, int step, float world_per_px,
                                   float thickness, int alpha_cut, const uint32_t *pal, int pal_n) {
    if (step < 1 || img_w < 1 || img_h < 1 || pal_n < 1 || pal_n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] bad from_icon parameters\n");
    const float st = 0.95f * world_per_px * (float)step, sn = 0.45f * st;
    size_t cap = 4096, n = 0;
    float *d = malloc(cap * SPLAT_FLOATS * sizeof(float));
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    #define FI_OPAQUE(i, j) ((i) >= 0 && (j) >= 0 && (i) < img_w && (j) < img_h && rgba[((size_t)(j) * img_w + (i)) * 4 + 3] > alpha_cut)
    #define FI_PUSH(X, Y, Z, NX, NY, NZ, RGB) do { \
        if (n == cap) { cap *= 2; d = realloc(d, cap * SPLAT_FLOATS * sizeof(float)); if (!d) SPLAT_FATAL("[splat] from_icon: out of memory\n"); } \
        float *o = d + n * SPLAT_FLOATS; const float nv[3] = {NX, NY, NZ}, dd = sn * sn - st * st; \
        o[0] = X; o[1] = Y; o[2] = Z; o[3] = 4.0f; \
        o[4] = st * st + dd * nv[0] * nv[0]; o[5] = dd * nv[0] * nv[1]; o[6] = dd * nv[0] * nv[2]; o[7] = (float)(RGB); \
        o[8] = st * st + dd * nv[1] * nv[1]; o[9] = dd * nv[1] * nv[2]; o[10] = st * st + dd * nv[2] * nv[2]; \
        o[11] = 0.0f; o[12] = o[13] = o[14] = o[15] = 0.0f; \
        const float p_[3] = {X, Y, Z}; \
        for (int c_ = 0; c_ < 3; c_++) { if (p_[c_] < lo[c_]) lo[c_] = p_[c_]; if (p_[c_] > hi[c_]) hi[c_] = p_[c_]; } \
        n++; } while (0)
    for (int j = step / 2; j < img_h; j += step)
        for (int i = step / 2; i < img_w; i += step) {
            if (!FI_OPAQUE(i, j)) continue;
            const uint8_t *px = rgba + ((size_t)j * img_w + i) * 4;
            const uint32_t rgb = (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16);
            const float x = ((float)i + 0.5f - 0.5f * (float)img_w) * world_per_px;
            const float z = (0.5f * (float)img_h - (float)j - 0.5f) * world_per_px;
            FI_PUSH(x, thickness, z, 0.0f, 1.0f, 0.0f, rgb);
            // rim: on the silhouette's edge, facing outwards, a little darker
            const int e[4][2] = {{step, 0}, {-step, 0}, {0, step}, {0, -step}};
            for (int k = 0; k < 4; k++) {
                if (FI_OPAQUE(i + e[k][0], j + e[k][1])) continue;
                const float ox = e[k][0] > 0 ? 1.0f : e[k][0] < 0 ? -1.0f : 0.0f, oz = e[k][1] > 0 ? -1.0f : e[k][1] < 0 ? 1.0f : 0.0f;
                const uint32_t dark = (uint32_t)(px[0] * 0.7f) | ((uint32_t)(px[1] * 0.7f) << 8) | ((uint32_t)(px[2] * 0.7f) << 16);
                for (float y = thickness - st; y > -0.25f * st; y -= st)
                    FI_PUSH(x + ox * 0.5f * st, y < 0 ? 0 : y, z + oz * 0.5f * st, ox, 0.0f, oz, dark);
            }
        }
    #undef FI_PUSH
    #undef FI_OPAQUE
    if (n == 0) {
        free(d);
        return NULL;
    }
    Splat_Cloud *c = calloc(1, sizeof(Splat_Cloud));
    c->count = (uint32_t)n;
    c->data = d;
    c->pal_n = (uint32_t)pal_n;
    memcpy(c->pal, pal, sizeof(uint32_t) * (size_t)pal_n);
    memcpy(c->bmin, lo, sizeof(lo));
    memcpy(c->bmax, hi, sizeof(hi));
    float r2 = 0;
    for (int k = 0; k < 8; k++) {
        const float x = (k & 1) ? hi[0] : lo[0], yy = (k & 2) ? hi[1] : lo[1], z = (k & 4) ? hi[2] : lo[2];
        const float q = x * x + yy * yy + z * z;
        if (q > r2) r2 = q;
    }
    c->radius = sqrtf(r2);
    return c;
}

Splat_Cloud *splat_cloud_from_mesh(const float *pos, const uint8_t *rgb, int vert_n, const uint16_t *idx, int idx_n,
                                   float spacing, const float light[3], float ambient, const uint32_t *pal, int pal_n) {
    if (vert_n < 3 || idx_n < 3 || idx_n % 3 || !(spacing > 0) || pal_n < 1 || pal_n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] bad from_mesh parameters\n");
    size_t cap = 4096, n = 0;
    float *d = malloc(cap * SPLAT_FLOATS * sizeof(float));
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int t = 0; t < idx_n; t += 3) {
        const int ia = idx[t], ib = idx[t + 1], ic = idx[t + 2];
        if (ia >= vert_n || ib >= vert_n || ic >= vert_n) SPLAT_FATAL("[splat] from_mesh: index beyond %d vertices\n", vert_n);
        const float *a = pos + ia * 3, *b = pos + ib * 3, *c = pos + ic * 3;
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float nv[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float nl = sqrtf(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
        if (nl < 1e-12f) continue;  // degenerate
        for (int k = 0; k < 3; k++) nv[k] /= nl;
        if (nv[1] < -0.7f) continue;  // underside
        const float l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]), l2 = sqrtf(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
        const float e3[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]}, l3 = sqrtf(e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2]);
        const float lmax = l1 > l2 ? (l1 > l3 ? l1 : l3) : (l2 > l3 ? l2 : l3);
        int m = (int)ceilf(lmax / spacing);
        if (m < 1) m = 1;
        if (m > 512) SPLAT_FATAL("[splat] from_mesh: a %.2f long triangle edge at spacing %.4f\n", lmax, spacing);
        // dab size from the sample spacing actually reached on this triangle (area per sample)
        const float st = 0.95f * sqrtf(0.5f * nl / (float)(m * m)), sn = 0.45f * st, dd = sn * sn - st * st;
        const float ndl = nv[0] * light[0] + nv[1] * light[1] + nv[2] * light[2];
        const float shade = ambient + (1.0f - ambient) * (ndl > 0 ? ndl : 0);
        const uint8_t *ca = rgb + ia * 3, *cb = rgb + ib * 3, *cc = rgb + ic * 3;
        // the m*m small triangles of the subdivision, one dab at each one's centroid
        for (int i = 0; i < m; i++)
            for (int j = 0; j < m - i; j++)
                for (int up = 0; up < 2; up++) {
                    if (up && j >= m - i - 1) continue;
                    const float u = ((float)i + (up ? 2.0f : 1.0f) / 3.0f) / (float)m, v = ((float)j + (up ? 2.0f : 1.0f) / 3.0f) / (float)m, w = 1.0f - u - v;
                    if (n == cap) {
                        cap *= 2;
                        d = realloc(d, cap * SPLAT_FLOATS * sizeof(float));
                        if (!d) SPLAT_FATAL("[splat] from_mesh: out of memory\n");
                    }
                    float *o = d + n * SPLAT_FLOATS;
                    uint32_t col = 0;
                    for (int k = 0; k < 3; k++) {
                        o[k] = w * a[k] + u * b[k] + v * c[k];
                        if (o[k] < lo[k]) lo[k] = o[k];
                        if (o[k] > hi[k]) hi[k] = o[k];
                        const float cv = shade * (w * (float)ca[k] + u * (float)cb[k] + v * (float)cc[k]);
                        col |= (uint32_t)lroundf(cv > 255 ? 255 : cv) << (8 * k);
                    }
                    o[3] = 4.0f;
                    o[4] = st * st + dd * nv[0] * nv[0]; o[5] = dd * nv[0] * nv[1]; o[6] = dd * nv[0] * nv[2]; o[7] = (float)col;
                    o[8] = st * st + dd * nv[1] * nv[1]; o[9] = dd * nv[1] * nv[2]; o[10] = st * st + dd * nv[2] * nv[2];
                    o[11] = 0.0f; o[12] = o[13] = o[14] = o[15] = 0.0f;
                    n++;
                }
    }
    if (n == 0) {
        free(d);
        return NULL;
    }
    Splat_Cloud *c = calloc(1, sizeof(Splat_Cloud));
    c->count = (uint32_t)n;
    c->data = d;
    c->pal_n = (uint32_t)pal_n;
    memcpy(c->pal, pal, sizeof(uint32_t) * (size_t)pal_n);
    memcpy(c->bmin, lo, sizeof(lo));
    memcpy(c->bmax, hi, sizeof(hi));
    float r2 = 0;
    for (int k = 0; k < 8; k++) {
        const float x = (k & 1) ? hi[0] : lo[0], yy = (k & 2) ? hi[1] : lo[1], z = (k & 4) ? hi[2] : lo[2];
        const float q = x * x + yy * yy + z * z;
        if (q > r2) r2 = q;
    }
    c->radius = sqrtf(r2);
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
    sg_image surface;
    sg_view surface_att, surface_tex;
    sg_image normal;
    sg_view normal_att, normal_tex;
    sg_image color, depth;           // depth: normalised depth in .r (second colour target)
    sg_image zbuf;                   // hardware depth
    sg_view color_att, depth_att, zbuf_att;
    sg_view zbuf_tex;                // the hardware depth to sample (kept past the pass for the art grid only)
    sg_view color_tex, depth_tex;
} Splat_Target;

typedef struct {
    sg_buffer buf;
    uint32_t count;
    int bone_n;
    bool physical_pixels;
    int own;       // own palette (0 = the scene's)
    float *rows;   // blended models: the model's rows, which its instances are sorted from
    float lead;    // blended models: world units their hardware depth is brought towards the eye
} Splat_Skin_Model;

// Crop in physical pixels of the projected art image, origin bottom-left.
typedef struct {
    int instance, x, y;
    Splat_Target target;
} Splat_Detail;

// one blended instance's rows in drawing order
typedef struct {
    sg_buffer buf;
    uint32_t cap;   // rows the buffer holds
    int model;      // what the order was made for: this model, this view direction, these bones
    float za[3];
    float bones[SPLAT_MAX_BONES * 12];  // linear parts as given; translations relative to bone 0
    uint32_t fit;   // the last frame the instance was drawn with a model that fills at least half of the buffer
} Splat_Sorted;

typedef struct {
    uint32_t start[SPLAT_LOD_LEVELS], count[SPLAT_LOD_LEVELS];   // splat ranges per level
    float lo[SPLAT_LOD_LEVELS][3], hi[SPLAT_LOD_LEVELS][3];      // per-level bounds, grown by that level's largest extent
} Splat_Chunk_Range;

typedef struct {
    sg_buffer buf;
    bool live;
    uint32_t placed;
    Splat_Chunk_Range r;     // ranges within its own buffer
} Splat_World_Chunk;

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
    sg_pipeline splat_pip, resolve_pip, blit_pip, detail_pip, depth_view_pip;
    Splat_Detail details[SPLAT_MAX_SKIN_INSTANCES];
    bool detail_active[SPLAT_MAX_SKIN_INSTANCES];
    int detail_n;
    sg_image lamp_img;
    sg_view lamp_tex;
    int lamp_x, lamp_z, lamp_w, lamp_h;
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
    int bone_rows;                 // rows of bone_img: grows by doubling with the number of instances
    Splat_Skin_Model models[4608];
    int model_n;
    int inst_model[SPLAT_MAX_SKIN_INSTANCES];
    int inst_n;
    float scene_radius;
    Splat_Chunk_Range *chunks;    // NULL: the static cloud is one range
    int chunk_n;
    Splat_World_Chunk *world;     // non-NULL: the static cloud is a grid of chunks with a buffer each
    int world_nx, world_nz, world_live;
    int *world_order;             // grid indices back to front
    int chunks_drawn, skin_drawn, lod_level;
    int pass_lod[2];
    uint64_t pass_splats[2], pass_skinned[2];
    uint64_t splats_drawn;
    uint64_t lod_splats[SPLAT_LOD_LEVELS];
    float inst_bounds[SPLAT_MAX_SKIN_INSTANCES][4];
    float inst_tint[SPLAT_MAX_SKIN_INSTANCES][4];
    float occ[SPLAT_MAX_OCCLUDERS * SPLAT_OCCLUDER_FLOATS];
    int occ_n;
    float lod_cell0;  // level 1 merge cell of the uploaded world
    // own palettes: row k of the texture is palette k (row 0 unused)
    sg_image own_img;
    sg_view own_tex;
    uint8_t own_px[(SPLAT_MAX_OWN_PALETTES + 1) * SPLAT_MAX_PALETTE * 4];
    float own_opt[SPLAT_MAX_OWN_PALETTES + 1][2];  // size, dither
    int own_n;
    bool own_dirty;
    // blended instances
    Splat_Sorted *sorted[SPLAT_MAX_SKIN_INSTANCES];  // made when an instance is first drawn blended
    int blend_list[SPLAT_MAX_SKIN_INSTANCES], blend_n;  // this frame's, far first
    float *sort_depth, *sort_rows;                   // scratch for one model
    uint32_t sort_cap;
    uint32_t blend_frame;
    int sorted_now;
    uint64_t sorted_splats, sorted_bytes;
    double sort_ms;
} S;

static float s_lod_cell0 = SPLAT_LOD_CELL0;  // for worlds baked from now on (any thread reads it)
void splat_set_lod_cell0(float cell) {
    if (!(cell > 0.0f)) SPLAT_FATAL("[splat] lod cell %g\n", cell);
    s_lod_cell0 = cell;
}

Splat_View splat_view_oblique(float k, const float target[3], float art_world, int art_w, int art_h, float radius,
                              float dither) {
    if (art_world <= 0.0f || art_w <= 0 || art_h <= 0 || k < 0.0f) SPLAT_FATAL("[splat] bad oblique view parameters\n");
    const double oh = 0.5 * art_world * (double)art_h;
    const double dn = sqrt(1.0 + (double)k * k);
    // rows: screen x, screen y (up), view z (towards the viewer: up and south)
    const double xa[3] = {1, 0, 0}, ya[3] = {0, k, 1}, za[3] = {0, 1.0 / dn, -(double)k / dn};
    const double lat_off = 0.375;
    const double tx = target[0], ty = (double)k * target[1] + target[2];
    const double nx = floor(tx / art_world - lat_off + 0.5), ny = floor(ty / art_world - lat_off + 0.5);
    const double sx = (nx + lat_off) * art_world, sy = (ny + lat_off) * art_world;
    // panning north moves the target along the line of sight too (unlike the 45-degree view, where
    // pans stay in the view plane). The depth normalisation is centred on the target's depth; it is
    // quantised to half the depth radius so that ordinary pans leave every splat's normalised depth
    // bit-identical (it then shifts only when a pan crosses a step), and the radius grows by half a
    // step so the visible range stays covered.
    const double r0 = fmin((double)radius, 2.0 * oh + 8.0), zstep = r0 * 0.5;
    const double tz = za[0] * target[0] + za[1] * target[1] + za[2] * target[2];
    const double tzq = floor(tz / zstep + 0.5) * zstep;
    const double eye_d = oh / tan(19.0 * 3.14159265358979 / 180.0) + 2.0 * radius + 4.0;
    Splat_View v = {0};
    const float m[16] = {
        (float)xa[0], (float)ya[0], (float)za[0], 0,
        (float)xa[1], (float)ya[1], (float)za[1], 0,
        (float)xa[2], (float)ya[2], (float)za[2], 0,
        (float)-sx, (float)-sy, (float)-(tzq + eye_d), 1,
    };
    memcpy(v.view, m, sizeof(m));
    v.ortho_half_h = (float)oh;
    v.eye_dist = (float)eye_d;
    v.radius = (float)(r0 + zstep * 0.5);
    v.near_dist = 0.4f;
    v.art_w = art_w;
    v.art_h = art_h;
    v.dither = dither;
    v.outline_step = 0.35f;
    v.lod_force = -1;
    const long px0 = (long)nx - art_w / 2, py0 = (long)ny - art_h / 2;
    v.phase_x = (int)(((px0 % 4) + 4) % 4);
    v.phase_y = (int)(((py0 % 4) + 4) % 4);
    return v;
}

static bool post_enabled, live_sun;
static Splat_Vec4 material_ramps[64];
static Splat_Target shadow_map;
static sg_image snow_image;
static sg_view snow_texture;
static int snow_width,snow_height;
void splat_enable_sun(const uint8_t indices[64][3]) {
    live_sun=true;
    for (int i=0; i<64; i++) {
        material_ramps[i].v[3] = 1;
        for (int k=0; k<3; k++) {
            material_ramps[i].v[k] = indices[i][k];
            if (indices[i][k] == i) material_ramps[i].v[3] = k;
        }
    }
}
void splat_snow_texture(const uint8_t *depth,int w,int h) {
    if(!live_sun) return;
    if(w!=snow_width || h!=snow_height){
        if(snow_image.id){sg_destroy_view(snow_texture);sg_destroy_image(snow_image);}
        snow_image=sg_make_image(&(sg_image_desc){.width=w,.height=h,.pixel_format=SG_PIXELFORMAT_R8,
            .usage.stream_update=true,.label="snow-depth"});
        snow_texture=sg_make_view(&(sg_view_desc){.texture.image=snow_image});snow_width=w;snow_height=h;
    }
    sg_update_image(snow_image,&(sg_image_data){.mip_levels[0]={depth,(size_t)w*h}});
}
static Splat_Post post;
typedef struct {
    sg_image image;
    sg_view attachment, texture;
    int width, height;
} BloomLevel;
static BloomLevel bloom_levels[2];
static sg_pipeline bloom_pipeline;
static sg_sampler bloom_sampler;

void splat_configure_post(Splat_Post settings) {
    if (S.ready) SPLAT_FATAL("[splat] configure post before initialization\n");
    post_enabled = true;
    post = settings;
}
bool splat_post_enabled(void) { return post_enabled; }

static void target_destroy(Splat_Target *t) {
    if (t->w == 0) return;
    if (t->normal.id) {
        sg_destroy_view(t->normal_att);
        sg_destroy_view(t->normal_tex);
        sg_destroy_image(t->normal);
        sg_destroy_view(t->surface_att); sg_destroy_view(t->surface_tex); sg_destroy_image(t->surface);
    }
    sg_destroy_view(t->color_att);
    sg_destroy_view(t->depth_att);
    sg_destroy_view(t->color_tex);
    sg_destroy_view(t->depth_tex);
    sg_destroy_view(t->zbuf_att);
    sg_destroy_view(t->zbuf_tex);
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
    if (post_enabled) {
        t->surface=sg_make_image(&(sg_image_desc){.usage.color_attachment=true,.width=w,.height=h,
            .pixel_format=SG_PIXELFORMAT_RGBA16F,.sample_count=1,.label="lighting-surface"});
        t->surface_att=sg_make_view(&(sg_view_desc){.color_attachment.image=t->surface});
        t->surface_tex=sg_make_view(&(sg_view_desc){.texture.image=t->surface});
        t->normal = make_target_image(w, h, "mesh-normal-depth");
        t->normal_att = sg_make_view(&(sg_view_desc){.color_attachment.image=t->normal});
        t->normal_tex = sg_make_view(&(sg_view_desc){.texture.image=t->normal});
    }
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
    t->zbuf_tex = sg_make_view(&(sg_view_desc){.texture = {.image = t->zbuf}});
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
    if (post_enabled && post.bloom > 0) {
        int w=aw, h=ah;
        for (int i=0; i<2; i++) {
            BloomLevel *level=&bloom_levels[i];
            if (level->image.id) {
                sg_destroy_view(level->attachment);
                sg_destroy_view(level->texture);
                sg_destroy_image(level->image);
            }
            w=(w+1)/2; h=(h+1)/2;
            level->width=w; level->height=h;
            level->image=make_target_image(w,h,"art-bloom");
            level->attachment=sg_make_view(&(sg_view_desc){.color_attachment.image=level->image});
            level->texture=sg_make_view(&(sg_view_desc){.texture.image=level->image});
        }
    }
    S.art = make_target_image(aw, ah, "splat-art");
    S.art_att = sg_make_view(&(sg_view_desc){.color_attachment = {.image = S.art}});
    S.art_tex = sg_make_view(&(sg_view_desc){.texture = {.image = S.art}});
    S.art_w = aw;
    S.art_h = ah;
}

static void bone_image_make(int rows) {
    S.bone_rows = rows;
    S.bone_img = sg_make_image(&(sg_image_desc){
        .usage = {.stream_update = true},
        .width = SPLAT_MAX_BONES * 3,
        .height = rows,
        .pixel_format = SG_PIXELFORMAT_RGBA32F,
        .label = "splat-bones",
    });
    S.bone_tex = sg_make_view(&(sg_view_desc){.texture = {.image = S.bone_img}});
}

void splat_set_lights(const float *data, int side) {
    sg_destroy_view(S.lamp_tex);
    sg_destroy_image(S.lamp_img);
    S.lamp_img = sg_make_image(&(sg_image_desc){
        .width = side, .height = side, .pixel_format = SG_PIXELFORMAT_RGBA32F,
        .data.mip_levels[0] = {data, (size_t)side * side * 4 * sizeof(float)},
        .label = "splat-local-lights"
    });
    S.lamp_tex = sg_make_view(&(sg_view_desc){.texture.image = S.lamp_img});
    S.lamp_x = (int)data[0]; S.lamp_z = (int)data[1];
    S.lamp_w = (int)data[2]; S.lamp_h = (int)data[3];
}

void splat_init(void) {
    if (S.ready) return;
    S.splat_shd = sg_make_shader(post_enabled ? splat_post_shader_desc(sg_query_backend()) : splat_shader_desc(sg_query_backend()));
    S.resolve_shd = sg_make_shader(post_enabled ? resolve_post_shader_desc(sg_query_backend()) : resolve_shader_desc(sg_query_backend()));
    S.blit_shd = sg_make_shader(blit_shader_desc(sg_query_backend()));

    sg_blend_state over = {
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    sg_pipeline_desc splat_desc={
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
        .color_count = post_enabled ? 4 : 2,
        .colors = {
            [0] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [1] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [2] = {.pixel_format = SG_PIXELFORMAT_RGBA8},
            [3] = {.pixel_format = SG_PIXELFORMAT_RGBA16F},
        },
        .sample_count = 1,
        .label = "splat-pipeline",
    };
    if(post_enabled) splat_desc.layout.attrs[ATTR_splat_post_a_material]=(sg_vertex_attr_state){.format=SG_VERTEXFORMAT_FLOAT4,.offset=48};
    S.splat_pip=sg_make_pipeline(&splat_desc);
    S.skin_shd = sg_make_shader(post_enabled ? splat_skin_post_shader_desc(sg_query_backend()) : splat_skin_shader_desc(sg_query_backend()));
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
        .color_count = post_enabled ? 4 : 2,
        .colors = {
            [0] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [1] = {.pixel_format = SG_PIXELFORMAT_RGBA8, .blend = over},
            [2] = {.pixel_format = SG_PIXELFORMAT_RGBA8},
            [3] = {.pixel_format = SG_PIXELFORMAT_RGBA16F},
        },
        .sample_count = 1,
        .label = "splat-skin-pipeline",
    });
    if(live_sun){
        const uint8_t bare=0;
        splat_snow_texture(&bare,1,1);
    }
    bone_image_make(256);
    S.bone_data = calloc((size_t)SPLAT_MAX_SKIN_INSTANCES * SPLAT_MAX_BONES * 12, sizeof(float));
    S.resolve_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.resolve_shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .depth = {.pixel_format = SG_PIXELFORMAT_NONE},
        .colors[0] = {.pixel_format = SG_PIXELFORMAT_RGBA8},
        .sample_count = 1,
        .label = "splat-resolve-pipeline",
    });
    if (post_enabled && post.bloom > 0) {
        bloom_pipeline=sg_make_pipeline(&(sg_pipeline_desc){
            .shader=sg_make_shader(bloom_shader_desc(sg_query_backend())),
            .primitive_type=SG_PRIMITIVETYPE_TRIANGLE_STRIP, .depth.pixel_format=SG_PIXELFORMAT_NONE,
            .colors[0].pixel_format=SG_PIXELFORMAT_RGBA8, .sample_count=1, .label="art-bloom"});
        bloom_sampler=sg_make_sampler(&(sg_sampler_desc){.min_filter=SG_FILTER_LINEAR,.mag_filter=SG_FILTER_LINEAR,
            .wrap_u=SG_WRAP_CLAMP_TO_EDGE,.wrap_v=SG_WRAP_CLAMP_TO_EDGE,.label="bloom-linear"});
    }
    S.blit_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = S.blit_shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ZERO,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE,
        },
        .label = "splat-blit-pipeline",
    });
    S.depth_view_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(depth_view_shader_desc(sg_query_backend())),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .label = "splat-depth-view",
    });
    S.detail_pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(detail_shader_desc(sg_query_backend())),
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .label = "splat-detail-composite",
    });
    S.nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST,
        .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "splat-nearest",
    });
    S.own_img = sg_make_image(&(sg_image_desc){
        .usage = {.dynamic_update = true},
        .width = SPLAT_MAX_PALETTE,
        .height = SPLAT_MAX_OWN_PALETTES + 1,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .label = "splat-own-palettes",
    });
    S.own_tex = sg_make_view(&(sg_view_desc){.texture = {.image = S.own_img}});
    S.own_dirty = true;
    const float unlit[4]={0};
    splat_set_lights(unlit, 1);
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

// drops the static cloud, whichever form it has
static void static_clear(void) {
    if (S.world) {
        for (int k = 0; k < S.world_nx * S.world_nz; k++)
            if (S.world[k].live) sg_destroy_buffer(S.world[k].buf);
        free(S.world);
        free(S.world_order);
        S.world = NULL;
        S.world_order = NULL;
        S.world_live = 0;
    } else if (S.count) {
        sg_destroy_buffer(S.instances);
    }
    free(S.chunks);
    S.chunks = NULL;
    S.chunk_n = 0;
    S.count = 0;
    memset(S.lod_splats, 0, sizeof(S.lod_splats));
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
    // a single cloud replaces any chunk world
    static_clear();
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

void splat_chunks_add_rows(Splat_Chunks *ch, const float *rows, uint32_t n) {
    const Splat_Cloud c = {.count = n, .data = (float *)rows};
    const float zero[3] = {0, 0, 0};
    splat_chunks_add(ch, &c, zero);
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
    uint32_t particles = 0, out = 0;
    for (uint32_t i = 0; i < n; i++) {
        const float *d = src + (size_t)i * SPLAT_FLOATS;
        if (d[3] == SPLAT_FLAT_PATCH || d[3] == SPLAT_GROUND_DAB) {
            memcpy(dst + (size_t)out++ * SPLAT_FLOATS, d, SPLAT_FLOATS * sizeof(float));
            continue;
        }
        const int64_t cx = (int64_t)floorf(d[0] / cell) + (1 << 20), cy = (int64_t)floorf(d[1] / cell) + (1 << 20),
                      cz = (int64_t)floorf(d[2] / cell) + (1 << 20);
        it[particles].key = ((uint64_t)cx << 42) | ((uint64_t)cy << 21) | (uint64_t)cz;
        it[particles++].idx = i;
    }
    n = particles;
    qsort(it, n, sizeof(Cell_Item), cmp_cell);
    for (uint32_t a = 0; a < n;) {
        uint32_t b = a;
        while (b < n && it[b].key == it[a].key) b++;
        double w = 0, m[3] = {0, 0, 0}, col[3] = {0, 0, 0}, al = 0, flame = 0;
        for (uint32_t k = a; k < b; k++) {
            const float *d = src + (size_t)it[k].idx * SPLAT_FLOATS;
            const double wk = 1.0;
            w += wk;
            if (d[11] < 0) flame += d[11] * wk;
            for (int q = 0; q < 3; q++) m[q] += wk * d[q];
            const uint32_t pk = (uint32_t)d[7];
            col[0] += wk * (pk % 256);
            col[1] += wk * ((pk / 256) % 256);
            col[2] += wk * (pk / 65536);
            if (d[3] > al) al = d[3];
        }
        for (int q = 0; q < 3; q++) m[q] /= w;
        const float *closest = src + (size_t)it[a].idx * SPLAT_FLOATS;
        double best = HUGE_VAL;
        double c[6] = {0, 0, 0, 0, 0, 0};  // c00 c01 c02 c11 c12 c22
        for (uint32_t k = a; k < b; k++) {
            const float *d = src + (size_t)it[k].idx * SPLAT_FLOATS;
            const double dx = d[0] - m[0], dy = d[1] - m[1], dz = d[2] - m[2];
            const double distance = dx*dx + dy*dy + dz*dz;
            if (distance < best) { best = distance; closest = d; }
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
        // Keep the authored material and surface normal of the nearest contributing dab
        // when a distant LOD merges several surfaces into a single Gaussian.
        memcpy(o+12,closest+12,sizeof(float)*4);
        o[11] = flame < 0 ? (flame / w < -.4 ? (float)(flame / w) : 0) : closest[11];
        out++;
        a = b;
    }
    free(it);
    return out;
}

// see splat.h
Splat_Cloud *splat_cloud_merged(const Splat_Cloud *c, float cell) {
    if (!(cell > 0.0f)) SPLAT_FATAL("[splat] merge cell %g\n", (double)cell);
    Splat_Cloud *o = malloc(sizeof(*o));
    float *part = malloc(sizeof(float) * SPLAT_FLOATS * c->count);
    float *rows = malloc(sizeof(float) * SPLAT_FLOATS * c->count);
    if (!o || !part || !rows) SPLAT_FATAL("[splat] out of memory merging %u splats\n", c->count);
    *o = *c;
    o->bones = NULL;
    const size_t bb = (size_t)c->frame_n * c->bone_n * 12 * sizeof(float);
    if (c->bones && bb) {
        o->bones = malloc(bb);
        memcpy(o->bones, c->bones, bb);
    }
    // a merged splat follows one bone: the splats of each first bone are merged among themselves
    uint32_t out = 0;
    const int groups = c->bone_n ? (int)c->bone_n : 1;
    for (int b = 0; b < groups; b++) {
        uint32_t n = 0;
        for (uint32_t i = 0; i < c->count; i++) {
            const float *d = c->data + (size_t)i * SPLAT_FLOATS;
            if (c->bone_n && (int)d[12] != b) continue;
            memcpy(part + (size_t)n++ * SPLAT_FLOATS, d, sizeof(float) * SPLAT_FLOATS);
        }
        if (!n) continue;
        const uint32_t m = lod_merge(part, n, cell, rows + (size_t)out * SPLAT_FLOATS);
        for (uint32_t i = 0; c->bone_n && i < m; i++) {
            float *d = rows + (size_t)(out + i) * SPLAT_FLOATS;
            d[12] = d[13] = (float)b;
            d[14] = 0;
        }
        out += m;
    }
    free(part);
    o->count = out;
    o->data = rows;
    return o;
}

// one level of one chunk: its rows sorted back to front for V into dst, and its bounds grown by
// the largest splat extent
static void level_emit(const float *src, uint32_t cnt, const float *V, float *dst, float lo[3], float hi[3],
                       Sort_Item **items_io, uint32_t *items_cap) {
    for (int a = 0; a < 3; a++) { lo[a] = 1e30f; hi[a] = -1e30f; }
    float ext = 0;
    if (cnt > *items_cap) {
        *items_cap = cnt;
        *items_io = realloc(*items_io, sizeof(Sort_Item) * cnt);
    }
    Sort_Item *items = *items_io;
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
        memcpy(dst + (size_t)i * SPLAT_FLOATS, src + (size_t)items[i].idx * SPLAT_FLOATS, SPLAT_FLOATS * 4);
}

struct Splat_Baked {
    float cell0;
    float *all;
    size_t bytes;
    Splat_Chunk_Range *chunks;
    int chunk_n;
    uint64_t placed, rows;
    uint64_t lod_splats[SPLAT_LOD_LEVELS];
};

Splat_Baked *splat_chunks_bake(Splat_Chunks *ch, const float V[16]) {
    Splat_Baked *B = calloc(1, sizeof(Splat_Baked));
    B->cell0 = s_lod_cell0;
    if (ch->total == 0) {
        // an empty world is valid (a map with nothing on it): upload clears the static cloud
        free(ch->data);
        free(ch->count);
        free(ch->cap);
        free(ch);
        return B;
    }
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
    
    for (int c = 0; c < used; c++) {
        const int k = order[c].idx;
        lev[k * SPLAT_LOD_LEVELS] = ch->data[k];
        lev_n[k * SPLAT_LOD_LEVELS] = ch->count[k];
        for (int l = 1; l < SPLAT_LOD_LEVELS; l++) {
            const float *src = lev[k * SPLAT_LOD_LEVELS + l - 1];
            const uint32_t sn = lev_n[k * SPLAT_LOD_LEVELS + l - 1];
            float *dst = malloc((size_t)sn * SPLAT_FLOATS * sizeof(float));
            const uint32_t dn = lod_merge(src, sn, B->cell0 * (float)(1 << (l - 1)), dst);
            lev[k * SPLAT_LOD_LEVELS + l] = realloc(dst, (size_t)dn * SPLAT_FLOATS * sizeof(float));
            lev_n[k * SPLAT_LOD_LEVELS + l] = dn;
        }
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
            total += lev_n[k * SPLAT_LOD_LEVELS + l];
            B->lod_splats[l] += lev_n[k * SPLAT_LOD_LEVELS + l];
        }
    }
    if (total * SPLAT_FLOATS * 4 > 0x7fffffffull) SPLAT_FATAL("[splat] %llu splats with levels exceed one 2 GB buffer\n", (unsigned long long)total);
    const size_t bytes = (size_t)total * SPLAT_FLOATS * 4;
    float *all = malloc(bytes);
    if (!all) SPLAT_FATAL("[splat] out of memory for %zu bytes\n", bytes);
    B->chunks = calloc((size_t)used, sizeof(Splat_Chunk_Range));
    B->chunk_n = used;
    uint32_t at = 0;
    Sort_Item *items = NULL;
    uint32_t items_cap = 0;
    for (int c = 0; c < used; c++) {
        const int k = order[c].idx;
        Splat_Chunk_Range *r = &B->chunks[c];
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
            const float *src = lev[k * SPLAT_LOD_LEVELS + l];
            const uint32_t cnt = lev_n[k * SPLAT_LOD_LEVELS + l];
            r->start[l] = at;
            r->count[l] = cnt;
            level_emit(src, cnt, V, all + (size_t)at * SPLAT_FLOATS, r->lo[l], r->hi[l], &items, &items_cap);
            at += cnt;
            free(lev[k * SPLAT_LOD_LEVELS + l]);
        }
        ch->data[k] = NULL;
    }
    free(lev);
    free(lev_n);
    free(items);
    free(order);
    B->all = all;
    B->bytes = bytes;
    B->placed = ch->total;
    B->rows = total;
    free(ch->data);
    free(ch->count);
    free(ch->cap);
    free(ch);
    return B;
}

void splat_baked_upload(Splat_Baked *B, const uint32_t *pal, int pal_n, float radius) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_baked_upload before splat_init\n");
    static_clear();
    if (B->bytes)
        S.instances = sg_make_buffer(&(sg_buffer_desc){
            .usage = {.vertex_buffer = true, .immutable = true},
            .data = {.ptr = B->all, .size = B->bytes},
            .label = "splat-chunks",
        });
    S.chunks = B->chunks;
    S.chunk_n = B->chunk_n;
    memcpy(S.lod_splats, B->lod_splats, sizeof(S.lod_splats));
    S.count = (uint32_t)B->placed;
    S.lod_cell0 = B->cell0;
    S.scene_radius = radius;
    splat_set_palette(pal, pal_n);
    LOG_INFO("[splat] uploaded %llu splats in %d chunks with %d levels: %llu rows (%.1f MB); per level %llu %llu %llu %llu %llu %llu\n",
             (unsigned long long)B->placed, B->chunk_n, SPLAT_LOD_LEVELS, (unsigned long long)B->rows, (double)B->bytes / 1048576.0,
             (unsigned long long)S.lod_splats[0], (unsigned long long)S.lod_splats[1], (unsigned long long)S.lod_splats[2],
             (unsigned long long)S.lod_splats[3], (unsigned long long)S.lod_splats[4], (unsigned long long)S.lod_splats[5]);
    free(B->all);
    free(B);
}

void splat_chunks_upload(Splat_Chunks *ch, const float V[16], const uint32_t *pal, int pal_n, float radius) {
    splat_baked_upload(splat_chunks_bake(ch, V), pal, pal_n, radius);
}

struct Splat_Chunk_Baked {
    float *all;
    size_t bytes;
    uint32_t placed;
    Splat_Chunk_Range r;
};

Splat_Chunk_Baked *splat_chunk_bake(float *rows, uint32_t n, const float V[16]) {
    Splat_Chunk_Baked *B = calloc(1, sizeof(Splat_Chunk_Baked));
    B->placed = n;
    if (n == 0) {
        free(rows);
        return B;
    }
    const float cell0 = s_lod_cell0;
    float *lev[SPLAT_LOD_LEVELS];
    uint32_t lev_n[SPLAT_LOD_LEVELS], total = n;
    lev[0] = rows;
    lev_n[0] = n;
    for (int l = 1; l < SPLAT_LOD_LEVELS; l++) {
        lev[l] = malloc((size_t)lev_n[l - 1] * SPLAT_FLOATS * sizeof(float));
        lev_n[l] = lod_merge(lev[l - 1], lev_n[l - 1], cell0 * (float)(1 << (l - 1)), lev[l]);
        total += lev_n[l];
    }
    B->bytes = (size_t)total * SPLAT_FLOATS * 4;
    B->all = malloc(B->bytes);
    if (!B->all) SPLAT_FATAL("[splat] out of memory for %zu bytes\n", B->bytes);
    Sort_Item *items = NULL;
    uint32_t items_cap = 0, at = 0;
    for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
        B->r.start[l] = at;
        B->r.count[l] = lev_n[l];
        level_emit(lev[l], lev_n[l], V, B->all + (size_t)at * SPLAT_FLOATS, B->r.lo[l], B->r.hi[l], &items, &items_cap);
        at += lev_n[l];
        free(lev[l]);
    }
    free(items);
    return B;
}

void splat_chunk_baked_free(Splat_Chunk_Baked *B) {
    free(B->all);
    free(B);
}

void splat_world_begin(float chunk_size, float x0, float z0, int nx, int nz, const float V[16], const uint32_t *pal,
                       int pal_n, float radius) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_world_begin before splat_init\n");
    if (chunk_size <= 0 || nx <= 0 || nz <= 0) SPLAT_FATAL("[splat] bad chunk grid\n");
    static_clear();
    S.world = calloc((size_t)nx * nz, sizeof(Splat_World_Chunk));
    S.world_nx = nx;
    S.world_nz = nz;
    Chunk_Order *order = malloc(sizeof(Chunk_Order) * (size_t)nx * nz);
    for (int k = 0; k < nx * nz; k++) {
        const float cx = x0 + ((float)(k % nx) + 0.5f) * chunk_size, cz = z0 + ((float)(k / nx) + 0.5f) * chunk_size;
        order[k].key = V[2] * cx + V[10] * cz + V[14];
        order[k].idx = k;
    }
    qsort(order, (size_t)nx * nz, sizeof(Chunk_Order), cmp_chunk);
    S.world_order = malloc(sizeof(int) * (size_t)nx * nz);
    for (int k = 0; k < nx * nz; k++) S.world_order[k] = order[k].idx;
    free(order);
    S.lod_cell0 = s_lod_cell0;
    S.scene_radius = radius;
    splat_set_palette(pal, pal_n);
}

void splat_world_set_chunk(int cx, int cz, Splat_Chunk_Baked *B) {
    if (!S.world) SPLAT_FATAL("[splat] splat_world_set_chunk without a world\n");
    if (cx < 0 || cz < 0 || cx >= S.world_nx || cz >= S.world_nz) SPLAT_FATAL("[splat] chunk %d,%d outside the world grid\n", cx, cz);
    Splat_World_Chunk *w = &S.world[cz * S.world_nx + cx];
    if (w->live) {
        sg_destroy_buffer(w->buf);
        S.count -= w->placed;
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) S.lod_splats[l] -= w->r.count[l];
        S.world_live--;
        w->live = false;
    }
    if (B->bytes) {
        w->buf = sg_make_buffer(&(sg_buffer_desc){
            .usage = {.vertex_buffer = true, .immutable = true},
            .data = {.ptr = B->all, .size = B->bytes},
            .label = "splat-chunk",
        });
        w->r = B->r;
        w->placed = B->placed;
        w->live = true;
        S.count += B->placed;
        for (int l = 0; l < SPLAT_LOD_LEVELS; l++) S.lod_splats[l] += B->r.count[l];
        S.world_live++;
    }
    splat_chunk_baked_free(B);
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

static sg_buffer skin_model_buffer(const Splat_Cloud *c) {
    if (c->bone_n == 0 || c->bone_n > SPLAT_MAX_BONES) SPLAT_FATAL("[splat] skinned model needs 1..%d bones, has %u\n", SPLAT_MAX_BONES, c->bone_n);
    for (uint32_t i = 0; i < c->count; i++) {
        const float *d = c->data + (size_t)i * SPLAT_FLOATS;
        if (((int)lroundf(d[11]) & 15) != 12) SPLAT_FATAL("[splat] skinned model splat %u is kind %d, not 12\n", i, (int)lroundf(d[11]));
        if (d[12] < 0 || d[12] >= (float)c->bone_n || d[13] < 0 || d[13] >= (float)c->bone_n)
            SPLAT_FATAL("[splat] skinned model splat %u has bone %g/%g of %u\n", i, d[12], d[13], c->bone_n);
    }
    if (S.pal_n == 0) splat_set_palette(c->pal, (int)c->pal_n);
    if (c->radius > S.scene_radius) S.scene_radius = c->radius;
    return sg_make_buffer(&(sg_buffer_desc){
        .usage = {.vertex_buffer = true, .immutable = true},
        .data = {.ptr = c->data, .size = (size_t)c->count * SPLAT_FLOATS * 4},
        .label = "splat-skin-model",
    });
}

int splat_skin_add_model(const Splat_Cloud *c) {
    if (!S.ready) SPLAT_FATAL("[splat] splat_skin_add_model before splat_init\n");
    if (S.model_n >= (int)(sizeof(S.models) / sizeof(S.models[0]))) SPLAT_FATAL("[splat] too many skinned models\n");
    Splat_Skin_Model *m = &S.models[S.model_n];
    m->buf = skin_model_buffer(c);
    m->count = c->count;
    m->bone_n = (int)c->bone_n;
    return S.model_n++;
}

void splat_skin_set_model(int model, const Splat_Cloud *c) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    Splat_Skin_Model *m = &S.models[model];
    if ((int)c->bone_n != m->bone_n) SPLAT_FATAL("[splat] skinned model %d has %d bones, its replacement %u\n", model, m->bone_n, c->bone_n);
    if (m->rows) SPLAT_FATAL("[splat] skinned model %d is blended: its splats cannot be replaced\n", model);
    sg_destroy_buffer(m->buf);
    m->buf = skin_model_buffer(c);
    m->count = c->count;
}

int splat_skin_add_instance(int model) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    if (S.inst_n >= SPLAT_MAX_SKIN_INSTANCES) SPLAT_FATAL("[splat] more than %d skinned instances\n", SPLAT_MAX_SKIN_INSTANCES);
    if (S.inst_n >= S.bone_rows) {
        sg_destroy_view(S.bone_tex);
        sg_destroy_image(S.bone_img);
        bone_image_make(S.bone_rows * 2);
    }
    S.inst_model[S.inst_n] = model;
    memset(S.inst_tint[S.inst_n], 0, sizeof(S.inst_tint[0]));
    return S.inst_n++;
}

void splat_skin_set_instance_model(int instance, int model) {
    if (instance < 0 || instance >= S.inst_n) SPLAT_FATAL("[splat] bad skinned instance %d\n", instance);
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    S.inst_model[instance] = model;
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

void splat_skin_set_tint(int instance, const float rgba[4]) {
    if (instance < 0 || instance >= S.inst_n) SPLAT_FATAL("[splat] bad skinned instance %d\n", instance);
    memcpy(S.inst_tint[instance], rgba, sizeof(S.inst_tint[0]));
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

int splat_palette_add(const uint32_t *pal, int n, float dither) {
    if (n <= 0 || n > SPLAT_MAX_PALETTE) SPLAT_FATAL("[splat] own palette size %d\n", n);
    if (S.own_n >= SPLAT_MAX_OWN_PALETTES) SPLAT_FATAL("[splat] more than %d own palettes\n", SPLAT_MAX_OWN_PALETTES);
    const int k = ++S.own_n;
    for (int i = 0; i < n; i++) {
        uint8_t *t = S.own_px + ((size_t)k * SPLAT_MAX_PALETTE + i) * 4;
        t[0] = (uint8_t)(pal[i] >> 16);
        t[1] = (uint8_t)(pal[i] >> 8);
        t[2] = (uint8_t)pal[i];
        t[3] = 255;
    }
    S.own_opt[k][0] = (float)n;
    S.own_opt[k][1] = dither;
    S.own_dirty = true;
    return k;
}

void splat_skin_set_model_physical_pixels(int model) {
    if (model < 0 || model >= S.model_n || !S.models[model].rows || !S.models[model].own)
        SPLAT_FATAL("[splat] physical pixels require a blended model with its own palette\n");
    S.models[model].physical_pixels = true;
}

void splat_skin_set_model_palette(int model, int palette) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    if (palette < 0 || palette > S.own_n) SPLAT_FATAL("[splat] bad own palette %d\n", palette);
    S.models[model].own = palette;
}

void splat_skin_set_model_blended(int model, const Splat_Cloud *c) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    Splat_Skin_Model *m = &S.models[model];
    if (c->count != m->count || (int)c->bone_n != m->bone_n) SPLAT_FATAL("[splat] blended model %d: the cloud is not the one it was made from\n", model);
    if (m->rows) return;
    const size_t bytes = (size_t)c->count * SPLAT_FLOATS * 4;
    m->rows = malloc(bytes);
    if (!m->rows) SPLAT_FATAL("[splat] out of memory for blended model %d\n", model);
    memcpy(m->rows, c->data, bytes);
}

void splat_skin_set_model_lead(int model, float lead) {
    if (model < 0 || model >= S.model_n) SPLAT_FATAL("[splat] bad skinned model %d\n", model);
    if (!S.models[model].rows) SPLAT_FATAL("[splat] skinned model %d is not blended: it has no depth lead\n", model);
    if (!(lead >= 0.0f)) SPLAT_FATAL("[splat] depth lead %g\n", (double)lead);
    S.models[model].lead = lead;
}

static double wall_ms(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

// Brings blended instance `inst` into back-to-front order for the view direction za (the view
// matrix's z row: larger = nearer the eye). The order holds while the instance's bones keep
// their linear parts and their places against each other and za stays: moving the whole
// instance, or the view sideways, changes no splat's rank.
#define SORT_BUCKETS 65536
#define SORTED_KEEP_FRAMES 120
static void sorted_update(int inst, const float za[3]) {
    const int model = S.inst_model[inst];
    const Splat_Skin_Model *m = &S.models[model];
    const float *bt = S.bone_data + (size_t)inst * SPLAT_MAX_BONES * 12;  // texels: [r00 r01 r02 tx] [r10 ..] [r20 ..]
    float key[SPLAT_MAX_BONES * 12];
    memcpy(key, bt, sizeof(float) * 12 * (size_t)m->bone_n);
    for (int b = m->bone_n - 1; b >= 0; b--)
        for (int r = 0; r < 3; r++) key[b * 12 + r * 4 + 3] -= key[r * 4 + 3];
    Splat_Sorted *s = S.sorted[inst];
    if (!s) {
        s = S.sorted[inst] = calloc(1, sizeof(Splat_Sorted));
        s->model = -1;
    }
    if (s->model == model && s->cap >= m->count && memcmp(s->za, za, sizeof(s->za)) == 0 &&
        memcmp(s->bones, key, sizeof(float) * 12 * (size_t)m->bone_n) == 0)
        return;
    const double t0 = wall_ms();
    const uint32_t n = m->count;
    if (S.sort_cap < n) {
        S.sort_cap = n;
        S.sort_depth = realloc(S.sort_depth, sizeof(float) * n);
        S.sort_rows = realloc(S.sort_rows, sizeof(float) * SPLAT_FLOATS * n);
        if (!S.sort_depth || !S.sort_rows) SPLAT_FATAL("[splat] out of memory sorting %u splats\n", n);
    }
    // each bone's depth row: za . (R p + t)
    float brow[SPLAT_MAX_BONES][4];
    for (int b = 0; b < m->bone_n; b++) {
        const float *t = bt + b * 12;
        for (int k = 0; k < 4; k++) brow[b][k] = za[0] * t[k] + za[1] * t[4 + k] + za[2] * t[8 + k];
    }
    float lo = 1e30f, hi = -1e30f;
    for (uint32_t i = 0; i < n; i++) {
        const float *d = m->rows + (size_t)i * SPLAT_FLOATS;
        const float *r0 = brow[(int)d[12]];
        float z = r0[0] * d[0] + r0[1] * d[1] + r0[2] * d[2] + r0[3];
        if (d[14] > 0.0f) {
            const float *r1 = brow[(int)d[13]];
            z += (r1[0] * d[0] + r1[1] * d[1] + r1[2] * d[2] + r1[3] - z) * d[14];
        }
        S.sort_depth[i] = z;
        if (z < lo) lo = z;
        if (z > hi) hi = z;
    }
    // counting sort over the depth range, far (small z) first; a small model takes fewer buckets
    // (still two or more a splat), so that clearing and summing them does not outweigh its sort
    static uint32_t start[SORT_BUCKETS + 1];
    uint32_t buckets = SORT_BUCKETS;
    while (buckets > 1024 && buckets / 2 >= n * 2) buckets /= 2;
    memset(start, 0, sizeof(uint32_t) * (buckets + 1));
    const float scale = hi > lo ? (float)(buckets - 1) / (hi - lo) : 0.0f;
    for (uint32_t i = 0; i < n; i++) start[(uint32_t)((S.sort_depth[i] - lo) * scale) + 1]++;
    for (uint32_t k = 0; k < buckets; k++) start[k + 1] += start[k];
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t at = start[(uint32_t)((S.sort_depth[i] - lo) * scale)]++;
        memcpy(S.sort_rows + (size_t)at * SPLAT_FLOATS, m->rows + (size_t)i * SPLAT_FLOATS, sizeof(float) * SPLAT_FLOATS);
    }
    if (s->cap < n) {
        if (s->cap) sg_destroy_buffer(s->buf);
        s->buf = sg_make_buffer(&(sg_buffer_desc){
            .usage = {.vertex_buffer = true, .dynamic_update = true},
            .size = (size_t)n * SPLAT_FLOATS * 4,
            .label = "splat-sorted",
        });
        S.sorted_bytes += (uint64_t)(n - s->cap) * SPLAT_FLOATS * 4;
        s->cap = n;
    }
    sg_update_buffer(s->buf, &(sg_range){.ptr = S.sort_rows, .size = (size_t)n * SPLAT_FLOATS * 4});
    s->model = model;
    memcpy(s->za, za, sizeof(s->za));
    memcpy(s->bones, key, sizeof(float) * 12 * (size_t)m->bone_n);
    S.sorted_now++;
    S.sorted_splats += n;
    S.sort_ms += wall_ms() - t0;
}

// The blended instances that reach the view, far ones first, each in order for this frame.
static void blended_prepare(const Splat_Target *t) {
    const Splat_View *v = &S.view;
    const double focal = (double)t->h / 2.0 / (double)v->ortho_half_h;
    const float cx = (float)(focal * (double)v->view[12] + (double)t->w / 2.0);
    const float cy = (float)(focal * (double)v->view[13] + (double)t->h / 2.0);
    const float za[3] = {v->view[2], v->view[6], v->view[10]};
    S.blend_n = 0;
    S.sorted_now = 0;
    S.sorted_splats = 0;
    S.sort_ms = 0.0;
    float depth[SPLAT_MAX_SKIN_INSTANCES];
    for (int i = 0; i < S.inst_n; i++) {
        if (!S.models[S.inst_model[i]].rows) continue;
        const float *b = S.inst_bounds[i];
        if (b[3] > 0) {
            const float lo[3] = {b[0] - b[3], b[1] - b[3], b[2] - b[3]}, hi[3] = {b[0] + b[3], b[1] + b[3], b[2] + b[3]};
            if (!box_visible(lo, hi, v->view, focal, cx, cy, t->w, t->h)) continue;
        }
        const float z = za[0] * b[0] + za[1] * b[1] + za[2] * b[2];
        int at = S.blend_n++;
        for (; at > 0 && depth[at - 1] > z; at--) {
            depth[at] = depth[at - 1];
            S.blend_list[at] = S.blend_list[at - 1];
        }
        depth[at] = z;
        S.blend_list[at] = i;
    }
    // An instance's buffer is as large as the largest model it has been sorted from. It is given
    // back once the instance has gone SORTED_KEEP_FRAMES frames without drawing a model that
    // needs at least half of it (out of view, another kind of model, or a much smaller one),
    // and made again at the size then needed.
    S.blend_frame++;
    for (int k = 0; k < S.blend_n; k++) {
        Splat_Sorted *s = S.sorted[S.blend_list[k]];
        if (s && s->cap <= 2 * S.models[S.inst_model[S.blend_list[k]]].count) s->fit = S.blend_frame;
    }
    for (int i = 0; i < S.inst_n; i++) {
        Splat_Sorted *s = S.sorted[i];
        if (!s || !s->cap || S.blend_frame - s->fit <= SORTED_KEEP_FRAMES) continue;
        sg_destroy_buffer(s->buf);
        S.sorted_bytes -= (uint64_t)s->cap * SPLAT_FLOATS * 4;
        s->cap = 0;
        s->model = -1;
    }
    for (int k = 0; k < S.blend_n; k++) sorted_update(S.blend_list[k], za);
}

static void prepare_detail_regions(void) {
    const Splat_View *v = &S.view;
    const int old_n = S.detail_n, scale = v->scale;
    S.detail_n = 0;
    memset(S.detail_active, 0, sizeof S.detail_active);
    // Host occluder rectangles use the shared art grid. Retain the regular
    // composite in that mode; 3D geometry in detail crops has real depth.
    if (!S.occ_n) {
        const int w = v->art_w * scale, h = v->art_h * scale;
        const float focal = h / (2.0f * v->ortho_half_h);
        for (int k = 0; k < S.blend_n; k++) {
            const int i = S.blend_list[k];
            if (!S.models[S.inst_model[i]].physical_pixels) continue;
            const float *b = S.inst_bounds[i];
            if (b[3] <= 0.0f) continue;
            const float x = w * 0.5f + focal * (v->view[0]*b[0] + v->view[4]*b[1] + v->view[8]*b[2] + v->view[12]);
            const float y = h * 0.5f + focal * (v->view[1]*b[0] + v->view[5]*b[1] + v->view[9]*b[2] + v->view[13]);
            const float r = focal * b[3] + 2;
            if (x+r <= 0 || y+r <= 0 || x-r >= w || y-r >= h) continue;
            // Round the allocation size so pan and small zoom changes reuse it.
            const int side = 32 * (int)ceilf(2*r / 32);
            const int dw = side < w ? side : w, dh = side < h ? side : h;
            Splat_Detail *d = &S.details[S.detail_n++];
            d->instance = i;
            d->x = (int)fmaxf(0, fminf(w-dw, floorf(x-dw*0.5f)));
            d->y = (int)fmaxf(0, fminf(h-dh, floorf(y-dh*0.5f)));
            if (d->target.w != dw || d->target.h != dh) target_make(&d->target, dw, dh, "splat-detail-region");
            S.detail_active[i] = true;
        }
    }
    for (int k = S.detail_n; k < old_n; k++) target_destroy(&S.details[k].target);
}

static void draw_splats(const Splat_Target *t, const Splat_Detail *detail, const Splat_View *v) {
    const bool casting=(t==&shadow_map);
    vs_splat_params_t p;
    p.lampOrigin = (Splat_Vec4){{(float)S.lamp_x, (float)S.lamp_z, 0, 0}};
    p.lampFrame=(Splat_Vec4){{live_sun?0.0f:(float)S.lamp_w,(float)S.lamp_h,v->lamp_seconds,v->lamp_height}};
    memcpy(p.view.v, v->view, sizeof(p.view.v));
    // A crop uses the same projected scene and depth range, translated to
    // its own target. Only the sampling pitch changes.
    const int w = detail ? v->art_w * v->scale : t->w;
    const int h = detail ? v->art_h * v->scale : t->h;
    const double focal = h / (2.0 * v->ortho_half_h);
    const float cx = (float)(focal*v->view[12] + w*0.5 - (detail ? detail->x : 0));
    const float cy = (float)(focal*v->view[13] + h*0.5 - (detail ? detail->y : 0));
    p.cam = (Splat_Vec4){{cx, cy, (float)t->w, (float)t->h}};
    p.dep = (Splat_Vec4){{v->eye_dist, v->radius, (float)focal, v->near_dist}};
    const float zrad = S.scene_radius > v->radius ? S.scene_radius : v->radius;
    p.zr = (Splat_Vec4){{v->eye_dist - zrad - 1.0f, 1.0f / (2.0f * zrad + 2.0f), 0, 0}};
    fs_splat_params_t fp = {.frag = {{cx, cy, (float)t->h, sg_query_features().origin_top_left ? 1.0f : 0.0f}}};
    const float *bg = S.pal[0];
    // Ink resolves a figure against its own background colour. The other
    // geometry in this crop supplies occlusion only; mixing its colour into
    // the figure changes the palette entry at the edge of the artwork.
    Splat_Vec4 backdrop = {{0}};
    if (detail) {
        const int own = S.models[S.inst_model[detail->instance]].own;
        const uint8_t *base = S.own_px + own * SPLAT_MAX_PALETTE * 4;
        backdrop = (Splat_Vec4){{base[0] / 255.0f, base[1] / 255.0f, base[2] / 255.0f, 1}};
        bg = backdrop.v;
        fp.tint = backdrop;
    }
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {bg[0], bg[1], bg[2], 0.0f}},
            .colors[1] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {1.0f, 0.0f, 0.0f, 0.0f}},
            .colors[3] = {.load_action=SG_LOADACTION_CLEAR,.clear_value={0,0,1,0}},
            .colors[2] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0,0,0,0}},
            // the art grid's depth outlives the pass: the composite tests what it lays over the picture against it
            .depth = {.load_action = SG_LOADACTION_CLEAR, .clear_value = 1.0f,
                      .store_action = t == &S.native ? SG_STOREACTION_STORE : SG_STOREACTION_DONTCARE},
        },
        .attachments = {.colors = {t->color_att, t->depth_att, t->normal_att,t->surface_att}, .depth_stencil = t->zbuf_att},
        .label = "splat-pass",
    });
    artmesh_draw(&p.view, p.cam, p.dep, p.zr, backdrop);
    if (S.count) {
        fp.mode = (Splat_Vec4){{v->static_cut ? 1.0f : 0.0f, 0, 0, 0}};
        sg_apply_pipeline(S.splat_pip);
        sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
        sg_apply_uniforms(UB_fs_splat_params, &SG_RANGE(fp));
        if (!S.chunks && !S.world) {
            sg_apply_bindings(&(sg_bindings){.vertex_buffers[0] = S.instances,.views[VIEW_lamp_cells]=S.lamp_tex,.samplers[SMP_lamp_smp]=S.nearest});
            sg_draw(0, 4, (int)S.count);
            S.splats_drawn = S.count;
        } else {
            // coarsest level whose merge cell is no larger than one pixel of this target
            const float px_world = (float)(1.0 / focal);
            int lvl = 0;
            for (int l = 1; l < SPLAT_LOD_LEVELS; l++)
                if (S.lod_cell0 * (float)(1 << (l - 1)) <= px_world) lvl = l;
            if (v->lod_force >= 0) lvl = v->lod_force < SPLAT_LOD_LEVELS ? v->lod_force : SPLAT_LOD_LEVELS - 1;
            S.lod_level = lvl;
            int drawn = 0;
            uint64_t sd = 0;
            const int chunk_n = S.world ? S.world_nx * S.world_nz : S.chunk_n;
            for (int c = 0; c < chunk_n; c++) {
                const Splat_World_Chunk *w = S.world ? &S.world[S.world_order[c]] : NULL;
                if (w && !w->live) continue;
                const Splat_Chunk_Range *r = w ? &w->r : &S.chunks[c];
                if (!r->count[lvl] || !box_visible(r->lo[lvl], r->hi[lvl], v->view, focal, cx, cy, t->w, t->h)) continue;
                sg_apply_bindings(&(sg_bindings){
                    .views[VIEW_lamp_cells]=S.lamp_tex,.samplers[SMP_lamp_smp]=S.nearest,
                    .vertex_buffers[0] = w ? w->buf : S.instances,
                    .vertex_buffer_offsets[0] = (int)((size_t)r->start[lvl] * SPLAT_FLOATS * 4),
                });
                sg_draw(0, 4, (int)r->count[lvl]);
                drawn++;
                sd += r->count[lvl];
            }
            S.chunks_drawn = drawn;
            S.splats_drawn = sd;
        }
        if (!detail && !casting) {
            S.pass_lod[t == &S.native] = S.lod_level;
            S.pass_splats[t == &S.native] = S.splats_drawn;
        }
    }
    if (S.inst_n) {
        fp.mode = (Splat_Vec4){{1, 0, 0, 0}};
        sg_apply_pipeline(S.skin_pip);
        int sk = 0;
        uint64_t sks = 0;
        for (int i = 0; i < S.inst_n; i++) {
            const Splat_Skin_Model *m = &S.models[S.inst_model[i]];
            if (m->rows && !casting) continue;  // an opaque cut can draw the unsorted original buffer
            const float *b = S.inst_bounds[i];
            if (b[3] > 0) {
                const float lo[3] = {b[0] - b[3], b[1] - b[3], b[2] - b[3]}, hi[3] = {b[0] + b[3], b[1] + b[3], b[2] + b[3]};
                if (!box_visible(lo, hi, v->view, focal, cx, cy, t->w, t->h)) continue;
            }
            sk++;
            sks += m->count;
            p.zr.v[2] = (float)i;
            fp.mode.v[1] = detail ? 0.0f : (float)m->own;
            if (!detail) memcpy(fp.tint.v, S.inst_tint[i], sizeof(S.inst_tint[0]));
            sg_apply_bindings(&(sg_bindings){
                .vertex_buffers[0] = m->buf,
                .views[VIEW_lamp_cells]=S.lamp_tex,.samplers[SMP_lamp_smp]=S.nearest,
                .views[VIEW_bone_tex] = S.bone_tex,
                .samplers[SMP_bone_smp] = S.nearest,
            });
            sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
            sg_apply_uniforms(UB_fs_splat_params, &SG_RANGE(fp));
            sg_draw(0, 4, (int)m->count);
        }
        // blended instances last, far ones first: each is in back-to-front order itself, and what
        // stands in front of it has written its depth
        for (int k = 0; !casting && k < S.blend_n; k++) {
            const int i = S.blend_list[k];
            if (!detail && S.detail_active[i]) continue;
            const Splat_Skin_Model *m = &S.models[S.inst_model[i]];
            sk++;
            sks += m->count;
            p.zr.v[2] = (float)i;
            p.zr.v[3] = m->lead * p.zr.v[1];
            fp.mode = (Splat_Vec4){{0, detail && i != detail->instance ? 0.0f : (float)m->own, 0, 0}};
            if (detail && i != detail->instance) fp.tint = backdrop;
            else memcpy(fp.tint.v, S.inst_tint[i], sizeof(S.inst_tint[0]));
            sg_apply_bindings(&(sg_bindings){
                .vertex_buffers[0] = S.sorted[i]->buf,
                .views[VIEW_lamp_cells]=S.lamp_tex,.samplers[SMP_lamp_smp]=S.nearest,
                .views[VIEW_bone_tex] = S.bone_tex,
                .samplers[SMP_bone_smp] = S.nearest,
            });
            sg_apply_uniforms(UB_vs_splat_params, &SG_RANGE(p));
            sg_apply_uniforms(UB_fs_splat_params, &SG_RANGE(fp));
            sg_draw(0, 4, (int)m->count);
        }
        if (!detail && !casting) {
            S.skin_drawn = sk;
            S.pass_skinned[t == &S.native] = sks;
        }
    }
    sg_end_pass();
}

static void blur_highlights(const vs_quad_params_t *quad, const int lattice[2]) {
    sg_view source = S.native.color_tex;
    int w=S.native.w, h=S.native.h;
    for (int i=0; i<2; i++) {
        BloomLevel *level=&bloom_levels[i];
        fs_bloom_params_t params = {.blur={{.68f,i==0?.16f:0,1.0f/w,1.0f/h}},
            .grid={{(float)((lattice[0]>>i)&1),(float)((lattice[1]>>i)&1),(float)level->width,(float)level->height}}};
        sg_begin_pass(&(sg_pass){.action.colors[0].load_action=SG_LOADACTION_DONTCARE,
            .attachments.colors[0]=level->attachment,.label="art-bloom-downsample"});
        sg_apply_pipeline(bloom_pipeline);
        sg_apply_bindings(&(sg_bindings){.views[VIEW_u_bright]=source,.samplers[SMP_bloom_smp]=bloom_sampler});
        sg_apply_uniforms(UB_vs_quad_params,&(sg_range){quad,sizeof *quad});
        sg_apply_uniforms(UB_fs_bloom_params,&SG_RANGE(params));
        sg_draw(0,4,1);
        sg_end_pass();
        source=level->texture; w=level->width; h=level->height;
    }
}

static void render_sun(fs_resolve_params_t *p){
    const Splat_View *eye=&S.view;
    const float angle=6.283185307f*(eye->sky_phase-.25f);
    const float elevation=sinf(angle);
    const float x=-cosf(angle),y=fmaxf(.13f,elevation),z=-.40f;
    const float len=sqrtf(x*x+y*y+z*z);
    p->sunVector=(Splat_Vec4){{x/len,y/len,z/len,fminf(1,fmaxf(0,(elevation+.14f)*3))}};
    const float pitch=asinf(y/len), yaw=atan2f(x,z);
    const float *v=eye->view;
    float center[3];
    for(int i=0;i<3;i++) center[i]=-v[4*i]*v[12]-v[4*i+1]*v[13]-v[4*i+2]*(v[14]+eye->eye_dist);
    const int side=1024;
    const float half=fmaxf(16,eye->ortho_half_h*sqrtf(1+powf((float)eye->art_w/eye->art_h,2))+10);
    Splat_View light=splat_view_ortho(yaw,pitch,center,2*half/side,side,side,S.scene_radius,0);
    light.static_cut=true;
    if(!shadow_map.w) target_make(&shadow_map,side,side,"sun-shadow");
    if(p->sunVector.v[3]>.01f) draw_splats(&shadow_map,NULL,&light);
    memcpy(p->camera.v,eye->view,sizeof p->camera.v);
    memcpy(p->lightCamera.v,light.view,sizeof p->lightCamera.v);
    p->reconstruct=(Splat_Vec4){{2*eye->ortho_half_h/eye->art_h,eye->eye_dist,eye->radius,1}};
    p->shadowGrid=(Splat_Vec4){{.5f/half,light.eye_dist-light.radius,.5f/light.radius,1.f/side}};
    const float dusk=1-fminf(1,fabsf(elevation)*3);
    p->skyTint=(Splat_Vec4){{eye->sky_tint[0],eye->sky_tint[1],eye->sky_tint[2],.60f*eye->night+.10f*dusk}};
    p->weather=(Splat_Vec4){{eye->rain,eye->clouds,eye->lamp_seconds,eye->lamp_height}};
    p->lightRect=(Splat_Vec4){{S.lamp_x,S.lamp_z,S.lamp_w,S.lamp_h}};
    memcpy(p->ramps,material_ramps,sizeof material_ramps);
}

void splat_render_offscreen(void) {
    if (!S.ready || !S.have_view || (S.count == 0 && S.inst_n == 0 && !artmesh_visible())) return;
    ensure_targets(S.view.art_w, S.view.art_h);
    if (S.inst_n) {
        sg_update_image(S.bone_img, &(sg_image_data){.mip_levels[0] = {
            .ptr = S.bone_data,
            .size = (size_t)S.bone_rows * SPLAT_MAX_BONES * 12 * sizeof(float),
        }});
    }
    if (S.own_dirty) {
        sg_update_image(S.own_img, &(sg_image_data){.mip_levels[0] = {.ptr = S.own_px, .size = sizeof(S.own_px)}});
        S.own_dirty = false;
    }
    blended_prepare(&S.samples);
    if (!live_sun) prepare_detail_regions();
    fs_resolve_params_t rp={0};
    if(live_sun) render_sun(&rp);
    for (int k = 0; k < S.detail_n; k++) draw_splats(&S.details[k].target, &S.details[k], &S.view);
    draw_splats(&S.samples, NULL, &S.view);
    draw_splats(&S.native, NULL, &S.view);

    const float flip = sg_query_features().origin_top_left ? 1.0f : 0.0f;
    memcpy(rp.pal, S.pal, sizeof(float) * 4 * (size_t)S.pal_n);
    rp.opts = (Splat_Vec4){{(float)S.pal_n, S.view.dither, flip, S.view.outline}};
    rp.size = (Splat_Vec4){{(float)S.art_w, (float)S.art_h, (float)S.samples.w, (float)S.samples.h}};
    rp.phase = (Splat_Vec4){{(float)S.view.phase_x, (float)S.view.phase_y,
                             S.view.outline_step / (2.0f * S.view.radius), S.view.transparent ? 1.0f : 0.0f}};
    for (int k = 1; k <= S.own_n; k++) rp.own[k] = (Splat_Vec4){{S.own_opt[k][0], S.own_opt[k][1], 0, 0}};
    vs_quad_params_t qp = {.quad = {{flip, 0, 0, 0}}};
    if (post_enabled) {
        float art_world=2.0f*S.view.ortho_half_h/S.art_h;
        float fade=fminf(1.0f,.045f/art_world);
        rp.effect=(Splat_Vec4){{post.outline*fade,post.ridge*fade,post.bloom,post.grade}};
        rp.depth_scale.v[0]=1.0f/(2.0f*S.view.radius);
        rp.opts.v[1]*=post.dither;
        for (int k=1;k<=S.own_n;k++) rp.own[k].v[1]*=post.dither;
        if (post.bloom>0) {
            // Bloom blocks follow the same world lattice as the dither pattern, in storage rows.
            const int lattice[2]={S.view.phase_x&3, (flip>0 ? -(S.art_h+S.view.phase_y) : S.view.phase_y)&3};
            rp.bloom_grid=(Splat_Vec4){{(float)lattice[0],(float)lattice[1],0,0}};
            blur_highlights(&qp,lattice);
        }
    }
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
            [VIEW_u_pal] = S.own_tex,
            [VIEW_u_geometry] = S.native.normal_tex,
            [VIEW_u_surface] = S.native.surface_tex,
            [VIEW_u_lamps] = post_enabled ? S.lamp_tex : (sg_view){0},
            [VIEW_u_shadow] = live_sun ? shadow_map.surface_tex : S.native.surface_tex,
            [VIEW_u_snow] = live_sun ? snow_texture : S.native.normal_tex,
            [VIEW_u_half] = post_enabled && post.bloom==0 ? S.native.color_tex : bloom_levels[0].texture,
            [VIEW_u_quarter] = post_enabled && post.bloom==0 ? S.native.color_tex : bloom_levels[1].texture,
        },
        .samplers[SMP_u_smp] = S.nearest,
    });
    sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
    sg_apply_uniforms(UB_fs_resolve_params, &SG_RANGE(rp));
    sg_draw(0, 4, 1);
    sg_end_pass();
}

void splat_composite(Splat_Show show) {
    if (!S.ready || !S.have_view || (S.count == 0 && S.inst_n == 0 && !artmesh_visible()) || S.art_w == 0) return;
    sg_view tex = S.art_tex;
    int tw = S.art_w, th = S.art_h;
    const bool raw = show == SPLAT_SHOW_NATIVE_RAW;
    if (show == SPLAT_SHOW_NATIVE || raw) {
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
    // occluders to art-pixel rectangles and normalised depths (same formulas as the splat pass)
    fs_blit_params_t bp;
    memset(&bp, 0, sizeof(bp));
    // Samples already contain RGB composited over the palette background. A diagnostic
    // readback must copy that RGB once, including pixels of zero accumulated opacity.
    bp.blit_opts.v[0] = (raw || show==SPLAT_SHOW_SAMPLES) ? 1.0f : 0.0f;
    if (show == SPLAT_SHOW_ART && S.occ_n) {
        const Splat_View *v = &S.view;
        const double f = (double)S.art_h / 2.0 / (double)v->ortho_half_h;  // art px per world unit
        const double cx = f * (double)v->view[12] + S.art_w / 2.0, cy = f * (double)v->view[13] + S.art_h / 2.0;
        for (int i = 0; i < S.occ_n; i++) {
            const float *o = S.occ + i * SPLAT_OCCLUDER_FLOATS;
            float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
            for (int c = 0; c < 8; c++) {
                const float x = (c & 1) ? o[2] : o[0], z = (c & 2) ? o[3] : o[1], y = (c & 4) ? o[4] : 0.0f;
                const float sx = (float)(f * (v->view[0] * x + v->view[4] * y + v->view[8] * z) + cx);
                const float sy = (float)(f * (v->view[1] * x + v->view[5] * y + v->view[9] * z) + cy);
                lo[0] = fminf(lo[0], sx); hi[0] = fmaxf(hi[0], sx);
                lo[1] = fminf(lo[1], sy); hi[1] = fmaxf(hi[1], sy);
            }
            bp.occ_rect[i] = (Splat_Vec4){{lo[0], lo[1], hi[0], hi[1]}};
            const float pz = v->view[2] * 0.5f * (o[0] + o[2]) + v->view[10] * o[5] + v->view[14];  // foot on the ground
            float dn = (-pz - (v->eye_dist - v->radius)) / (2.0f * v->radius);
            dn = dn < 0 ? 0 : dn > 1 ? 1 : dn;
            bp.occ_depth[i].v[0] = dn;
        }
        bp.occ_info = (Splat_Vec4){{(float)S.occ_n, (float)S.art_h, sg_query_features().origin_top_left ? 1.0f : 0.0f, (float)S.samples.h}};
    }
    sg_apply_viewport(S.view.offset_px[0], S.view.offset_px[1], tw * k, th * k, true);
    if (show == SPLAT_SHOW_DEPTH) {
        const Splat_View *v = &S.view;
        const float zrad = S.scene_radius > v->radius ? S.scene_radius : v->radius;
        fs_depth_view_params_t dv = {
            .range = {{v->eye_dist - zrad - 1.0f, 1.0f / (2.0f * zrad + 2.0f), v->eye_dist, 4.0f * v->ortho_half_h}},
            .bands = {{0.5f, 0, 0, 0}},
        };
        sg_apply_pipeline(S.depth_view_pip);
        sg_apply_bindings(&(sg_bindings){.views[VIEW_depth_view_z] = S.native.zbuf_tex, .samplers[SMP_depth_view_smp] = S.nearest});
        sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
        sg_apply_uniforms(UB_fs_depth_view_params, &SG_RANGE(dv));
        sg_draw(0, 4, 1);
        sg_apply_viewport(0, 0, sw, sh, true);
        return;
    }
    sg_apply_pipeline(S.blit_pip);
    sg_apply_bindings(&(sg_bindings){
        .views = {[VIEW_blit_tex] = tex, [VIEW_blit_depth] = S.samples.depth_tex},
        .samplers = {[SMP_blit_smp] = S.nearest, [SMP_blit_dsmp] = S.nearest},
    });
    sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
    sg_apply_uniforms(UB_fs_blit_params, &SG_RANGE(bp));
    sg_draw(0, 4, 1);
    if (show == SPLAT_SHOW_ART) {
        for (int i = 0; i < S.detail_n; i++) {
            const Splat_Detail *d = &S.details[i];
            const int pal = S.models[S.inst_model[d->instance]].own;
            const int x = S.view.offset_px[0] + d->x;
            const int y = S.view.offset_px[1] + S.art_h * k - d->y - d->target.h;
            sg_apply_viewport(x, y, d->target.w, d->target.h, true);
            sg_apply_pipeline(S.detail_pip);
            sg_apply_bindings(&(sg_bindings){
                .views = {[VIEW_detail_color] = d->target.color_tex, [VIEW_detail_depth] = d->target.depth_tex,
                          [VIEW_detail_palette] = S.own_tex},
                .samplers[SMP_detail_smp] = S.nearest,
            });
            fs_detail_params_t dp = {.palette = {{(float)pal, S.own_opt[pal][0], S.own_opt[pal][1], 0}}};
            sg_apply_uniforms(UB_vs_quad_params, &SG_RANGE(qp));
            sg_apply_uniforms(UB_fs_detail_params, &SG_RANGE(dp));
            sg_draw(0, 4, 1);
        }
    }
    sg_apply_viewport(0, 0, sw, sh, true);
}

void splat_set_occluders(const float *occ, int n) {
    if (n < 0 || n > SPLAT_MAX_OCCLUDERS) SPLAT_FATAL("[splat] %d occluders (max %d)\n", n, SPLAT_MAX_OCCLUDERS);
    if (n) memcpy(S.occ, occ, sizeof(float) * SPLAT_OCCLUDER_FLOATS * (size_t)n);
    S.occ_n = n;
}

_Static_assert(SPLAT_LOD_LEVELS == 6, "Splat_Stats.lod_splats holds six levels");
Splat_Stats splat_stats(void) {
    uint32_t sk = 0;
    for (int i = 0; i < S.inst_n; i++) sk += S.models[S.inst_model[i]].count;
    Splat_Stats st = {.splats = S.count, .skinned_splats = sk, .skin_instances = S.inst_n,
                      .chunks = S.world ? S.world_live : S.chunk_n, .chunks_drawn = S.chunks_drawn, .splats_drawn = S.splats_drawn,
                      .skin_drawn = S.skin_drawn, .lod_level = S.lod_level,
                      .art_w = S.art_w, .art_h = S.art_h, .scale = S.last_scale};
    for (int p = 0; p < 2; p++) {
        st.pass_lod[p] = S.pass_lod[p];
        st.pass_splats[p] = S.pass_splats[p];
        st.pass_skinned[p] = S.pass_skinned[p];
    }
    for (int l = 0; l < SPLAT_LOD_LEVELS; l++) {
        st.lod_splats[l] = S.lod_splats[l];
        st.static_bytes += S.lod_splats[l] * SPLAT_FLOATS * 4;
    }
    if (S.lod_splats[0] == 0) st.static_bytes = (uint64_t)S.count * SPLAT_FLOATS * 4;  // one buffer, no levels
    for (int m = 0; m < S.model_n; m++) st.skin_bytes += (uint64_t)S.models[m].count * SPLAT_FLOATS * 4;
    // a target is two RGBA8 images and a depth buffer (4 bytes a pixel each): the 2x sample grid, the art grid; then the art image
    st.target_bytes = (uint64_t)S.art_w * S.art_h * (4 * 12 + 12 + 4) + (uint64_t)S.bone_rows * SPLAT_MAX_BONES * 12 * 4;
    for (int i = 0; i < S.detail_n; i++)
        st.target_bytes += (uint64_t)S.details[i].target.w * S.details[i].target.h * 12;
    st.blended_drawn = S.blend_n;
    st.blended_sorted = S.sorted_now;
    st.blended_sorted_splats = S.sorted_splats;
    st.blended_bytes = S.sorted_bytes;
    st.blended_sort_ms = S.sort_ms;
    if (post_enabled) {
        st.target_bytes+=(uint64_t)S.art_w*S.art_h*60;
        for(int i=0;i<S.detail_n;i++) st.target_bytes+=(uint64_t)S.details[i].target.w*S.details[i].target.h*12;
        for(int i=0;i<2;i++) st.target_bytes+=(uint64_t)bloom_levels[i].width*bloom_levels[i].height*4;
    }
    if(live_sun) st.target_bytes+=(uint64_t)shadow_map.w*shadow_map.h*24+(uint64_t)snow_width*snow_height;
    return st;
}
