
#include "sound.h"
#include "fmod_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    FMOD_STUDIO_EVENTINSTANCE* event;
    uint64_t last_update_tick;
    char* unique_id;
} Sound_Emitter;

static struct {
    bool initialized;
    uint64_t sound_ticks;
    FMOD_STUDIO_SYSTEM* system;
    FMOD_SYSTEM* core_system;
    FMOD_STUDIO_BANK* bank;
    FMOD_STUDIO_BANK* strings_bank;
    FMOD_CHANNELGROUP* master_ch_group;

    Sound_Emitter* emitters;
    int emitter_count;
    int emitter_capacity;
} state = {0};

static void fmod_error_check(FMOD_RESULT result, const char* context) {
    if (result != FMOD_OK) {
        fprintf(stderr, "FMOD Error in %s: %s\n", context, FMOD_ErrorString(result));
    }
}

static void delete_emitter(Sound_Emitter* emitter) {
    if (emitter->unique_id) {
        free(emitter->unique_id);
        emitter->unique_id = NULL;
    }
    emitter->event = NULL;
    emitter->last_update_tick = 0;
}

void sound_init(void) {
    FMOD_RESULT result;

    result = FMOD_Debug_Initialize(
        FMOD_DEBUG_LEVEL_WARNING,
        FMOD_DEBUG_MODE_TTY,
        NULL,
        "fmod.file"
    );
    fmod_error_check(result, "Debug_Initialize");

    result = FMOD_Studio_System_Create(&state.system, FMOD_VERSION);
    fmod_error_check(result, "Studio_System_Create");

    result = FMOD_Studio_System_Initialize(
        state.system,
        512,
        FMOD_STUDIO_INIT_NORMAL,
        FMOD_INIT_NORMAL,
        NULL
    );
    fmod_error_check(result, "Studio_System_Initialize");

    result = FMOD_Studio_System_LoadBankFile(
        state.system,
        "res/fmod/Master.bank",
        FMOD_STUDIO_LOAD_BANK_NORMAL,
        &state.bank
    );
    fmod_error_check(result, "LoadBankFile (Master.bank)");

    result = FMOD_Studio_System_LoadBankFile(
        state.system,
        "res/fmod/Master.strings.bank",
        FMOD_STUDIO_LOAD_BANK_NORMAL,
        &state.strings_bank
    );
    fmod_error_check(result, "LoadBankFile (Master.strings.bank)");

    result = FMOD_Studio_System_GetCoreSystem(state.system, &state.core_system);
    fmod_error_check(result, "GetCoreSystem");

    result = FMOD_System_GetMasterChannelGroup(state.core_system, &state.master_ch_group);
    fmod_error_check(result, "GetMasterChannelGroup");

    state.emitter_capacity = 16;
    state.emitters = (Sound_Emitter*)calloc(state.emitter_capacity, sizeof(Sound_Emitter));
    state.emitter_count = 0;

    state.initialized = true;
    printf("Sound system initialized\n");
}

void sound_update(Vec2 listener_pos, float master_volume) {
    if (!state.initialized) {
        fprintf(stderr, "Warning: sound_update called but sound not initialized\n");
        return;
    }

    float vol = fminf(fmaxf(master_volume, 0.0f), 1.0f);
    FMOD_RESULT result = FMOD_ChannelGroup_SetVolume(state.master_ch_group, vol);
    fmod_error_check(result, "SetVolume");

    result = FMOD_Studio_System_Update(state.system);
    fmod_error_check(result, "System_Update");

    FMOD_3D_ATTRIBUTES attributes = {0};
    attributes.position.x = listener_pos[0];
    attributes.position.y = 0;
    attributes.position.z = listener_pos[1];
    attributes.forward.x = 0;
    attributes.forward.y = 0;
    attributes.forward.z = 1;
    attributes.up.x = 0;
    attributes.up.y = 1;
    attributes.up.z = 0;

    result = FMOD_Studio_System_SetListenerAttributes(state.system, 0, &attributes, NULL);
    fmod_error_check(result, "SetListenerAttributes");
}

FMOD_STUDIO_EVENTINSTANCE* sound_play(const char* name, Vec2 pos, float cooldown_ms) {
    if (!state.initialized) return NULL;

    FMOD_STUDIO_EVENTDESCRIPTION* event_desc;
    FMOD_RESULT result = FMOD_Studio_System_GetEvent(state.system, name, &event_desc);
    fmod_error_check(result, "GetEvent");
    if (result != FMOD_OK) return NULL;

    FMOD_STUDIO_EVENTINSTANCE* instance;
    result = FMOD_Studio_EventDescription_CreateInstance(event_desc, &instance);
    fmod_error_check(result, "CreateInstance");
    if (result != FMOD_OK) return NULL;

    result = FMOD_Studio_EventInstance_SetProperty(
        instance,
        FMOD_STUDIO_EVENT_PROPERTY_COOLDOWN,
        cooldown_ms / 1000.0f
    );
    fmod_error_check(result, "SetProperty (cooldown)");

    result = FMOD_Studio_EventInstance_Start(instance);
    fmod_error_check(result, "EventInstance_Start");

    const float INVALID_POS_VALUE = 99999.0f;
    if (fabsf(pos[0] - INVALID_POS_VALUE) > 0.1f && fabsf(pos[1] - INVALID_POS_VALUE) > 0.1f) {
        FMOD_3D_ATTRIBUTES attributes = {0};
        attributes.position.x = pos[0];
        attributes.position.y = 0;
        attributes.position.z = pos[1];
        attributes.forward.z = 1;
        attributes.up.y = 1;

        result = FMOD_Studio_EventInstance_Set3DAttributes(instance, &attributes);
        fmod_error_check(result, "Set3DAttributes");
    }

    result = FMOD_Studio_EventInstance_Release(instance);
    fmod_error_check(result, "EventInstance_Release");

    return instance;
}

void sound_play_continuously(const char* name, const char* unique_id_suffix, Vec2 pos) {
    if (!state.initialized) return;

    char unique_id[256];
    snprintf(unique_id, sizeof(unique_id), "%s%s", name, unique_id_suffix);

    for (int i = 0; i < state.emitter_count; i++) {
        if (strcmp(state.emitters[i].unique_id, unique_id) == 0) {

            state.emitters[i].last_update_tick = state.sound_ticks;

            const float INVALID_POS_VALUE = 99999.0f;
            if (fabsf(pos[0] - INVALID_POS_VALUE) > 0.1f) {
                FMOD_3D_ATTRIBUTES attributes;
                FMOD_RESULT result = FMOD_Studio_EventInstance_Get3DAttributes(
                    state.emitters[i].event,
                    &attributes
                );

                if (result == FMOD_OK) {
                    attributes.position.x = pos[0];
                    attributes.position.z = pos[1];
                    FMOD_Studio_EventInstance_Set3DAttributes(state.emitters[i].event, &attributes);
                }
            }
            return;
        }
    }

    if (state.emitter_count >= state.emitter_capacity) {
        state.emitter_capacity *= 2;
        state.emitters = (Sound_Emitter*)realloc(
            state.emitters,
            state.emitter_capacity * sizeof(Sound_Emitter)
        );
    }

    Sound_Emitter* emitter = &state.emitters[state.emitter_count++];
    emitter->event = sound_play(name, pos, 40.0f);
    emitter->last_update_tick = state.sound_ticks;
    emitter->unique_id = strdup(unique_id);

    printf("New sound emitter: %s\n", unique_id);
}

void sound_update_emitters(void) {
    if (!state.initialized) return;

    for (int i = state.emitter_count - 1; i >= 0; i--) {
        if (state.emitters[i].last_update_tick != state.sound_ticks) {

            FMOD_RESULT result = FMOD_Studio_EventInstance_Stop(
                state.emitters[i].event,
                FMOD_STUDIO_STOP_ALLOWFADEOUT
            );

            if (result != FMOD_OK) {
                fprintf(stderr, "Warning: Failed to stop emitter\n");
            }

            delete_emitter(&state.emitters[i]);

            for (int j = i; j < state.emitter_count - 1; j++) {
                state.emitters[j] = state.emitters[j + 1];
            }
            state.emitter_count--;

            printf("Killed sound emitter\n");
        }
    }

    state.sound_ticks++;
}
