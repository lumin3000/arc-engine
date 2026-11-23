// Opaque triangles composited in the pixel-art targets. Render-thread API.
// A batch owns immutable geometry and a replaceable stream of affine instances.
#pragma once
#include "splat.h"
typedef struct Artmesh_Batch Artmesh_Batch;
typedef struct {
    float position[3];
    uint8_t color[4]; // RGB; alpha selects the instance swatch: 0 top, 1 lit, 2 shade
} Artmesh_Vertex;
typedef struct {
    float transform[3][4]; // affine rows
    uint8_t swatch[3][4]; // RGB multipliers for authored vertex colour
} Artmesh_Instance;
Artmesh_Batch *artmesh_create(const Artmesh_Vertex *vertices, int count);
Artmesh_Batch *artmesh_create_box(void); // centred unit cube with three face swatches
// Optional shared albedo for vertices carrying UV in RG and light in B (tag 254).
void artmesh_albedo(const uint8_t *rgba, int width, int height);
void artmesh_instances(Artmesh_Batch *batch, const Artmesh_Instance *rows, int count);
void artmesh_destroy(Artmesh_Batch *batch);
bool artmesh_visible(void);
void artmesh_draw(const Splat_Mat4 *view, Splat_Vec4 cam, Splat_Vec4 dep,
                  Splat_Vec4 zr, Splat_Vec4 tint);
