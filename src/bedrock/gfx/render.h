
#ifndef BALD_DRAW_RENDER_H
#define BALD_DRAW_RENDER_H

#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../types.h"
#include "../utils/array.h"
#include "../utils/shape.h"

#define MAX_QUADS 65536
#define MAX_VERTS (MAX_QUADS * 4)
#define MAX_INDICES (MAX_QUADS * 6)
#define FONT_BITMAP_W 256
#define FONT_BITMAP_H 256
#define CHAR_COUNT 96

#pragma pack(push, 1)
typedef struct {
  Vec3 pos;
  Vec4 col;
  Vec2 uv;
  uint8_t _pad0;
  uint8_t quad_flags;
  uint8_t _padding[2];
  Vec4 uv_rect;
} Vertex;
#pragma pack(pop)

typedef Vertex Quad[4];

typedef struct {
  sg_pass_action pass_action;
  sg_pipeline pip;
  sg_pipeline inst_pip;
  sg_pipeline inst_pip_alpha;
  sg_pipeline inst_pip_additive;
  sg_bindings bind;
} Render_State;

typedef struct {
  int32_t width;
  int32_t height;
  uint8_t tex_index;
  sg_image sg_img;
  uint8_t *data;
  Vec4 atlas_uvs;
} Sprite;

typedef struct {
  int w;
  int h;
  sg_image sg_image;
} Atlas;

typedef struct {
  void *char_data;
  sg_image sg_image;
} Font;

typedef struct {
  Matrix4 proj;
  Matrix4 camera;
} Coord_Space;

typedef struct {
  Quad *data;
  int count;
  int capacity;
} Quad_Array;

typedef struct {
  Vertex *data;
  int count;
  int capacity;
} Vertex_Array;

typedef struct {
  uint32_t *data;
  int count;
  int capacity;
} Index_Array;

typedef struct MaterialBlock {
  Vec4 color;
  Vec4 color_two;
  bool use_color;
  bool use_color_two;
} MaterialBlock;

typedef struct {
  int start_quad_index;
  int quad_count;
  uint8_t tex_index;
  Vec4 col_override;
  Vec4 col_override_2;
  Vec4 params;
  bool use_mvp;
  Matrix4 mvp;
  sg_view custom_view;    // when valid, temporarily switch texture binding (blood canvas etc.)
} Render_Batch;

typedef struct {
  Render_Batch *data;
  int count;
  int capacity;
} Batch_Array;

void batch_array_init(Batch_Array *arr, int initial_capacity);
void batch_array_free(Batch_Array *arr);
void batch_array_push(Batch_Array *arr, Render_Batch batch);

typedef struct {

  Quad_Array quads[ZLAYER_COUNT];
  Batch_Array batches[ZLAYER_COUNT];

  Vertex_Array tri_verts[ZLAYER_COUNT];
  Index_Array tri_indices[ZLAYER_COUNT];

  Coord_Space coord_space;
  ZLayer active_z_layer;
  Rect active_scissor;
  Quad_Flags active_flags;

  Shader_Data shader_data;

  int total_quad_count;
  int total_tri_vert_count;
  int total_tri_index_count;
} Draw_Frame;

void quad_array_init(Quad_Array *arr, int initial_capacity);
void quad_array_free(Quad_Array *arr);
void quad_array_push(Quad_Array *arr, const Vertex verts[4]);
void quad_array_insert(Quad_Array *arr, int index, const Vertex verts[4]);

void vertex_array_init(Vertex_Array *arr, int initial_capacity);
void vertex_array_free(Vertex_Array *arr);
int vertex_array_add(Vertex_Array *arr, const Vertex *vert);

void index_array_init(Index_Array *arr, int initial_capacity);
void index_array_free(Index_Array *arr);
void index_array_add_tri(Index_Array *arr, uint32_t i0, uint32_t i1,
                         uint32_t i2);

extern Render_State render_state;
extern Draw_Frame draw_frame;
extern Sprite sprites[SPRITE_NAME_COUNT];
extern Atlas atlas;
extern Font font;
extern Vec4 clear_col;

extern uint8_t actual_quad_data[MAX_QUADS * sizeof(Quad)];

#define DEFAULT_UV ((Vec4){0, 0, 1, 1})

void render_init(void);

void core_render_frame_start(void);
void core_render_frame_end(void);

void reset_draw_frame(void);

void render_set_sway_head(float sway_head);

float render_get_sway_head(void);

void load_sprites_into_atlas(void);

void load_font(void);

void set_coord_space(Coord_Space coord);
Coord_Space
push_coord_space(Coord_Space coord);

void set_z_layer(ZLayer zlayer);
ZLayer push_z_layer(ZLayer zlayer);

void draw_quad_projected(
    Matrix4 world_to_clip,
    Vec2 positions[4],
    Vec4 colors[4],
    Vec2 uvs[4],
    uint8_t tex_index, Vec2 sprite_size, Vec4 col_override, Vec4 col_override_2,
    ZLayer z_layer, Quad_Flags flags, Vec4 params,
    int z_layer_queue
);

void atlas_uv_from_sprite(Sprite_Name sprite, Vec4 out);

void get_sprite_size_render(Sprite_Name sprite, Vec2 out);

int get_current_quad_count(void);
int get_remaining_quad_count(void);
bool can_add_quads(int count);
float get_quad_usage_ratio(void);

typedef struct {
  int mesh_rq_0_4000;
  int quad_batches;
  int tri_meshes;
  int mesh_rq_4000_plus;
  int instanced;
  int imgui;
  int sdf_text;
  int total;
} DC_Breakdown;

extern DC_Breakdown g_dc_breakdown;
extern DC_Breakdown g_dc_breakdown_prev;

void dc_breakdown_reset(void);

#endif
