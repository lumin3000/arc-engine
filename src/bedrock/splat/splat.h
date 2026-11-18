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
    float outline;      // 0 = reference look; 1 = dark contour on silhouettes and depth steps
    float outline_step; // world depth step that draws a contour when outline is on
    int phase_x, phase_y; // ordered-dither phase of art pixel (0,0), so the pattern stays on the world
    int scale;          // composite scale in physical px per art px; 0 = largest integer fit
    int static_cut;     // 1 = draw the static cloud with the opaque cut too (diagnostics)
} Splat_View;

typedef enum {
    SPLAT_SHOW_ART = 0,
    SPLAT_SHOW_NATIVE = 1,
    SPLAT_SHOW_SAMPLES = 2,
} Splat_Show;

typedef struct Splat_Stats {
    uint32_t splats;
    uint32_t skinned_splats;  // drawn per frame (instances x model splats)
    int skin_instances;
    int chunks, chunks_drawn;       // static chunks in the world / drawn last frame
    uint64_t splats_drawn;          // static splats submitted last frame (per pass)
    int skin_drawn;                 // skinned instances drawn last frame
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

// Chunked static worlds: copies of prototype clouds placed by translation into a grid of
// square chunks on x/z. splat_chunks_upload sorts each chunk back to front for the view
// direction, orders the chunks back to front, uploads one buffer and frees the CPU copy.
// Each frame only chunks whose bounds reach the view are drawn.
typedef struct Splat_Chunks Splat_Chunks;
Splat_Chunks *splat_chunks_new(float chunk_size, float x0, float z0, int nx, int nz);
void splat_chunks_add(Splat_Chunks *ch, const Splat_Cloud *proto, const float offset[3]);
uint64_t splat_chunks_count(const Splat_Chunks *ch);
void splat_chunks_upload(Splat_Chunks *ch, const float view[16], const uint32_t *pal, int pal_n, float radius);

// Skinned clouds (every splat kind 12, carried by bones): uploaded at rest; each instance has
// its own bone matrices (12 floats a bone: 3x3 rotation by rows, then translation), drawn with
// depth test and an opaque cut after the static cloud.
#define SPLAT_MAX_BONES 64
#define SPLAT_MAX_SKIN_INSTANCES 256
int splat_skin_add_model(const Splat_Cloud *c);
int splat_skin_add_instance(int model);
void splat_skin_set_bones(int instance, const float *bones, int bone_n);
// bounding sphere of an instance in world space, for culling (radius <= 0: always drawn)
void splat_skin_set_bounds(int instance, const float center[3], float radius);
// out = (rotation yaw about +y, then translation) applied after each bone of frame `frame`
void splat_bones_place(float *out, const float *frame, int bone_n, float yaw, const float pos[3]);

// Builds a Splat_View from a cloud's embedded reference camera. Fatal if none.
Splat_View splat_view_from_cloud_camera(const Splat_Cloud *c);

// Orthographic view looking at `target` from yaw/pitch (radians, as ink-studio's
// viewMatrix), with `art_world` world units per art pixel and an art grid of
// art_w x art_h. The target is snapped to the art-pixel lattice of the view plane
// so panning moves the picture by whole art pixels; the dither phase follows.
Splat_View splat_view_ortho(float yaw, float pitch, const float target[3], float art_world, int art_w, int art_h,
                            float radius, float dither);

#endif
