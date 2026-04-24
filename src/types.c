
#include "types.h"
#include <string.h>

Sprite_Data sprite_data[SPRITE_NAME_COUNT] = {
    [Sprite_Name_nil] = {1, {0, 0}, Pivot_bottom_left},
    [Sprite_Name_fmod_logo] = {1, {0, 0}, Pivot_center_center},
    [Sprite_Name_player_still] = {1, {0, 0}, Pivot_bottom_center},
    [Sprite_Name_shadow_medium] = {1, {0, 0}, Pivot_center_center},
    [Sprite_Name_bg_repeat_tex0] = {1, {0, 0}, Pivot_bottom_left},
    [Sprite_Name_player_death] = {7, {0, 0}, Pivot_bottom_center},
    [Sprite_Name_player_run] = {3, {0, 0}, Pivot_bottom_center},
    [Sprite_Name_player_idle] = {2, {0, 0}, Pivot_bottom_center},
};

void get_sprite_offset(Sprite_Name sprite, Vec2 out_offset, Pivot* out_pivot) {
    if (sprite >= SPRITE_NAME_COUNT) {
        out_offset[0] = 0.0f;
        out_offset[1] = 0.0f;
        if (out_pivot) *out_pivot = Pivot_center_center;
        return;
    }

    Sprite_Data* data = &sprite_data[sprite];
    out_offset[0] = data->offset[0];
    out_offset[1] = data->offset[1];
    if (out_pivot) {
        *out_pivot = data->pivot;
    }
}

int get_frame_count(Sprite_Name sprite) {
    if (sprite >= SPRITE_NAME_COUNT) {
        return 1;
    }

    int frame_count = sprite_data[sprite].frame_count;
    if (frame_count == 0) {
        frame_count = 1;
    }
    return frame_count;
}

const char* sprite_name_to_string(Sprite_Name sprite) {
    switch (sprite) {
        case Sprite_Name_nil: return "nil";
        case Sprite_Name_fmod_logo: return "fmod_logo";
        case Sprite_Name_player_still: return "player_still";
        case Sprite_Name_shadow_medium: return "shadow_medium";
        case Sprite_Name_bg_repeat_tex0: return "bg_repeat_tex0";
        case Sprite_Name_player_death: return "player_death";
        case Sprite_Name_player_run: return "player_run";
        case Sprite_Name_player_idle: return "player_idle";
        default: return "unknown";
    }
}
