// splat.h — Gaussian splat clouds: loading (.splat v1), GPU upload, offscreen
// rendering into an art-pixel grid with a palette resolve, and compositing.
//
// Frame wiring (consumer side):
//   on_render_ready     -> splat_init()
//   on_pre_pass_render  -> splat_render_offscreen()   (outside any pass)
//   on_post_mesh_render -> splat_composite(...)       (inside the swapchain pass)
//
// The view is orthographic and fixed in direction; splats are uploaded in
// back-to-front order for that direction once (splat_upload).

#ifndef ARC_SPLAT_H
#define ARC_SPLAT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { float v[16]; } Splat_Mat4;
typedef struct { float v[4]; } Splat_Vec4;

#define SPLAT_FLOATS 16
#define SPLAT_MAX_PALETTE 48

typedef struct Splat_Cloud {
    uint32_t count;
    uint32_t pal_n;
    uint32_t bone_n;
    uint32_t frame_n;
    float fps;
    float radius;
    float bmin[3], bmax[3];
    bool has_cam;
    // yaw, pitch, dist, target xyz, eyeD, oh, focal, near, px, dither, W, H, 0, 0
    float cam[16];
    float cam_view[16];
    uint32_t pal[SPLAT_MAX_PALETTE];
    float *data;   // count * SPLAT_FLOATS
    float *bones;  // frame_n * bone_n * 12, or NULL
} Splat_Cloud;

typedef struct Splat_View {
    float view[16];     // world -> view, column-major
    float ortho_half_h; // world units
    float eye_dist;     // distance of the eye from the target
    float radius;       // depth normalisation radius
    float near_dist;
    int art_w, art_h;   // art grid; samples target is 2x
    float dither;
    float outline;      // 0 = body-coloured contour, 1 = background-coloured (dark) contour
} Splat_View;

typedef enum {
    SPLAT_SHOW_ART = 0,
    SPLAT_SHOW_NATIVE = 1,
    SPLAT_SHOW_SAMPLES = 2,
} Splat_Show;

typedef struct Splat_Stats {
    uint32_t splats;
    int art_w, art_h;
    int scale;          // composite scale (physical px per target px)
} Splat_Stats;

Splat_Cloud *splat_cloud_load(const char *path);
void splat_cloud_free(Splat_Cloud *c);

void splat_init(void);
void splat_upload(const Splat_Cloud *c, const float view[16]);
void splat_set_palette(const uint32_t *pal, int n);
void splat_set_view(const Splat_View *v);
void splat_render_offscreen(void);
void splat_composite(Splat_Show show);
Splat_Stats splat_stats(void);

// Builds a Splat_View from a cloud's embedded reference camera. Fatal if none.
Splat_View splat_view_from_cloud_camera(const Splat_Cloud *c);

#endif
