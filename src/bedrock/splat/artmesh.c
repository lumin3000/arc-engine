#include "artmesh.h"
#include "artsurface.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../log.h"
#include "splat_shader.h"
#include <stdlib.h>

struct Artmesh_Batch {
    sg_buffer geometry, instances;
    int vertices, count, capacity;
    struct Artmesh_Batch *next;
};
static Artmesh_Batch *batches;
static int visible_batches;
static sg_shader shader;
static sg_pipeline pipeline;

static void fail(const char *reason) {
    LOG_ERROR("[artmesh] %s\n", reason);
    exit(1);
}
static void make_pipeline(void) {
    shader = sg_make_shader(splat_post_enabled() ? artmesh_post_shader_desc(sg_query_backend()) : artmesh_shader_desc(sg_query_backend()));
    pipeline = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shader,
        .layout = {
            .buffers = {{.stride = sizeof(Artmesh_Vertex)},
                        {.stride = sizeof(Artmesh_Instance), .step_func = SG_VERTEXSTEP_PER_INSTANCE}},
            .attrs = {
                [ATTR_artmesh_position] = {.format = SG_VERTEXFORMAT_FLOAT3},
                [ATTR_artmesh_color] = {.format = SG_VERTEXFORMAT_UBYTE4N},
                [ATTR_artmesh_row0] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_FLOAT4},
                [ATTR_artmesh_row1] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_FLOAT4},
                [ATTR_artmesh_row2] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_FLOAT4},
                [ATTR_artmesh_top] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_UBYTE4N},
                [ATTR_artmesh_lit] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_UBYTE4N},
                [ATTR_artmesh_shade] = {.buffer_index = 1, .format = SG_VERTEXFORMAT_UBYTE4N},
            }},
        .depth = {.pixel_format = SG_PIXELFORMAT_DEPTH, .compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
        .color_count = splat_post_enabled() ? 3 : 2, .colors = {{.pixel_format = SG_PIXELFORMAT_RGBA8},{.pixel_format = SG_PIXELFORMAT_RGBA8},{.pixel_format=SG_PIXELFORMAT_RGBA8}},
        .sample_count = 1, .label = "artmesh-pipeline",
    });
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) fail("pipeline creation failed");
}
Artmesh_Batch *artmesh_create(const Artmesh_Vertex *vertices, int count) {
    if (!vertices || count <= 0 || count % 3) fail("expected a triangle list");
    if (!batches) make_pipeline();
    Artmesh_Batch *b = calloc(1, sizeof *b);
    if (!b) fail("out of memory");
    b->geometry = sg_make_buffer(&(sg_buffer_desc){
        .data = {vertices, (size_t)count * sizeof *vertices}, .label = "artmesh-triangles"});
    if (sg_query_buffer_state(b->geometry) != SG_RESOURCESTATE_VALID) fail("geometry upload failed");
    b->vertices = count;
    b->next = batches;
    batches = b;
    return b;
}
Artmesh_Batch *artmesh_create_box(void) {
    Artmesh_Vertex vertices[36];
    const int corners[6] = {0, 1, 3, 0, 3, 2};
    for (int face = 0; face < 6; face++) {
        const int axis = face / 2;
        const uint8_t swatch = face == 3 ? 0 : face == 0 || face == 5 ? 1 : 2;
        for (int i = 0; i < 6; i++) {
            Artmesh_Vertex *v = &vertices[face * 6 + i];
            v->position[axis] = (face & 1) - 0.5f;
            v->position[(axis + 1) % 3] = (corners[i] & 1) - 0.5f;
            v->position[(axis + 2) % 3] = (corners[i] >> 1) - 0.5f;
            v->color[0] = v->color[1] = v->color[2] = 255;
            v->color[3] = swatch;
        }
    }
    return artmesh_create(vertices, 36);
}
void artmesh_instances(Artmesh_Batch *b, const Artmesh_Instance *rows, int count) {
    if (count < 0 || (count && !rows)) fail("invalid instance stream");
    if (count > b->capacity) {
        if (b->instances.id) sg_destroy_buffer(b->instances);
        b->capacity = count;
        b->instances = sg_make_buffer(&(sg_buffer_desc){
            .size = (size_t)count * sizeof *rows, .usage.stream_update = true, .label = "artmesh-transforms"});
        if (sg_query_buffer_state(b->instances) != SG_RESOURCESTATE_VALID) fail("instance allocation failed");
    }
    if (count) sg_update_buffer(b->instances, &(sg_range){rows, (size_t)count * sizeof *rows});
    visible_batches += (count > 0) - (b->count > 0);
    b->count = count;
}
void artmesh_destroy(Artmesh_Batch *b) {
    Artmesh_Batch **link = &batches;
    while (*link && *link != b) link = &(*link)->next;
    if (!*link) fail("unknown geometry");
    *link = b->next;
    sg_destroy_buffer(b->geometry);
    if (b->instances.id) sg_destroy_buffer(b->instances);
    visible_batches -= b->count > 0;
    free(b);
    if (!batches) { sg_destroy_pipeline(pipeline); sg_destroy_shader(shader); }
}
bool artmesh_visible(void) { return visible_batches > 0 || artsurface_visible(); }
void artmesh_draw(const Splat_Mat4 *view, Splat_Vec4 cam, Splat_Vec4 dep, Splat_Vec4 zr, Splat_Vec4 tint) {
    artsurface_draw(view,cam,dep,zr,tint);
    if (!visible_batches) return;
    const vs_artmesh_params_t p = {.view = *view, .cam = cam, .dep = dep, .zr = zr, .backdrop = tint};
    sg_apply_pipeline(pipeline);
    sg_apply_uniforms(UB_vs_artmesh_params, &SG_RANGE(p));
    for (Artmesh_Batch *b = batches; b; b = b->next) {
        if (!b->count) continue;
        sg_apply_bindings(&(sg_bindings){.vertex_buffers = {b->geometry, b->instances}});
        sg_draw(0, b->vertices, b->count);
    }
}
