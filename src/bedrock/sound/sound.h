#ifndef BALD_SOUND_H
#define BALD_SOUND_H

#include "../../types.h"

void sound_init(void);

void sound_update(Vec2 listener_pos, float volume);

typedef struct FMOD_STUDIO_EVENTINSTANCE FMOD_STUDIO_EVENTINSTANCE;

FMOD_STUDIO_EVENTINSTANCE* sound_play(const char* event_name, Vec2 pos, float cooldown_ms);

void sound_play_continuously(const char* event_name, const char* unique_id_suffix, Vec2 pos);

void sound_update_emitters(void);

typedef struct {
    Vec2 pos;
    float volume;
    bool loop;
} Sound_Opts;

#define SOUND_OPTS_DEFAULT (Sound_Opts){ \
    .pos = {0, 0}, \
    .volume = 1.0f, \
    .loop = false \
}

#endif
