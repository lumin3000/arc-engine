
#include "render.h"
#include "../engine_state.h"
#include "../../../external/sokol/c/sokol_app.h"
#include "../../../external/sokol/c/sokol_gfx.h"

#include "../../../external/sokol/c/sokol_glue.h"
#include "../../../external/sokol/c/sokol_log.h"
#include "../../config.h"
#include "../screenshot.h"
#include "draw.h"
#include "generated_shader.h"
#include "graphics.h"
#include "mesh.h"


#undef ATTR_quad_position
#undef ATTR_quad_color0
#undef ATTR_quad_uv0
#undef ATTR_quad_local_uv0
#undef ATTR_quad_size0
#undef ATTR_quad_extras0
#undef ATTR_quad_uv_rect0
#undef ATTR_quad_inst_uv_rect0

#define ATTR_quad_position 0
#define ATTR_quad_color0 1
#define ATTR_quad_uv0 2
#define ATTR_quad_local_uv0 3
#define ATTR_quad_size0 4
#define ATTR_quad_extras0 5
#define ATTR_quad_uv_rect0 6
#define ATTR_quad_inst_uv_rect0                                                \
  6

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../../../external/stb/stb_image.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "../../../external/stb/stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../external/stb/stb_truetype.h"

#ifdef ODIN_OS_WINDOWS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/stb/stb_image_write.h"
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform.h"

Render_State render_state = {0};
Draw_Frame draw_frame = {0};
Sprite *sprites = NULL;  // allocated in render_init()
Atlas atlas = {0};
Font font = {0};
Vec4 clear_col = {0};

DC_Breakdown g_dc_breakdown = {0};
DC_Breakdown g_dc_breakdown_prev = {0};

void dc_breakdown_reset(void) {

  g_dc_breakdown_prev = g_dc_breakdown;
  memset(&g_dc_breakdown, 0, sizeof(DC_Breakdown));
}

uint8_t actual_quad_data[MAX_QUADS * sizeof(Quad)] = {0};

static float g_sway_head = 0.0f;

void render_set_sway_head(float sway_head) {
    g_sway_head = sway_head;
}

float render_get_sway_head(void) {
    return g_sway_head;
}

static void hex_to_rgba_vec4(uint32_t hex, Vec4 out) {
  out[0] = ((hex >> 24) & 0xFF) / 255.0f;
  out[1] = ((hex >> 16) & 0xFF) / 255.0f;
  out[2] = ((hex >> 8) & 0xFF) / 255.0f;
  out[3] = (hex & 0xFF) / 255.0f;
}

static bool vec4_equals(const Vec4 a, const Vec4 b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

void batch_array_init(Batch_Array *arr, int initial_capacity) {
  arr->data = (Render_Batch *)malloc(initial_capacity * sizeof(Render_Batch));
  arr->count = 0;
  arr->capacity = initial_capacity;
}

void batch_array_free(Batch_Array *arr) {
  if (arr->data) {
    free(arr->data);
    arr->data = NULL;
  }
  arr->count = 0;
  arr->capacity = 0;
}

void batch_array_push(Batch_Array *arr, Render_Batch batch) {
  if (arr->count >= arr->capacity) {
    int new_capacity = arr->capacity > 0 ? arr->capacity * 2 : 32;
    Render_Batch *new_data =
        (Render_Batch *)realloc(arr->data, new_capacity * sizeof(Render_Batch));
    assert(new_data != NULL);
    arr->data = new_data;
    arr->capacity = new_capacity;
  }
  arr->data[arr->count++] = batch;
}

void quad_array_init(Quad_Array *arr, int initial_capacity) {
  arr->data = (Quad *)malloc(initial_capacity * sizeof(Quad));
  arr->count = 0;
  arr->capacity = initial_capacity;
}

void quad_array_free(Quad_Array *arr) {
  if (arr->data) {
    free(arr->data);
    arr->data = NULL;
  }
  arr->count = 0;
  arr->capacity = 0;
}

void quad_array_push(Quad_Array *arr, const Vertex verts[4]) {

  if (arr->count >= arr->capacity) {
    int new_capacity = arr->capacity * 2;
    Quad *new_data = (Quad *)realloc(arr->data, new_capacity * sizeof(Quad));
    assert(new_data != NULL);
    arr->data = new_data;
    arr->capacity = new_capacity;
  }

  memcpy(arr->data[arr->count], verts, sizeof(Quad));
  arr->count++;
}

void quad_array_insert(Quad_Array *arr, int index, const Vertex verts[4]) {
  assert(index >= 0 && index <= arr->count);

  if (arr->count >= arr->capacity) {
    int new_capacity = arr->capacity * 2;
    Quad *new_data = (Quad *)realloc(arr->data, new_capacity * sizeof(Quad));
    assert(new_data != NULL);
    arr->data = new_data;
    arr->capacity = new_capacity;
  }

  if (index < arr->count) {
    memmove(&arr->data[index + 1], &arr->data[index],
            (arr->count - index) * sizeof(Quad));
  }

  memcpy(arr->data[index], verts, sizeof(Quad));
  arr->count++;
}

void vertex_array_init(Vertex_Array *arr, int initial_capacity) {
  arr->data = (Vertex *)malloc(initial_capacity * sizeof(Vertex));
  arr->count = 0;
  arr->capacity = initial_capacity;
}

void vertex_array_free(Vertex_Array *arr) {
  if (arr->data) {
    free(arr->data);
    arr->data = NULL;
  }
  arr->count = 0;
  arr->capacity = 0;
}

int vertex_array_add(Vertex_Array *arr, const Vertex *vert) {
  if (arr->count >= arr->capacity) {
    int new_capacity = arr->capacity * 2;
    Vertex *new_data =
        (Vertex *)realloc(arr->data, new_capacity * sizeof(Vertex));
    if (!new_data)
      return -1;
    arr->data = new_data;
    arr->capacity = new_capacity;
  }
  int idx = arr->count;
  memcpy(&arr->data[idx], vert, sizeof(Vertex));
  arr->count++;
  return idx;
}

void index_array_init(Index_Array *arr, int initial_capacity) {
  arr->data = (uint32_t *)malloc(initial_capacity * sizeof(uint32_t));
  arr->count = 0;
  arr->capacity = initial_capacity;
}

void index_array_free(Index_Array *arr) {
  if (arr->data) {
    free(arr->data);
    arr->data = NULL;
  }
  arr->count = 0;
  arr->capacity = 0;
}

void index_array_add_tri(Index_Array *arr, uint32_t i0, uint32_t i1,
                         uint32_t i2) {
  if (arr->count + 3 > arr->capacity) {
    int new_capacity = arr->capacity * 2;
    uint32_t *new_data =
        (uint32_t *)realloc(arr->data, new_capacity * sizeof(uint32_t));
    if (!new_data)
      return;
    arr->data = new_data;
    arr->capacity = new_capacity;
  }
  arr->data[arr->count++] = i0;
  arr->data[arr->count++] = i1;
  arr->data[arr->count++] = i2;
}

void load_sprites_into_atlas(void) {
  const char *img_dir = "res/images/";

  for (int img_name = 0; img_name < SPRITE_NAME_COUNT; img_name++) {
    if (img_name == Sprite_Name_nil)
      continue;

    const char *sprite_name_str = sprite_name_to_string((Sprite_Name)img_name);

    char path[256];
    snprintf(path, sizeof(path), "%s%s.png", img_dir, sprite_name_str);

    FILE *f = fopen(path, "rb");
    if (!f) {
      fprintf(stderr, "Failed to open: %s\n", path);
      assert(false);
    }

    fseek(f, 0, SEEK_END);
    long png_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *png_data = malloc(png_size);
    fread(png_data, 1, png_size, f);
    fclose(f);

    stbi_set_flip_vertically_on_load(1);
    int width, height, channels;
    uint8_t *img_data = stbi_load_from_memory(png_data, (int)png_size, &width,
                                              &height, &channels,
                                              4
    );

    free(png_data);

    if (!img_data) {
      fprintf(stderr, "stbi_load failed for %s: %s\n", path,
              stbi_failure_reason());
      assert(false);
    }

    sprites[img_name].width = width;
    sprites[img_name].height = height;
    sprites[img_name].data = img_data;
  }

  {
#define ATLAS_SIZE 1024
    atlas.w = ATLAS_SIZE;
    atlas.h = ATLAS_SIZE;

    stbrp_context cont;
    stbrp_node nodes[ATLAS_SIZE];
    stbrp_init_target(&cont, atlas.w, atlas.h, nodes, ATLAS_SIZE);

    int sprite_total = SPRITE_NAME_COUNT;
    stbrp_rect *rects = (stbrp_rect *)calloc(sprite_total > 0 ? sprite_total : 1, sizeof(stbrp_rect));
    int rect_count = 0;

    for (int id = 0; id < sprite_total; id++) {
      if (sprites[id].width == 0)
        continue;

      rects[rect_count].id = id;
      rects[rect_count].w = sprites[id].width + 2;
      rects[rect_count].h = sprites[id].height + 2;
      rect_count++;
    }

    int packed = stbrp_pack_rects(&cont, rects, rect_count);
    if (!packed) {
      fprintf(stderr, "Failed to pack all rects into atlas!\n");
      assert(false);
    }

    uint8_t *raw_data = calloc(atlas.w * atlas.h * 4, 1);

    for (int i = 0; i < rect_count; i++) {
      stbrp_rect *rect = &rects[i];
      Sprite *img = &sprites[rect->id];

      int rect_w = rect->w - 2;
      int rect_h = rect->h - 2;

      for (int row = 0; row < rect_h; row++) {
        uint8_t *src_row = &img->data[row * rect_w * 4];
        uint8_t *dest_row =
            &raw_data[((rect->y + 1 + row) * atlas.w + (rect->x + 1)) * 4];
        memcpy(dest_row, src_row, rect_w * 4);
      }

      stbi_image_free(img->data);
      img->data = NULL;

      img->atlas_uvs[0] = (float)(rect->x + 1) / (float)atlas.w;
      img->atlas_uvs[1] = (float)(rect->y + 1) / (float)atlas.h;
      img->atlas_uvs[2] =
          img->atlas_uvs[0] + (float)img->width / (float)atlas.w;
      img->atlas_uvs[3] =
          img->atlas_uvs[1] + (float)img->height / (float)atlas.h;
    }

#ifdef ODIN_OS_WINDOWS
    stbi_write_png("atlas.png", atlas.w, atlas.h, 4, raw_data, 4 * atlas.w);
#endif

    sg_image_desc desc = {
        .width = atlas.w,
        .height = atlas.h,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = raw_data,
                               .size = (size_t)(atlas.w * atlas.h * 4)}};
    atlas.sg_image = sg_make_image(&desc);

    if (atlas.sg_image.id == SG_INVALID_ID) {
      fprintf(stderr, "Failed to create atlas image\n");
      assert(false);
    }

    free(raw_data);
    free(rects);
  }
}

void load_font(void) {

  font.char_data = malloc(sizeof(stbtt_bakedchar) * CHAR_COUNT);

  uint8_t *bitmap = calloc(FONT_BITMAP_W * FONT_BITMAP_H, 1);
  float font_height = 14.0f;
  const char *path =
      "res/fonts/LiberationSans.ttf";

  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Failed to open font: %s\n", path);
    assert(false);
  }

  fseek(f, 0, SEEK_END);
  long ttf_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *ttf_data = malloc(ttf_size);
  fread(ttf_data, 1, ttf_size, f);
  fclose(f);

  int ret = stbtt_BakeFontBitmap(ttf_data, 0, font_height, bitmap,
                                 FONT_BITMAP_W, FONT_BITMAP_H,
                                 32,
                                 CHAR_COUNT, (stbtt_bakedchar *)font.char_data);

  free(ttf_data);

  if (ret <= 0) {
    fprintf(stderr, "Not enough space in font bitmap\n");
    assert(false);
  }

#ifdef ODIN_OS_WINDOWS

#endif

  sg_image_desc desc = {
      .width = FONT_BITMAP_W,
      .height = FONT_BITMAP_H,
      .pixel_format = SG_PIXELFORMAT_R8,
      .data.mip_levels[0] = {.ptr = bitmap,
                             .size = FONT_BITMAP_W * FONT_BITMAP_H}};
  font.sg_image = sg_make_image(&desc);

  if (font.sg_image.id == SG_INVALID_ID) {
    fprintf(stderr, "Failed to create font image\n");
    assert(false);
  }

  free(bitmap);
}

void render_init(void) {

  // Allocate sprite storage sized to the game-registered set (including nil).
  int sprite_count = engine_sprite_count();
  sprites = (Sprite *)calloc(sprite_count > 0 ? sprite_count : 1, sizeof(Sprite));

  reset_draw_frame();

  sg_desc sokol_desc = {
      .environment = sglue_environment(),
      .logger.func = slog_func,
      .buffer_pool_size = 32768,
      .image_pool_size = 32768,
      .view_pool_size = 32768,
  };
#ifdef ODIN_DEBUG
  sokol_desc.d3d11_shader_debugging = true;
#endif

  sg_setup(&sokol_desc);
  sg_enable_stats();

  mesh_register_main_thread();

  load_sprites_into_atlas();
  load_font();

  render_state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
      .usage.stream_update = true, .size = sizeof(actual_quad_data)});

  render_state.bind.vertex_buffers[1] = sg_make_buffer(&(sg_buffer_desc){
      .usage.stream_update = true,
      .size = 8192 * (sizeof(Matrix4) +
                      sizeof(Vec4)),
      .label = "instance-buffer"});

  render_state.bind.index_buffer =
      sg_make_buffer(&(sg_buffer_desc){.usage.index_buffer = true,
                                       .usage.stream_update = true,
                                       .size = sizeof(uint32_t) * MAX_INDICES});

  render_state.bind.samplers[SMP_default_sampler] =
      sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
      });

  render_state.bind.views[VIEW_tex0] = sg_make_view(
      &(sg_view_desc){.texture.image = atlas.sg_image, .label = "atlas-view"});
  render_state.bind.views[VIEW_font_tex] = sg_make_view(
      &(sg_view_desc){.texture.image = font.sg_image, .label = "font-view"});

  {
    uint8_t default_flow[4] = {128, 128, 0, 255};
    sg_image default_flow_img = sg_make_image(&(sg_image_desc){
      .width = 1, .height = 1,
      .pixel_format = SG_PIXELFORMAT_RGBA8,
      .data.mip_levels[0] = {.ptr = default_flow, .size = 4},
      .label = "default-flow-map",
    });
    render_state.bind.views[VIEW_flow_map] = sg_make_view(
      &(sg_view_desc){.texture.image = default_flow_img, .label = "default-flow-map-view"});
    render_state.bind.views[VIEW_ripple_tex] = sg_make_view(
      &(sg_view_desc){.texture.image = default_flow_img, .label = "default-ripple-view"});
    render_state.bind.views[VIEW_noise_tex] = sg_make_view(
      &(sg_view_desc){.texture.image = default_flow_img, .label = "default-noise-view"});
  }

  sg_pipeline_desc pipeline_desc = {
      .shader = sg_make_shader(quad_shader_desc(sg_query_backend())),
      .index_type = SG_INDEXTYPE_UINT32,
      .layout = {
          .buffers = {[0] = {.stride = sizeof(Vertex)}},
          .attrs = {

              [ATTR_quad_position] = {.format = SG_VERTEXFORMAT_FLOAT3,
                                      .offset =
                                          offsetof(Vertex, pos)},
              [ATTR_quad_color0] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                    .offset =
                                        offsetof(Vertex, col)},
              [ATTR_quad_uv0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                 .offset = offsetof(Vertex, uv)},
              [ATTR_quad_local_uv0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                       .offset =
                                           offsetof(Vertex, uv)},
              [ATTR_quad_size0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                   .offset = offsetof(Vertex, uv)},
              [ATTR_quad_extras0] = {.format = SG_VERTEXFORMAT_UBYTE4N,
                                     .offset = offsetof(Vertex, _pad0)},
              [ATTR_quad_uv_rect0] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                      .offset =
                                          offsetof(Vertex, uv_rect)},
          }}};

  pipeline_desc.colors[0].blend = (sg_blend_state){
      .enabled = true,
      .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
      .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .op_rgb = SG_BLENDOP_ADD,
      .src_factor_alpha = SG_BLENDFACTOR_ONE,
      .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .op_alpha = SG_BLENDOP_ADD,
  };

  render_state.pip = sg_make_pipeline(&pipeline_desc);

  sg_pipeline_desc inst_pipeline_desc =
      {.shader = sg_make_shader(quad_inst_shader_desc(sg_query_backend())),
       .index_type = SG_INDEXTYPE_UINT32,
       .layout =
           {.buffers =
                {
                    [0] = {.stride = sizeof(Vertex)},
                    [1] = {.stride = sizeof(Matrix4) + sizeof(Vec4),
                           .step_func = SG_VERTEXSTEP_PER_INSTANCE}

                },
            .attrs =
                {
                 [ATTR_quad_inst_position] =
                     {.format = SG_VERTEXFORMAT_FLOAT3,
                      .buffer_index = 0,
                      .offset = offsetof(Vertex, pos)},
                 [ATTR_quad_inst_color0] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                            .buffer_index = 0,
                                            .offset = offsetof(Vertex, col)},
                 [ATTR_quad_inst_uv0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                         .buffer_index = 0,
                                         .offset = offsetof(Vertex, uv)},
                 [ATTR_quad_inst_local_uv0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                               .buffer_index = 0,
                                               .offset = offsetof(Vertex, uv)},
                 [ATTR_quad_inst_size0] = {.format = SG_VERTEXFORMAT_FLOAT2,
                                           .buffer_index = 0,
                                           .offset = offsetof(Vertex, uv)},
                 [ATTR_quad_inst_extras0] = {.format = SG_VERTEXFORMAT_UBYTE4N,
                                             .buffer_index = 0,
                                             .offset = offsetof(
                                                 Vertex,
                                                 _pad0)},

                 [ATTR_quad_inst_inst_m0] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                             .buffer_index = 1,
                                             .offset = 0},

                 [ATTR_quad_inst_uv_rect0] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                              .buffer_index = 0,
                                              .offset =
                                                  offsetof(Vertex, uv_rect)},
                 [ATTR_quad_inst_inst_m1] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                             .buffer_index = 1,
                                             .offset = 16},
                 [ATTR_quad_inst_inst_m2] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                             .buffer_index = 1,
                                             .offset = 32},
                 [ATTR_quad_inst_inst_m3] = {.format = SG_VERTEXFORMAT_FLOAT4,
                                             .buffer_index = 1,
                                             .offset = 48},

                 [ATTR_quad_inst_inst_color] = {.format =
                                                    SG_VERTEXFORMAT_FLOAT4,
                                                .buffer_index = 1,
                                                .offset = 64}}},

       .colors[0].blend = pipeline_desc.colors[0].blend};

  render_state.inst_pip = sg_make_pipeline(&inst_pipeline_desc);

  inst_pipeline_desc.colors[0].blend = (sg_blend_state){
      .enabled = true,
      .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
      .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .op_rgb = SG_BLENDOP_ADD,
      .src_factor_alpha = SG_BLENDFACTOR_ONE,
      .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .op_alpha = SG_BLENDOP_ADD,
  };
  inst_pipeline_desc.depth.write_enabled =
      false;
  render_state.inst_pip_alpha = sg_make_pipeline(&inst_pipeline_desc);

  inst_pipeline_desc.colors[0].blend = (sg_blend_state){
      .enabled = true,
      .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
      .dst_factor_rgb = SG_BLENDFACTOR_ONE,
      .op_rgb = SG_BLENDOP_ADD,
      .src_factor_alpha = SG_BLENDFACTOR_ZERO,
      .dst_factor_alpha = SG_BLENDFACTOR_ONE,
      .op_alpha = SG_BLENDOP_ADD,
  };
  inst_pipeline_desc.depth.write_enabled = false;
  render_state.inst_pip_additive = sg_make_pipeline(&inst_pipeline_desc);

  hex_to_rgba_vec4(0x090a14ff, clear_col);

  render_state.pass_action = (sg_pass_action){
      .colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                    .clear_value = {clear_col[0], clear_col[1], clear_col[2],
                                    clear_col[3]}},
      .depth = {
          .load_action = SG_LOADACTION_CLEAR,
          .clear_value = 1.0f
      }};

  extern void graphics_init(void);
  graphics_init();

  extern void sdf_text_pipeline_init(void);
  sdf_text_pipeline_init();
}

void draw_quad_projected(Matrix4 world_to_clip, Vec2 positions[4],
                         Vec4 colors[4], Vec2 uvs[4], uint8_t tex_index,
                         Vec2 sprite_size, Vec4 col_override,
                         Vec4 col_override_2, ZLayer z_layer, Quad_Flags flags,
                         Vec4 params, int z_layer_queue) {
  ZLayer z_layer0 = z_layer;
  if (z_layer0 == ZLayer_nil) {
    z_layer0 = draw_frame.active_z_layer;
  }

  if (draw_frame.total_quad_count >= MAX_QUADS) {
    fprintf(stderr, "Quad limit exceeded! Current: %d, Max: %d\n",
            draw_frame.total_quad_count, MAX_QUADS);
    return;
  }

  Vertex verts[4];

  for (int i = 0; i < 4; i++) {
    float x = positions[i][0];
    float y = positions[i][1];
    float z = 0.0f;

    verts[i].pos[0] = world_to_clip[0] * x + world_to_clip[4] * y +
                      world_to_clip[8] * z + world_to_clip[12];

    verts[i].pos[1] = world_to_clip[1] * x + world_to_clip[5] * y +
                      world_to_clip[9] * z + world_to_clip[13];

    verts[i].pos[2] = world_to_clip[2] * x + world_to_clip[6] * y +
                      world_to_clip[10] * z + world_to_clip[14];

    verts[i].col[0] = colors[i][0];
    verts[i].col[1] = colors[i][1];
    verts[i].col[2] = colors[i][2];
    verts[i].col[3] = colors[i][3];

    verts[i].uv[0] = uvs[i][0];
    verts[i].uv[1] = uvs[i][1];

    verts[i]._pad0 = 0;
    verts[i].quad_flags = flags;
    verts[i]._padding[0] = 0;
    verts[i]._padding[1] = 0;

    verts[i].uv_rect[0] = 0;
    verts[i].uv_rect[1] = 0;
    verts[i].uv_rect[2] = 0;
    verts[i].uv_rect[3] = 0;
  }

  if (z_layer_queue == -1) {
    quad_array_push(&draw_frame.quads[z_layer0], verts);

    Batch_Array *batches = &draw_frame.batches[z_layer0];
    Render_Batch *last_batch =
        batches->count > 0 ? &batches->data[batches->count - 1] : NULL;
    int current_quad_count =
        draw_frame.quads[z_layer0].count;

    bool can_batch = false;
    if (last_batch) {
      if (last_batch->tex_index == tex_index &&
          vec4_equals(last_batch->col_override, col_override) &&
          vec4_equals(last_batch->col_override_2, col_override_2) &&
          vec4_equals(last_batch->params, params) &&
          (last_batch->start_quad_index + last_batch->quad_count ==
           current_quad_count - 1)) {
        can_batch = true;
      }
    }

    if (can_batch) {
      last_batch->quad_count++;
    } else {
      Render_Batch new_batch;
      memset(&new_batch, 0, sizeof(Render_Batch));
      new_batch.start_quad_index = current_quad_count - 1;
      new_batch.quad_count = 1;
      new_batch.tex_index = tex_index;
      memcpy(new_batch.col_override, col_override, sizeof(Vec4));
      memcpy(new_batch.col_override_2, col_override_2, sizeof(Vec4));
      memcpy(new_batch.params, params, sizeof(Vec4));
      batch_array_push(batches, new_batch);
    }
  } else {
    quad_array_insert(&draw_frame.quads[z_layer0], z_layer_queue, verts);
  }

  draw_frame.total_quad_count++;
}

static uint8_t merged_vertex_data[MAX_VERTS * sizeof(Vertex)];
static uint32_t merged_index_data[MAX_INDICES];

static int g_frame_started = 0;

void reset_draw_frame(void) {
  draw_frame.total_quad_count = 0;
  draw_frame.total_tri_vert_count = 0;
  draw_frame.total_tri_index_count = 0;

  draw_frame.shader_data.col_override[0] = 1.0f;
  draw_frame.shader_data.col_override[1] = 1.0f;
  draw_frame.shader_data.col_override[2] = 1.0f;
  draw_frame.shader_data.col_override[3] = 1.0f;
  draw_frame.shader_data.col_override_2[0] = 1.0f;
  draw_frame.shader_data.col_override_2[1] = 1.0f;
  draw_frame.shader_data.col_override_2[2] = 1.0f;
  draw_frame.shader_data.col_override_2[3] = 1.0f;

  for (int i = 0; i < ZLAYER_COUNT; i++) {

    if (!draw_frame.quads[i].data) {
      quad_array_init(&draw_frame.quads[i], 256);
    }
    draw_frame.quads[i].count = 0;

    if (!draw_frame.batches[i].data) {
      batch_array_init(&draw_frame.batches[i], 32);
    }
    draw_frame.batches[i].count = 0;

    if (!draw_frame.tri_verts[i].data) {
      vertex_array_init(&draw_frame.tri_verts[i], 256);
    }
    draw_frame.tri_verts[i].count = 0;

    if (!draw_frame.tri_indices[i].data) {
      index_array_init(&draw_frame.tri_indices[i], 256);
    }
    draw_frame.tri_indices[i].count = 0;
  }
}

void core_render_frame_start(void) {
  dc_breakdown_reset();
  reset_draw_frame();
  graphics_frame_begin();

  extern void sdf_text_begin_frame(void);
  sdf_text_begin_frame();

  g_frame_started = 1;
}

void core_render_frame_end(void) {

  if (!g_frame_started) {

    return;
  }
  g_frame_started = 0;

  int total_vert_count = 0;
  int total_index_count = 0;

  for (int layer = 0; layer < ZLAYER_COUNT; layer++) {

    if (draw_frame.quads[layer].data) {
      int quad_count = draw_frame.quads[layer].count;
      total_vert_count += quad_count * 4;
      total_index_count += quad_count * 6;
    }

    if (draw_frame.tri_verts[layer].data) {
      int cnt = draw_frame.tri_verts[layer].count;
      if (cnt < 0 || cnt > MAX_VERTS) {
        draw_frame.tri_verts[layer].count = 0;
      }
      total_vert_count += draw_frame.tri_verts[layer].count;
    }
    if (draw_frame.tri_indices[layer].data) {
      int cnt = draw_frame.tri_indices[layer].count;
      if (cnt < 0 || cnt > MAX_INDICES) {
        draw_frame.tri_indices[layer].count = 0;
      }
      total_index_count += draw_frame.tri_indices[layer].count;
    }
  }

  if (total_vert_count > MAX_VERTS || total_index_count > MAX_INDICES) {
    fprintf(stderr, "\n[FATAL] verts=%d (max %d), indices=%d (max %d)\n",
            total_vert_count, MAX_VERTS, total_index_count, MAX_INDICES);
    fflush(stderr);
    assert(0);
  }

  int vert_offset = 0;
  int index_offset = 0;

  for (int layer = 0; layer < ZLAYER_COUNT; layer++) {

    int quad_count =
        draw_frame.quads[layer].data ? draw_frame.quads[layer].count : 0;
    if (quad_count > 0) {

      memcpy(&merged_vertex_data[vert_offset * sizeof(Vertex)],
             draw_frame.quads[layer].data, quad_count * sizeof(Quad));

      for (int q = 0; q < quad_count; q++) {
        int base = vert_offset + q * 4;
        merged_index_data[index_offset++] = base + 0;
        merged_index_data[index_offset++] = base + 1;
        merged_index_data[index_offset++] = base + 2;
        merged_index_data[index_offset++] = base + 0;
        merged_index_data[index_offset++] = base + 2;
        merged_index_data[index_offset++] = base + 3;
      }
      vert_offset += quad_count * 4;
    }

    int tri_vert_count = draw_frame.tri_verts[layer].data
                             ? draw_frame.tri_verts[layer].count
                             : 0;
    int tri_index_count = draw_frame.tri_indices[layer].data
                              ? draw_frame.tri_indices[layer].count
                              : 0;

    if (tri_vert_count > 0 && tri_index_count > 0) {

      memcpy(&merged_vertex_data[vert_offset * sizeof(Vertex)],
             draw_frame.tri_verts[layer].data, tri_vert_count * sizeof(Vertex));

      for (int i = 0; i < tri_index_count; i++) {
        merged_index_data[index_offset++] =
            draw_frame.tri_indices[layer].data[i] + vert_offset;
      }
      vert_offset += tri_vert_count;
    }
  }

  {

    if (vert_offset > 0) {
      sg_update_buffer(render_state.bind.vertex_buffers[0],
                       &(sg_range){.ptr = merged_vertex_data,
                                   .size = vert_offset * sizeof(Vertex)});
    }

    if (index_offset > 0) {
      sg_update_buffer(render_state.bind.index_buffer,
                       &(sg_range){.ptr = merged_index_data,
                                   .size = index_offset * sizeof(uint32_t)});
    }

    sg_begin_pass(&(sg_pass){.action = render_state.pass_action,
                             .swapchain = sglue_swapchain()});

    sg_apply_pipeline(render_state.pip);
    sg_apply_bindings(&render_state.bind);

    VS_MVP_t vs_mvp = {0};
    xform_identity(vs_mvp.mvp);
    vs_mvp.use_mvp = 0.0f;
    vs_mvp.sway_head = g_sway_head;
    sg_apply_uniforms(UB_VS_MVP,
                      &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});

    sg_apply_uniforms(UB_Shader_Data,
                      &(sg_range){.ptr = &draw_frame.shader_data,
                                  .size = sizeof(Shader_Data)});

    int draw_offset = 0;

    Shader_Data sd = draw_frame.shader_data;

    for (int layer = 0; layer < ZLAYER_COUNT; layer++) {

      Batch_Array *batches = &draw_frame.batches[layer];
      if (batches->count > 0) {
        for (int b = 0; b < batches->count; b++) {
          Render_Batch *batch = &batches->data[b];

          // custom texture: temporarily switch binding
          bool has_custom_tex = (batch->custom_view.id != SG_INVALID_ID);
          if (has_custom_tex) {
            sg_bindings custom_bind = render_state.bind;
            custom_bind.views[VIEW_tex0] = batch->custom_view;
            sg_apply_bindings(&custom_bind);
          }

          sd.batch_props[0] = (float)batch->tex_index;
          memcpy(sd.col_override, batch->col_override, sizeof(Vec4));
          memcpy(sd.col_override_2, batch->col_override_2, sizeof(Vec4));
          memcpy(sd.params, batch->params, sizeof(Vec4));

          sg_apply_uniforms(
              UB_Shader_Data,
              &(sg_range){.ptr = &sd, .size = sizeof(Shader_Data)});

          if (batch->use_mvp) {
            VS_MVP_t batch_mvp = {0};
            memcpy(batch_mvp.mvp, batch->mvp, sizeof(Matrix4));
            batch_mvp.use_mvp = 1.0f;
            batch_mvp.sway_head = g_sway_head;
            sg_apply_uniforms(UB_VS_MVP,
                              &(sg_range){.ptr = &batch_mvp, .size = sizeof(VS_MVP_t)});
          }

          int count = batch->quad_count * 6;
          sg_draw(draw_offset, count, 1);
          draw_offset += count;
          g_dc_breakdown.quad_batches++;

          if (batch->use_mvp) {
            sg_apply_uniforms(UB_VS_MVP,
                              &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});
          }

          // restore atlas texture binding
          if (has_custom_tex) {
            sg_apply_bindings(&render_state.bind);
          }
        }
      } else {

        if (draw_frame.quads[layer].count > 0 && batches->count == 0) {

        }
      }

      int tri_idx_count = draw_frame.tri_indices[layer].count;
      if (tri_idx_count > 0) {

        sd.batch_props[0] = 0;
        sd.col_override[0] = 1.0f;
        sd.col_override[1] = 1.0f;
        sd.col_override[2] = 1.0f;
        sd.col_override[3] = 1.0f;
        sd.col_override_2[0] = 1.0f;
        sd.col_override_2[1] = 1.0f;
        sd.col_override_2[2] = 1.0f;
        sd.col_override_2[3] = 1.0f;
        memset(sd.params, 0, sizeof(Vec4));

        sg_apply_uniforms(UB_Shader_Data,
                          &(sg_range){.ptr = &sd, .size = sizeof(Shader_Data)});

        sg_draw(draw_offset, tri_idx_count, 1);
        draw_offset += tri_idx_count;
        g_dc_breakdown.tri_meshes++;
      }
    }

    // mesh queue submitted after quad batches, ensuring draw_mesh content renders above batch tiles
    extern int graphics_submit_meshes_range_count(int rq_min, int rq_max,
                                                  bool clear_after,
                                                  const Vec4 debug_color);
    g_dc_breakdown.mesh_rq_0_4000 =
        graphics_submit_meshes_range_count(0, 4000, false, NULL);

    {
      void (*hook)(void) = arc_engine_state()->post_mesh_render_hook;
      if (hook) hook();
    }

    sg_apply_pipeline(render_state.pip);
    sg_apply_bindings(&render_state.bind);
    sg_apply_uniforms(UB_VS_MVP,
                      &(sg_range){.ptr = &vs_mvp, .size = sizeof(VS_MVP_t)});

    g_dc_breakdown.mesh_rq_4000_plus =
        graphics_submit_meshes_range_count(4000, 999999, true, NULL);

    extern void draw_line_xz_flush(void);
    draw_line_xz_flush();

    extern int graphics_submit_instanced_count(void);
    g_dc_breakdown.instanced = graphics_submit_instanced_count();

    g_dc_breakdown.imgui = 0;

    extern struct font_manager *get_global_font_manager(void);
    extern int sdf_text_flush_count(int, int, struct font_manager *);
    struct font_manager *fm = get_global_font_manager();
    if (fm) {
      g_dc_breakdown.sdf_text = sdf_text_flush_count(sapp_width(), sapp_height(), fm);
    }

    extern int imgui_frame_is_active(void);
    extern void imgui_frame_reset(void);
    extern int simgui_get_draw_call_count(void);
    if (imgui_frame_is_active()) {
      extern void simgui_render_wrapper(void);
      simgui_render_wrapper();
      g_dc_breakdown.imgui = simgui_get_draw_call_count();
      imgui_frame_reset();
    }

    g_dc_breakdown.total = g_dc_breakdown.mesh_rq_0_4000
                         + g_dc_breakdown.quad_batches
                         + g_dc_breakdown.tri_meshes
                         + g_dc_breakdown.mesh_rq_4000_plus
                         + g_dc_breakdown.instanced
                         + g_dc_breakdown.imgui
                         + g_dc_breakdown.sdf_text;

    sg_end_pass();
  }

  sg_commit();

  if (screenshot_is_pending()) {
    screenshot_capture();
  }

  graphics_frame_end();

}

void set_coord_space(Coord_Space coord) { draw_frame.coord_space = coord; }

Coord_Space push_coord_space(Coord_Space coord) {
  Coord_Space og = draw_frame.coord_space;
  draw_frame.coord_space = coord;
  return og;
}

void set_z_layer(ZLayer zlayer) { draw_frame.active_z_layer = zlayer; }

ZLayer push_z_layer(ZLayer zlayer) {
  ZLayer og = draw_frame.active_z_layer;
  draw_frame.active_z_layer = zlayer;
  return og;
}

void atlas_uv_from_sprite(Sprite_Name sprite, Vec4 out) {
  memcpy(out, sprites[sprite].atlas_uvs, sizeof(Vec4));
}

void get_sprite_size_render(Sprite_Name sprite, Vec2 out) {
  out[0] = (float)sprites[sprite].width;
  out[1] = (float)sprites[sprite].height;
}

int get_current_quad_count(void) { return draw_frame.total_quad_count; }

int get_remaining_quad_count(void) {
  return MAX_QUADS - draw_frame.total_quad_count;
}

bool can_add_quads(int count) {
  return draw_frame.total_quad_count + count <= MAX_QUADS;
}

float get_quad_usage_ratio(void) {
  return (float)draw_frame.total_quad_count / (float)MAX_QUADS;
}
