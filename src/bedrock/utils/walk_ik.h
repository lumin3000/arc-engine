// Distance-driven two-legged gait adapted from loktar00's sprite-stack-animation.
// See LICENSE.rig. Coordinates use y up and a normalized (x,z) facing direction.
#pragma once
#include <stdbool.h>
typedef struct { float x, y, z; } WalkPoint;
typedef struct {
    float stride, lift, duty, hip_width, thigh, shin, ankle, standing_hip;
} WalkDimensions;
typedef struct {
    float x, z, from_x, from_z, height, facing_x, facing_z;
    bool planted;
} WalkFoot;
typedef struct {
    WalkFoot foot[2];
    float x, z, phase, hip;
} WalkState;
void walk_place(WalkState *s, const WalkDimensions *dims, float x, float z, float fx, float fz, float phase);
void walk_step(WalkState *s, const WalkDimensions *dims, float x, float z, float fx, float fz, float dt);
WalkPoint walk_joint(WalkPoint root, WalkPoint tip, float upper, float lower, WalkPoint hint);
