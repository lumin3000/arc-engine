#ifndef BALD_HELPERS_H
#define BALD_HELPERS_H

#include "../types.h"
#include "gfx/render.h"

Coord_Space get_world_space(void);

Coord_Space get_screen_space(void);

void get_world_space_proj(Matrix4 out);

void get_world_space_camera(Matrix4 out);

float get_camera_zoom(void);

void get_screen_space_proj(Matrix4 out);

void screen_pivot(Pivot pivot, Vec2 out);

void raw_button(Rect rect, bool* out_hover, bool* out_pressed);

void mouse_pos_in_current_space(Vec2 out);

double app_now(void);

float time_since(double time);

void get_sprite_center_mass(Sprite_Name sprite, Vec2 out);

#endif
