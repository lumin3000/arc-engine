
#include "sound.h"
#include "fmod_wrapper.h"
#include "../../log.h"
#include <stdatomic.h>
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

// ---------------------------------------------------------------------------
// 单文件音乐流（见 sound.h 线程契约）。
// music_lock 只保护 cmd/status 两个纯数据块；锁内不做 FMOD 调用、不做 IO。
// FMOD 资源 (music_sound/music_channel) 只被主线程触碰。

static atomic_flag music_lock = ATOMIC_FLAG_INIT;

static struct {
    // 命令信箱（任意线程写，主线程消费；后写覆盖先写——单曲语义）
    int pending_cmd;  // 0=none 1=play 2=stop
    char pending_path[SOUND_MUSIC_PATH_MAX];
    float pending_volume;
    // 状态快照（主线程写，任意线程读）
    Sound_Music_Status status;
    // 主线程私有：FMOD 资源
    FMOD_SOUND* music_sound;
    FMOD_CHANNEL* music_channel;
} music = {0};

static void music_lock_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&music_lock, memory_order_acquire)) {}
}
static void music_lock_release(void) {
    atomic_flag_clear_explicit(&music_lock, memory_order_release);
}

void sound_music_request_play(const char* path, float volume) {
    if (!path || !path[0] || strlen(path) >= SOUND_MUSIC_PATH_MAX) {
        LOG_ERROR("[sound] music play rejected: path empty or >= %d chars\n",
                  SOUND_MUSIC_PATH_MAX);
        music_lock_acquire();
        music.status.state = SOUND_MUSIC_FAILED;
        music.status.error_code = FMOD_ERR_INVALID_PARAM;
        snprintf(music.status.error_context, SOUND_MUSIC_ERRCTX_MAX, "path_validate");
        music_lock_release();
        return;
    }
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    music_lock_acquire();
    music.pending_cmd = 1;
    // 长度已校验 < SOUND_MUSIC_PATH_MAX，strncpy 必然 NUL 结尾
    strncpy(music.pending_path, path, SOUND_MUSIC_PATH_MAX);
    music.pending_volume = volume;
    music_lock_release();
}

void sound_music_request_stop(void) {
    music_lock_acquire();
    music.pending_cmd = 2;
    music.pending_path[0] = '\0';
    music_lock_release();
}

Sound_Music_Status sound_music_get_status(void) {
    music_lock_acquire();
    Sound_Music_Status s = music.status;
    music_lock_release();
    return s;
}

// 主线程专用：停声道、释放 sound。不改 status（调用者决定终态）。
static void music_release_current(void) {
    if (music.music_channel) {
        FMOD_RESULT r = FMOD_Channel_Stop(music.music_channel);
        // 声道自然播完后句柄失效，INVALID_HANDLE/CHANNEL_STOLEN 是预期
        if (r != FMOD_OK && r != FMOD_ERR_INVALID_HANDLE && r != FMOD_ERR_CHANNEL_STOLEN) {
            fmod_error_check(r, "Channel_Stop (music)");
        }
        music.music_channel = NULL;
    }
    if (music.music_sound) {
        fmod_error_check(FMOD_Sound_Release(music.music_sound), "Sound_Release (music)");
        music.music_sound = NULL;
    }
}

static void music_set_failed(FMOD_RESULT code, const char* context) {
    LOG_ERROR("[sound] music failed at %s: %s (%d)\n", context,
              FMOD_ErrorString(code), (int)code);
    music_lock_acquire();
    music.status.state = SOUND_MUSIC_FAILED;
    music.status.error_code = (int)code;
    snprintf(music.status.error_context, SOUND_MUSIC_ERRCTX_MAX, "%s", context);
    music.status.position_ms = 0;
    music_lock_release();
}

// 主线程每帧调用（sound_update 内）：先消费命令，再刷新播放状态。
static void music_main_thread_tick(void) {
    // 取走命令（锁内只拷数据）
    music_lock_acquire();
    int cmd = music.pending_cmd;
    char path[SOUND_MUSIC_PATH_MAX];
    float volume = music.pending_volume;
    if (cmd == 1) memcpy(path, music.pending_path, SOUND_MUSIC_PATH_MAX);
    music.pending_cmd = 0;
    music_lock_release();

    if (cmd == 2) {
        int had = (music.music_sound != NULL);
        music_release_current();
        if (had) {
            music_lock_acquire();
            music.status.state = SOUND_MUSIC_STOPPED;
            music.status.position_ms = 0;
            music_lock_release();
            LOG_INFO("[sound] music stopped\n");
        }
    } else if (cmd == 1) {
        music_release_current();  // 单曲：新播替换现曲，不叠播
        FMOD_SOUND* snd = NULL;
        FMOD_MODE mode = FMOD_MODE_CREATESTREAM | FMOD_MODE_LOOP_OFF |
                         FMOD_MODE_2D | FMOD_MODE_ACCURATETIME;
        FMOD_RESULT r = FMOD_System_CreateSound(state.core_system, path, mode, NULL, &snd);
        if (r != FMOD_OK) {
            music_set_failed(r, "System_CreateSound");
            return;
        }
        unsigned int length_ms = 0;
        r = FMOD_Sound_GetLength(snd, &length_ms, FMOD_TIMEUNIT_MS);
        if (r != FMOD_OK) {
            FMOD_Sound_Release(snd);
            music_set_failed(r, "Sound_GetLength");
            return;
        }
        FMOD_CHANNEL* ch = NULL;
        r = FMOD_System_PlaySound(state.core_system, snd, NULL, 0, &ch);
        if (r != FMOD_OK) {
            FMOD_Sound_Release(snd);
            music_set_failed(r, "System_PlaySound");
            return;
        }
        fmod_error_check(FMOD_Channel_SetVolume(ch, volume), "Channel_SetVolume (music)");
        music.music_sound = snd;
        music.music_channel = ch;
        music_lock_acquire();
        music.status.state = SOUND_MUSIC_PLAYING;
        music.status.position_ms = 0;
        music.status.length_ms = length_ms;
        music.status.error_code = 0;
        music.status.error_context[0] = '\0';
        music.status.play_serial++;
        music_lock_release();
        LOG_INFO("[sound] music playing: %s (%u ms)\n", path, length_ms);
    }

    // 刷新在播状态
    if (music.music_channel) {
        FMOD_BOOL playing = 0;
        FMOD_RESULT r = FMOD_Channel_IsPlaying(music.music_channel, &playing);
        if (r == FMOD_OK && playing) {
            unsigned int pos = 0;
            if (FMOD_Channel_GetPosition(music.music_channel, &pos, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                music_lock_acquire();
                music.status.position_ms = pos;
                music_lock_release();
            }
        } else {
            // 自然结束（EOS 后声道失效返回 INVALID_HANDLE 也走这里）
            music_release_current();
            music_lock_acquire();
            music.status.state = SOUND_MUSIC_ENDED;
            music.status.position_ms = 0;
            music_lock_release();
            LOG_INFO("[sound] music ended (EOS)\n");
        }
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

    // 音乐命令消费+状态刷新（主线程唯一拥有者）。放在 Studio_System_Update
    // 之后：Studio update 内部驱动同一 Core System，不再另调 Core update。
    music_main_thread_tick();
}

void sound_shutdown(void) {
    if (!state.initialized) return;
    music_release_current();
    music_lock_acquire();
    music.status.state = SOUND_MUSIC_IDLE;
    music.pending_cmd = 0;
    music_lock_release();
    FMOD_RESULT result = FMOD_Studio_System_Release(state.system);
    fmod_error_check(result, "Studio_System_Release");
    state.system = NULL;
    state.core_system = NULL;
    state.master_ch_group = NULL;
    state.initialized = false;
    LOG_INFO("[sound] shutdown complete\n");
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
