// Portions follow ink-studio by siliconjungle (MIT, Copyright (c) 2026 siliconjungle; full
// notice in LICENSE.ink-studio next to this file): the splat data layout, the orthographic
// projection and falloff of cli/render.js.
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
// A filled axis-aligned rectangular patch in the same instance stream. The three
// diagonal slots hold squared half-extents (one zero); RGB, position and skinning are shared.
#define SPLAT_FLAT_PATCH (-4.0f)
// Density-four Gaussian anchored to a horizontal surface, with depth across its footprint.
#define SPLAT_GROUND_DAB (-5.0f)
#define SPLAT_MAX_PALETTE 64

typedef struct Splat_Cloud {
    uint32_t count;
    uint32_t pal_n;
    uint32_t bone_n;
    uint32_t frame_n;
    float fps;
    float radius;
    float bmin[3], bmax[3];
    bool has_cam;
    // yaw, pitch, dist, target xyz, eyeD, oh, focal, near, px, dither, W, H, has authored figure pitch, authored pitch (rad)
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
    float sky_phase, night, sky_tint[3], rain, clouds;
    float dither;
    float outline;      // 0 = reference look; 1 = dark contour on silhouettes and depth steps
    float outline_step; // world depth step that draws a contour when outline is on
    int phase_x, phase_y; // ordered-dither phase of art pixel (0,0), so the pattern stays on the world
    int scale;          // composite scale in physical px per art px; 0 = largest integer fit
    int static_cut;     // 1 = draw the static cloud with the opaque cut too (diagnostics)
    int lod_force;      // chunk worlds: -1 = pick the level from the art-pixel size, else that level
    int transparent;    // 1 = art pixels with no splat are transparent, so the composite lies over the host's frame
    int offset_px[2];   // composite offset in physical pixels (x right, y down), e.g. to undo art-pixel snapping
    float lamp_seconds, lamp_height; // local light animation clock and emitter elevation
} Splat_View;

typedef enum {
    SPLAT_SHOW_ART = 0,
    SPLAT_SHOW_NATIVE = 1,
    SPLAT_SHOW_SAMPLES = 2,
    // the native picture with its colours exactly as the resolve reads them, covering whatever is
    // behind (the plain one is laid over it by the target's alpha): for reading the target back
    // from a screenshot
    SPLAT_SHOW_NATIVE_RAW = 3,
} Splat_Show;

typedef struct Splat_Stats {
    uint32_t splats;
    uint32_t skinned_splats;  // drawn per frame (instances x model splats)
    int skin_instances;
    int chunks, chunks_drawn;       // static chunks in the world / drawn last frame
    uint64_t splats_drawn;          // static splats submitted last frame (per pass)
    int skin_drawn;                 // skinned instances drawn last frame
    int lod_level;                  // chunk level drawn last frame (last pass)
    int art_w, art_h;
    int scale;          // composite scale (physical px per target px)
    // per pass ([0] the 2x sample grid, [1] the art grid): static level drawn, static and skinned splats submitted
    int pass_lod[2];
    uint64_t pass_splats[2];
    uint64_t pass_skinned[2];
    uint64_t lod_splats[6];   // static splats held per level (all chunks)
    // GPU memory held: static buffers (every level), skinned model buffers, render targets with the bone texture
    uint64_t static_bytes, skin_bytes, target_bytes;
    // blended instances last frame: drawn, sorted again (with their splats and the CPU time of
    // sorting and handing the rows over), and the bytes their sorted buffers hold
    int blended_drawn, blended_sorted;
    uint64_t blended_sorted_splats, blended_bytes;
    double blended_sort_ms;
} Splat_Stats;

Splat_Cloud *splat_cloud_load(const char *path);
void splat_cloud_free(Splat_Cloud *c);
// A coarser copy of a cloud, for when it is drawn small: the splats that share a cubic cell of
// side `cell` (in a skinned cloud: and the same first bone) become one splat with their mean
// place, their spread and their average colour; a merged splat of a skinned cloud follows that
// one bone. Palette, bones, frames and camera are copied. The caller frees it.
Splat_Cloud *splat_cloud_merged(const Splat_Cloud *c, float cell);

// A slab from a top-down image (RGBA, rows top down; image up = +z, right = +x): every `step`-th
// opaque pixel becomes an upward dab at height `thickness`, and pixels on the silhouette's edge
// add a darker rim down to y = 0. Centred on the origin, world_per_px per image pixel, static
// (kind 0). NULL if no pixel is opaque.
Splat_Cloud *splat_cloud_from_icon(const uint8_t *rgba, int img_w, int img_h, int step, float world_per_px,
                                   float thickness, int alpha_cut, const uint32_t *pal, int pal_n);

// A solid from a triangle mesh (positions and normals 3 floats per vertex, rgb 3 bytes per vertex,
// 16-bit indices): every triangle is covered with flat dabs about `spacing` apart, facing the
// triangle's normal, coloured by the vertex colours times a fixed directional shade
// (ambient + (1 - ambient) * max(0, n . light), light normalised by the caller). Triangles
// facing straight down are left out. Coordinates are taken as they are (y up), static (kind 0).
// NULL if the mesh yields no dab.
Splat_Cloud *splat_cloud_from_mesh(const float *pos, const uint8_t *rgb, int vert_n, const uint16_t *idx, int idx_n,
                                   float spacing, const float light[3], float ambient, const uint32_t *pal, int pal_n);

// A figure from four orthographic views (RGBA, rows top down): views[0] seen from -z, [1] from +x,
// [2] from +z, [3] from -x; in each the image's right is the viewer's right (+x, +z, -x, -z).
// Every `step`-th image row becomes a ring of flat dabs: its x and z radii are the half-widths of
// the opaque run in the -z/+z and the +x/-x views; around the ring the colour blends the two
// views it faces (the side view's share is cos^2 of the angle to the x axis). The figure stands
// at y = 0 on its own axis, faces -z, is world_per_px per image pixel, and is made skinned
// (kind 12) with two bones: 0 below image row split_row, 1 at and above it. Bones and frames are
// the caller's to fill (bone_n = 2, frame_n = 0). NULL if no row has an opaque run.
Splat_Cloud *splat_cloud_from_views(const uint8_t *const views[4], int img_w, int img_h, int step,
                                    float world_per_px, int alpha_cut, int split_row,
                                    const uint32_t *pal, int pal_n);
// Portrait variant for the Arc crew plates: a shallow, round-cornered section with broad
// painted panels. The upper 40% trades crown for face height; the collar and hem stay registered.
Splat_Cloud *splat_cloud_from_portrait(const uint8_t *const views[4], int img_w, int img_h, int step,
                                    float world_per_px, int alpha_cut, int split_row,
                                    const uint32_t *pal, int pal_n);

// Configure once, before splat_init, to keep the baseline two-attachment path intact.
typedef struct { float outline, ridge, bloom, grade, dither; } Splat_Post;
void splat_configure_post(Splat_Post settings);
bool splat_post_enabled(void);
void splat_enable_sun(const uint8_t ramp_indices[64][3]);
void splat_snow_texture(const uint8_t *depth, int width, int height);
void splat_init(void);
void splat_upload(const Splat_Cloud *c, const float view[16]);
void splat_set_palette(const uint32_t *pal, int n);
void splat_set_lights(const float *data, int side);
void splat_set_view(const Splat_View *v);
void splat_render_offscreen(void);
void splat_composite(Splat_Show show);
Splat_Stats splat_stats(void);

// Occluders for the next composite: things the host already drew (e.g. 2D characters). Each is
// SPLAT_OCCLUDER_FLOATS floats: a world box — ground rectangle x0, z0, x1, z1 extruded up to
// height h — whose screen bounds are the area the drawing covers, and the world z of its foot
// (at the rectangle's middle x), whose depth decides who is in front. At most
// SPLAT_MAX_OCCLUDERS; more is fatal.
#define SPLAT_MAX_OCCLUDERS 32
#define SPLAT_OCCLUDER_FLOATS 6
void splat_set_occluders(const float *occ, int n);

// Chunked static worlds: copies of prototype clouds placed by translation into a grid of
// square chunks on x/z. splat_chunks_upload sorts each chunk back to front for the view
// direction, orders the chunks back to front, uploads one buffer and frees the CPU copy.
// Each frame only chunks whose bounds reach the view are drawn.
typedef struct Splat_Chunks Splat_Chunks;
// Levels of detail: level 0 is the placed splats; level l >= 1 merges them on a grid of cell
// SPLAT_LOD_CELL0 * 2^(l-1) (moment-matched: mean, covariance with spread, mean colour). A
// frame draws the coarsest level whose cell is no larger than one samples pixel.
#define SPLAT_LOD_LEVELS 6
#define SPLAT_LOD_CELL0 0.25f
// level 1's merge cell for worlds baked from now on (default SPLAT_LOD_CELL0); a host whose
// splats are finer than the default cell (e.g. at a small art pixel) sets a smaller one
void splat_set_lod_cell0(float cell);
Splat_Chunks *splat_chunks_new(float chunk_size, float x0, float z0, int nx, int nz);
void splat_chunks_add(Splat_Chunks *ch, const Splat_Cloud *proto, const float offset[3]);
// rows already in world space (SPLAT_FLOATS each), static
void splat_chunks_add_rows(Splat_Chunks *ch, const float *rows, uint32_t n);
uint64_t splat_chunks_count(const Splat_Chunks *ch);
void splat_chunks_upload(Splat_Chunks *ch, const float view[16], const uint32_t *pal, int pal_n, float radius);
// The same in two halves: bake (sort, levels, bounds; CPU only, touches no GPU or shared state,
// so it may run on any thread; consumes ch) and upload (GPU buffer, main thread; consumes baked).
typedef struct Splat_Baked Splat_Baked;
Splat_Baked *splat_chunks_bake(Splat_Chunks *ch, const float view[16]);
void splat_baked_upload(Splat_Baked *baked, const uint32_t *pal, int pal_n, float radius);

// Chunk worlds that change: the same grid, but every chunk has its own GPU buffer and is baked and
// replaced on its own, so an edit costs one chunk. splat_world_begin (main thread) replaces the
// static cloud with an empty grid, drawn back to front for the direction of `view`.
// splat_chunk_bake sorts one chunk's rows (world space, SPLAT_FLOATS each, static; malloc'd, taken
// over; n may be 0) for the view direction and builds its levels: CPU only, any thread.
// splat_world_set_chunk (main thread) puts the result at grid cell (cx, cz) in place of what was
// there and consumes it; splat_chunk_baked_free drops a result that is not wanted any more.
typedef struct Splat_Chunk_Baked Splat_Chunk_Baked;
void splat_world_begin(float chunk_size, float x0, float z0, int nx, int nz, const float view[16], const uint32_t *pal,
                       int pal_n, float radius);
Splat_Chunk_Baked *splat_chunk_bake(float *rows, uint32_t n, const float view[16]);
void splat_world_set_chunk(int cx, int cz, Splat_Chunk_Baked *baked);
void splat_chunk_baked_free(Splat_Chunk_Baked *baked);

// Skinned clouds (every splat kind 12, carried by bones): uploaded at rest; each instance has
// its own bone matrices (12 floats a bone: 3x3 rotation by rows, then translation), drawn with
// depth test and an opaque cut after the static cloud.
#define SPLAT_MAX_BONES 64
#define SPLAT_MAX_SKIN_INSTANCES 4096
int splat_skin_add_model(const Splat_Cloud *c);
// puts other splats in a model's place (the same number of bones); its instances stay as they are
void splat_skin_set_model(int model, const Splat_Cloud *c);
int splat_skin_add_instance(int model);
// an instance draws another model from now on (its bones must be set again before it is drawn)
void splat_skin_set_instance_model(int instance, int model);
void splat_skin_set_bones(int instance, const float *bones, int bone_n);
// bounding sphere of an instance in world space, for culling (radius <= 0: always drawn)
void splat_skin_set_bounds(int instance, const float center[3], float radius);
// pulls every splat of an instance towards rgb by a (0..1; 0, the default, leaves its colours alone)
void splat_skin_set_tint(int instance, const float rgba[4]);
// Own palettes: a look that keeps its colours apart from the scene's. A skinned model given an
// own palette is resolved against that palette and its dither wherever its splats make up at
// least half of an art pixel. At most SPLAT_MAX_OWN_PALETTES; returns the palette's number (>= 1).
#define SPLAT_MAX_OWN_PALETTES 15
int splat_palette_add(const uint32_t *pal, int n, float dither);
// a model's splats are resolved against own palette `palette` (0, the default: the scene's)
void splat_skin_set_model_palette(int model, int palette);
// Resolve a blended model on the output pixel grid, retaining scene occlusion.
void splat_skin_set_model_physical_pixels(int model);
// A blended model: its instances are drawn back to front with alpha blending, after everything
// else, instead of out of order with the opaque cut. Each instance is sorted by depth on the
// CPU from the model's rows (c: the cloud the model was made from; its rows are copied) and
// holds the sorted rows in a buffer of its own; it is sorted again only when its bones turn or
// move against each other, or the view direction changes.
void splat_skin_set_model_blended(int model, const Splat_Cloud *c);
// A blended model's splats are tested against the hardware depth, and written to it, `lead` world
// units nearer the eye than they are (0, the default: where they are). A splat has one depth for
// its whole quad, the depth of its centre: a broad flat splat of the ground reaches up the screen
// with the depth of its centre and so hides the lowest pixels of what stands on the ground just
// behind that centre. The lead is the allowance for that; the model shows through anything that
// stands less than `lead` in front of it.
void splat_skin_set_model_lead(int model, float lead);
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

// Oblique "top-down" projection on a cell grid (x east, z north, y up): the ground plane maps
// to the screen without foreshortening (screen x = x, screen up = z + k * y), so every cell is
// square on screen and ground picking is the inverse of a plain 2D top-down view; heights rise
// up the screen by k per unit. Depth runs along the line of sight (down and north).
Splat_View splat_view_oblique(float k, const float target[3], float art_world, int art_w, int art_h, float radius,
                              float dither);

#endif
