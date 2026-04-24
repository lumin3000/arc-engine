#ifndef ARC_ENGINE_STATE_H
#define ARC_ENGINE_STATE_H

#include "../types.h"
#include <stdint.h>

// Engine-internal state shared between engine_main.c (owner) and the
// engine bindings layer (reader). This header replaces the prior
// mechanism where engine_bindings.c redeclared Game_State / Core_Context
// as local typedefs and pulled window_w / window_h / ctx / argv via bare
// `extern` — see docs/s16_review.md §A4, §A5.

typedef struct {
    uint64_t ticks;
    double   game_time_elapsed;
    Vec3     cam_pos;
    Vec3     cam_rot;
    Vec3     cam_vel;
    float    zoom_level;
    float    desired_zoom_level;
} Game_State;

typedef struct {
    Game_State *gs;
    float       delta_t;
} Core_Context;

// Owned by engine_main.c. Do not mutate from bindings except through the
// established setter paths (camera_set_position etc.).
extern Core_Context ctx;
extern int   window_w;
extern int   window_h;
extern float g_master_volume;

char **app_get_argv(void);
int    app_get_argc(void);

#endif
