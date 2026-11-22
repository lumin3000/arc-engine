// Immutable triangle lists baked on workers and uploaded on the render thread.
#pragma once
#include <stddef.h>
#include "splat.h"
typedef struct {
    float position[3];
    uint8_t tones[4][4]; // dark, base, light, accent (RGBA)
    uint8_t material[4]; // surface kind, snow depth, shore neighbour mask, reserved
} ArtSurfaceVertex;
typedef struct ArtSurface ArtSurface;
ArtSurface *artsurface_create(const ArtSurfaceVertex *vertices, int count);
void artsurface_destroy(ArtSurface *surface);
bool artsurface_visible(void);
size_t artsurface_bytes(void);
void artsurface_draw(const Splat_Mat4 *view, Splat_Vec4 cam, Splat_Vec4 dep, Splat_Vec4 zr, Splat_Vec4 tint);
