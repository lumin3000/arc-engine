
#include "sdf_text_pipeline.h"
#include "font_manager.h"
#include "render.h"

#include "../../../external/sokol/c/sokol_gfx.h"
#include "sdftext_shader.h"

#include <string.h>
#include <stdio.h>

#define MAX_TEXT_INSTANCES 4096

typedef struct {
    float x, y;
    float base_u, base_v;
    float glyph_w, glyph_h;
    float offset_x, offset_y;
    float tex_w, tex_h;
} SDF_Text_Vertex;

typedef struct {
    sg_pipeline pip;
    sg_buffer inst_buf;
    sg_image font_img;
    sg_view font_view;
    sg_sampler sampler;

    SDF_Text_Vertex instances[MAX_TEXT_INSTANCES];
    int instance_count;

    float current_color[4];

    int initialized;
    int first_flush;
} SDF_Text_State;

static SDF_Text_State g_sdf_text = {0};

void sdf_text_pipeline_init(void) {
    if (g_sdf_text.initialized) return;

    sg_shader shd = sg_make_shader(sdftext_shader_desc(sg_query_backend()));
    if (sg_query_shader_state(shd) != SG_RESOURCESTATE_VALID) {
        fprintf(stderr, "[sdf_text] Failed to create shader\n");
        return;
    }

    g_sdf_text.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .layout = {
            .buffers[0].step_func = SG_VERTEXSTEP_PER_INSTANCE,
            .attrs = {
                [ATTR_sdftext_position] = {.format = SG_VERTEXFORMAT_FLOAT2},
                [ATTR_sdftext_glyph_rect] = {.format = SG_VERTEXFORMAT_FLOAT4},
                [ATTR_sdftext_glyph_offset] = {.format = SG_VERTEXFORMAT_FLOAT4},
            }
        },
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ZERO
        },
        .shader = shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .label = "sdf-text-pipeline"
    });

    if (sg_query_pipeline_state(g_sdf_text.pip) != SG_RESOURCESTATE_VALID) {
        fprintf(stderr, "[sdf_text] Failed to create pipeline\n");
        return;
    }

    g_sdf_text.inst_buf = sg_make_buffer(&(sg_buffer_desc){
        .size = MAX_TEXT_INSTANCES * sizeof(SDF_Text_Vertex),
        .usage.stream_update = true,
        .label = "sdf-text-inst"
    });

    g_sdf_text.font_img = sg_make_image(&(sg_image_desc){
        .width = FONT_MANAGER_TEXSIZE,
        .height = FONT_MANAGER_TEXSIZE,
        .pixel_format = SG_PIXELFORMAT_R8,
        .usage.dynamic_update = true,
        .label = "sdf-font-texture"
    });

    g_sdf_text.font_view = sg_make_view(&(sg_view_desc){
        .texture.image = g_sdf_text.font_img,
        .label = "sdf-font-view"
    });

    g_sdf_text.sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .label = "sdf-font-sampler"
    });

    g_sdf_text.initialized = 1;
    g_sdf_text.instance_count = 0;
    g_sdf_text.current_color[0] = 1.0f;
    g_sdf_text.current_color[1] = 1.0f;
    g_sdf_text.current_color[2] = 1.0f;
    g_sdf_text.current_color[3] = 1.0f;

    fprintf(stderr, "[sdf_text] Pipeline initialized\n");
}

void sdf_text_pipeline_shutdown(void) {
    if (!g_sdf_text.initialized) return;

    sg_destroy_pipeline(g_sdf_text.pip);
    sg_destroy_buffer(g_sdf_text.inst_buf);
    sg_destroy_view(g_sdf_text.font_view);
    sg_destroy_image(g_sdf_text.font_img);
    sg_destroy_sampler(g_sdf_text.sampler);

    memset(&g_sdf_text, 0, sizeof(g_sdf_text));
}

void sdf_text_begin_frame(void) {
    g_sdf_text.instance_count = 0;
}

void sdf_text_add_glyph_ex(float x, float y,
                           int glyph_u, int glyph_v,
                           int glyph_w, int glyph_h,
                           int tex_w, int tex_h,
                           int offset_x, int offset_y,
                           uint32_t color) {
    if (g_sdf_text.instance_count >= MAX_TEXT_INSTANCES) {
        return;
    }

    SDF_Text_Vertex *inst = &g_sdf_text.instances[g_sdf_text.instance_count++];

    inst->x = x;
    inst->y = y;
    inst->base_u = (float)glyph_u;
    inst->base_v = (float)glyph_v;
    inst->glyph_w = (float)glyph_w;
    inst->glyph_h = (float)glyph_h;
    inst->offset_x = (float)offset_x;
    inst->offset_y = (float)offset_y;
    inst->tex_w = (float)tex_w;
    inst->tex_h = (float)tex_h;

    g_sdf_text.current_color[0] = ((color >> 16) & 0xFF) / 255.0f;
    g_sdf_text.current_color[1] = ((color >> 8) & 0xFF) / 255.0f;
    g_sdf_text.current_color[2] = (color & 0xFF) / 255.0f;
    g_sdf_text.current_color[3] = ((color >> 24) & 0xFF) / 255.0f;
}

void sdf_text_add_glyph(float x, float y,
                        int glyph_u, int glyph_v,
                        int glyph_w, int glyph_h,
                        int offset_x, int offset_y,
                        uint32_t color) {
    sdf_text_add_glyph_ex(x, y, glyph_u, glyph_v, glyph_w, glyph_h, glyph_w, glyph_h, offset_x, offset_y, color);
}

void sdf_text_add_font_glyph(float x, float y, const FontGlyph *g, uint32_t color) {
    sdf_text_add_glyph(x, y,
                       g->u, g->v,
                       g->w, g->h,
                       g->offset_x, g->offset_y,
                       color);
}

void sdf_text_flush(int screen_width, int screen_height, struct font_manager *font_mgr) {
    sdf_text_flush_count(screen_width, screen_height, font_mgr);
}

int sdf_text_flush_count(int screen_width, int screen_height, struct font_manager *font_mgr) {
    if (!g_sdf_text.initialized || g_sdf_text.instance_count == 0) {
        return 0;
    }

    int tex_size;
    const void *tex_data = font_manager_texture(font_mgr, &tex_size);
    int dirty = font_manager_flush(font_mgr);
    if (dirty || !g_sdf_text.first_flush) {
        g_sdf_text.first_flush = 1;
        sg_update_image(g_sdf_text.font_img, &(sg_image_data){
            .mip_levels[0] = {
                .ptr = tex_data,
                .size = tex_size * tex_size
            }
        });
    }

    sg_update_buffer(g_sdf_text.inst_buf, &(sg_range){
        .ptr = g_sdf_text.instances,
        .size = g_sdf_text.instance_count * sizeof(SDF_Text_Vertex)
    });

    sg_apply_pipeline(g_sdf_text.pip);

    sg_bindings bind = {
        .vertex_buffers[0] = g_sdf_text.inst_buf,
        .views[VIEW_tex] = g_sdf_text.font_view,
        .samplers[SMP_smp] = g_sdf_text.sampler,
    };
    sg_apply_bindings(&bind);

    vs_params_t vs_params = {
        .framesize = { 2.0f / screen_width, 2.0f / screen_height },
        .texsize = 1.0f / FONT_MANAGER_TEXSIZE,
        .glyphsize = (float)FONT_MANAGER_GLYPHSIZE
    };
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs_params));

    fs_params_t fs_params = {
        .edge_mask = font_manager_sdf_mask(font_mgr),
        .dist_multiplier = 1.0f,
        .color = {
            g_sdf_text.current_color[0],
            g_sdf_text.current_color[1],
            g_sdf_text.current_color[2],
            g_sdf_text.current_color[3]
        }
    };
    sg_apply_uniforms(UB_fs_params, &SG_RANGE(fs_params));

    sg_draw(0, 4, g_sdf_text.instance_count);

    g_sdf_text.instance_count = 0;
    return 1;
}

sg_image sdf_text_get_font_image(void) {
    return g_sdf_text.font_img;
}
