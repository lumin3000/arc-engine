
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

bool sound_music_request_play(const char* path, float volume) {
    // 请求级参数错误：拒绝本次请求即可，不触碰 status/pending——
    // 现曲若在播必须继续播，全局状态不能被一次坏调用污染。
    if (!path || !path[0] || strlen(path) >= SOUND_MUSIC_PATH_MAX) {
        LOG_ERROR("[sound] music play rejected: path empty or >= %d chars\n",
                  SOUND_MUSIC_PATH_MAX);
        return false;
    }
    if (!(volume >= 0.0f && volume <= 1.0f)) {  // NaN/Inf/越界一并拒绝
        LOG_ERROR("[sound] music play rejected: volume %f not in [0,1]\n",
                  (double)volume);
        return false;
    }
    music_lock_acquire();
    music.pending_cmd = 1;
    // 长度已校验 < SOUND_MUSIC_PATH_MAX，strncpy 必然 NUL 结尾
    strncpy(music.pending_path, path, SOUND_MUSIC_PATH_MAX);
    music.pending_volume = volume;
    music_lock_release();
    return true;
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
        // 先 paused 起播，音量设置成功后才解除暂停，避免初始一瞬满音量
        FMOD_CHANNEL* ch = NULL;
        r = FMOD_System_PlaySound(state.core_system, snd, NULL, 1, &ch);
        if (r != FMOD_OK) {
            FMOD_Sound_Release(snd);
            music_set_failed(r, "System_PlaySound");
            return;
        }
        r = FMOD_Channel_SetVolume(ch, volume);
        if (r != FMOD_OK) {
            FMOD_Channel_Stop(ch);
            FMOD_Sound_Release(snd);
            music_set_failed(r, "Channel_SetVolume");
            return;
        }
        r = FMOD_Channel_SetPaused(ch, 0);
        if (r != FMOD_OK) {
            FMOD_Channel_Stop(ch);
            FMOD_Sound_Release(snd);
            music_set_failed(r, "Channel_SetPaused");
            return;
        }
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

    // 刷新在播状态。错误分类：
    //   IsPlaying OK + false            → 正常 EOS
    //   IsPlaying ERR_INVALID_HANDLE    → 也判 EOS。依据：FMOD Core 的 Channel
    //     是复用型轻句柄，声音自然播完后句柄即失效，后续 Channel API 返回
    //     FMOD_ERR_INVALID_HANDLE（FMOD 2.02 docs, Core API Channel /
    //     "Channel handles ... become invalid when the sound finishes"）。
    //     本处 channel 只被本模块持有、上一帧还在播，失效的唯一正常来源
    //     就是曲尾。
    //   IsPlaying ERR_CHANNEL_STOLEN    → 不是曲尾：声道被虚拟化/抢占，属
    //     异常（音乐是唯一 Core 声道，512 上限下不应发生），按 API 错误记
    //     failed 保留 code/context。
    //   其他错误                        → failed，保留 code/context。
    // 所有离开 PLAYING 的分支都释放资源。
    if (music.music_channel) {
        FMOD_BOOL playing = 0;
        FMOD_RESULT r = FMOD_Channel_IsPlaying(music.music_channel, &playing);
        if (r == FMOD_OK && playing) {
            unsigned int pos = 0;
            FMOD_RESULT rp = FMOD_Channel_GetPosition(music.music_channel, &pos, FMOD_TIMEUNIT_MS);
            if (rp == FMOD_OK) {
                music_lock_acquire();
                music.status.position_ms = pos;
                music_lock_release();
            } else if (rp == FMOD_ERR_INVALID_HANDLE) {
                // IsPlaying 成功与 GetPosition 之间声音恰好自然播完的竞态：
                // 句柄失效语义与上面 EOS 判定同源，按正常曲尾收束，不记 failed
                music_release_current();
                music_lock_acquire();
                music.status.state = SOUND_MUSIC_ENDED;
                music.status.position_ms = 0;
                music_lock_release();
                LOG_INFO("[sound] music ended (EOS, GetPosition=INVALID_HANDLE race)\n");
            } else {
                // 其他位置查询失败不是可继续状态，按错误收束（不吞）
                music_release_current();
                music_set_failed(rp, "Channel_GetPosition");
            }
        } else if (r == FMOD_OK || r == FMOD_ERR_INVALID_HANDLE) {
            music_release_current();
            music_lock_acquire();
            music.status.state = SOUND_MUSIC_ENDED;
            music.status.position_ms = 0;
            music_lock_release();
            LOG_INFO("[sound] music ended (EOS, IsPlaying=%s)\n", FMOD_ErrorString(r));
        } else {
            music_release_current();
            music_set_failed(r, "Channel_IsPlaying");
        }
    }
}

// ---------------------------------------------------------------------------
// 单次 2D 短声效（cue，见 sound.h 契约）。与音乐同模型：cue_lock 只保护
// 命令环与状态快照两个纯数据块，锁内不做 FMOD 调用、不做 IO；样本缓存与
// 活声位是主线程私有。与音乐的信箱/锁/资源完全分离，互不覆盖。

static atomic_flag cue_lock = ATOMIC_FLAG_INIT;

typedef struct {
    char path[SOUND_MUSIC_PATH_MAX];
    float volume;
} Sound_Cue_Cmd;

typedef struct {
    char path[SOUND_MUSIC_PATH_MAX];
    FMOD_SOUND* sound;
} Sound_Cue_Sample;

static struct {
    // 命令环（任意线程写，主线程整批消费；容量 = 单帧上限）
    Sound_Cue_Cmd pending[SOUND_CUE_QUEUE_CAP];
    int pending_count;
    // 状态快照（主线程写，任意线程读；rejected_queue_full 在请求线程累加）
    Sound_Cue_Status status;
    // 主线程私有：常驻样本缓存 + 活声位
    Sound_Cue_Sample samples[SOUND_CUE_SAMPLE_CACHE_CAP];
    int sample_count;
    FMOD_CHANNEL* voices[SOUND_CUE_MAX_VOICES];
    int voice_count;
} cue = {0};

static void cue_lock_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&cue_lock, memory_order_acquire)) {}
}
static void cue_lock_release(void) {
    atomic_flag_clear_explicit(&cue_lock, memory_order_release);
}

Sound_Cue_Request_Result sound_cue_request_play(const char* path, float volume) {
    // 参数无效：同步拒绝本次请求，不入队、不触碰任何状态——已播实例继续播
    if (!path || !path[0] || strlen(path) >= SOUND_MUSIC_PATH_MAX) {
        LOG_ERROR("[sound] cue play rejected: path empty or >= %d chars\n",
                  SOUND_MUSIC_PATH_MAX);
        return SOUND_CUE_REJECT_INVALID;
    }
    if (!(volume >= 0.0f && volume <= 1.0f)) {  // NaN/Inf/越界一并拒绝
        LOG_ERROR("[sound] cue play rejected: volume %f not in [0,1]\n",
                  (double)volume);
        return SOUND_CUE_REJECT_INVALID;
    }
    Sound_Cue_Request_Result result;
    cue_lock_acquire();
    if (cue.pending_count >= SOUND_CUE_QUEUE_CAP) {
        cue.status.rejected_queue_full_total++;
        result = SOUND_CUE_REJECT_QUEUE_FULL;
    } else {
        Sound_Cue_Cmd* cmd = &cue.pending[cue.pending_count++];
        // 长度已校验 < SOUND_MUSIC_PATH_MAX，strncpy 必然 NUL 结尾
        strncpy(cmd->path, path, SOUND_MUSIC_PATH_MAX);
        cmd->volume = volume;
        cue.status.accepted_total++;
        result = SOUND_CUE_ACCEPTED;
    }
    cue_lock_release();
    if (result == SOUND_CUE_REJECT_QUEUE_FULL) {
        LOG_ERROR("[sound] cue play rejected: queue full (%d pending)\n",
                  SOUND_CUE_QUEUE_CAP);
    }
    return result;
}

Sound_Cue_Status sound_cue_get_status(void) {
    cue_lock_acquire();
    Sound_Cue_Status s = cue.status;
    cue_lock_release();
    return s;
}

// 主线程专用：记录一次 cue 失败（code=-1 表示非 FMOD 的缓存满）
static void cue_set_failed(int code, const char* context) {
    LOG_ERROR("[sound] cue failed at %s: %s (%d)\n", context,
              code >= 0 ? FMOD_ErrorString((FMOD_RESULT)code) : "sample cache full",
              code);
    cue_lock_acquire();
    cue.status.failed_total++;
    cue.status.last_error_code = code;
    snprintf(cue.status.last_error_context, SOUND_MUSIC_ERRCTX_MAX, "%s", context);
    cue_lock_release();
}

// 主线程专用：查缓存，未命中则加载并缓存。失败返回 NULL（已记 status）。
static FMOD_SOUND* cue_sample_acquire(const char* path) {
    for (int i = 0; i < cue.sample_count; i++) {
        if (strcmp(cue.samples[i].path, path) == 0) return cue.samples[i].sound;
    }
    if (cue.sample_count >= SOUND_CUE_SAMPLE_CACHE_CAP) {
        cue_set_failed(-1, "Cue_SampleCacheFull");
        return NULL;
    }
    FMOD_SOUND* snd = NULL;
    FMOD_MODE mode = FMOD_MODE_CREATESAMPLE | FMOD_MODE_LOOP_OFF | FMOD_MODE_2D;
    FMOD_RESULT r = FMOD_System_CreateSound(state.core_system, path, mode, NULL, &snd);
    if (r != FMOD_OK) {
        cue_set_failed((int)r, "Cue_CreateSound");
        return NULL;
    }
    Sound_Cue_Sample* slot = &cue.samples[cue.sample_count++];
    strncpy(slot->path, path, SOUND_MUSIC_PATH_MAX);
    slot->sound = snd;
    LOG_INFO("[sound] cue sample cached: %s\n", path);
    return snd;
}

// 主线程每帧调用（sound_update 内）：先回收已播完声位，再消费命令。
// 顺序不可倒——若先分配后回收，本帧实际已结束的声位会被容量判断误当
// 仍占用，8 声位满时丢掉本该能播的新请求（Codex 首版审查发现 1）。
static void cue_main_thread_tick(void) {
    // 活声位轮询回收。EOS 判定与音乐同源（IsPlaying OK+false 或
    // INVALID_HANDLE=句柄自然失效）。样本常驻缓存，声位回收不 Release sound。
    for (int i = cue.voice_count - 1; i >= 0; i--) {
        FMOD_BOOL playing = 0;
        FMOD_RESULT r = FMOD_Channel_IsPlaying(cue.voices[i], &playing);
        if (r == FMOD_OK && playing) continue;
        if (r == FMOD_OK || r == FMOD_ERR_INVALID_HANDLE) {
            cue.voices[i] = cue.voices[--cue.voice_count];
            cue_lock_acquire();
            cue.status.ended_total++;
            cue.status.active_voices = cue.voice_count;
            cue_lock_release();
            LOG_INFO("[sound] cue ended (voices %d)\n", cue.voice_count);
            continue;
        }
        // 非自然 EOS 的 IsPlaying 错误：句柄状态不明。退役跟踪前必须先
        // Stop，避免留下无人拥有的活声道（Codex 首版审查发现 2）。原始
        // IsPlaying 错误先记录，不被 Stop 结果吞掉。
        cue_set_failed((int)r, "Cue_IsPlaying");
        FMOD_RESULT rs = FMOD_Channel_Stop(cue.voices[i]);
        if (rs == FMOD_OK || rs == FMOD_ERR_INVALID_HANDLE || rs == FMOD_ERR_CHANNEL_STOLEN) {
            // 已停 / 句柄已失效 / 已被抢占——确认无活声道，安全退役
            cue.voices[i] = cue.voices[--cue.voice_count];
            cue_lock_acquire();
            cue.status.active_voices = cue.voice_count;
            cue_lock_release();
            LOG_ERROR("[sound] cue voice retired after IsPlaying error (Stop=%s)\n",
                      FMOD_ErrorString(rs));
        } else {
            // Stop 也异常失败：声道可能仍在播，保留跟踪下帧重试，
            // 不失去拥有者；失败明确记录（覆盖 last_error 为 Stop 错误，
            // IsPlaying 错误已在上面计数+日志留痕）
            cue_set_failed((int)rs, "Cue_StopAfterIsPlayingError");
        }
    }

    // 整批取走命令（锁内只拷数据）
    Sound_Cue_Cmd batch[SOUND_CUE_QUEUE_CAP];
    int batch_count;
    cue_lock_acquire();
    batch_count = cue.pending_count;
    if (batch_count > 0)
        memcpy(batch, cue.pending, (size_t)batch_count * sizeof(Sound_Cue_Cmd));
    cue.pending_count = 0;
    cue_lock_release();

    for (int i = 0; i < batch_count; i++) {
        if (cue.voice_count >= SOUND_CUE_MAX_VOICES) {
            // 并发满：丢弃本条，已播实例与音乐都不动
            cue_lock_acquire();
            cue.status.dropped_voice_limit_total++;
            cue_lock_release();
            LOG_ERROR("[sound] cue dropped: voice limit %d reached\n",
                      SOUND_CUE_MAX_VOICES);
            continue;
        }
        FMOD_SOUND* snd = cue_sample_acquire(batch[i].path);
        if (!snd) continue;  // 失败已入 status
        // 先 paused 起播，音量设置成功后才解除暂停（同音乐，防满音量瞬时）
        FMOD_CHANNEL* ch = NULL;
        FMOD_RESULT r = FMOD_System_PlaySound(state.core_system, snd, NULL, 1, &ch);
        if (r != FMOD_OK) {
            cue_set_failed((int)r, "Cue_PlaySound");
            continue;
        }
        r = FMOD_Channel_SetVolume(ch, batch[i].volume);
        if (r != FMOD_OK) {
            FMOD_Channel_Stop(ch);
            cue_set_failed((int)r, "Cue_SetVolume");
            continue;
        }
        r = FMOD_Channel_SetPaused(ch, 0);
        if (r != FMOD_OK) {
            FMOD_Channel_Stop(ch);
            cue_set_failed((int)r, "Cue_SetPaused");
            continue;
        }
        cue.voices[cue.voice_count++] = ch;
        cue_lock_acquire();
        cue.status.started_total++;
        cue.status.active_voices = cue.voice_count;
        cue_lock_release();
        LOG_INFO("[sound] cue started: %s (voices %d)\n", batch[i].path, cue.voice_count);
    }
}

// 主线程退出收尾：停活声位、释放常驻样本。仅 sound_shutdown 调用。
static void cue_release_all(void) {
    for (int i = 0; i < cue.voice_count; i++) {
        FMOD_RESULT r = FMOD_Channel_Stop(cue.voices[i]);
        // 播完后句柄自然失效，INVALID_HANDLE/CHANNEL_STOLEN 是预期
        if (r != FMOD_OK && r != FMOD_ERR_INVALID_HANDLE && r != FMOD_ERR_CHANNEL_STOLEN) {
            fmod_error_check(r, "Channel_Stop (cue)");
        }
        cue.voices[i] = NULL;
    }
    cue.voice_count = 0;
    for (int i = 0; i < cue.sample_count; i++) {
        fmod_error_check(FMOD_Sound_Release(cue.samples[i].sound), "Sound_Release (cue)");
        cue.samples[i].sound = NULL;
        cue.samples[i].path[0] = '\0';
    }
    cue.sample_count = 0;
    cue_lock_acquire();
    cue.pending_count = 0;
    cue.status.active_voices = 0;
    cue_lock_release();
}

static void delete_emitter(Sound_Emitter* emitter) {
    if (emitter->unique_id) {
        free(emitter->unique_id);
        emitter->unique_id = NULL;
    }
    emitter->event = NULL;
    emitter->last_update_tick = 0;
}

// 初始化链失败即 fatal（项目 Fail-Fast 规则：基础设施损坏 exit(1)）。
// 本播放器与所有后续声音都依赖这四步；不允许假 initialized=true 继续。
static void fmod_init_require(FMOD_RESULT result, const char* context) {
    if (result != FMOD_OK) {
        LOG_ERROR("[sound] FATAL: %s failed: %s (%d) — sound system unusable\n",
                  context, FMOD_ErrorString(result), (int)result);
        exit(1);
    }
}

void sound_init(void) {
    FMOD_RESULT result;

    // release 版 libfmod 不带 debug 日志，返回 UNSUPPORTED 是已知可解释现象，
    // 保持非致命检查（logging 版 libfmodL 才支持）。
    result = FMOD_Debug_Initialize(
        FMOD_DEBUG_LEVEL_WARNING,
        FMOD_DEBUG_MODE_TTY,
        NULL,
        "fmod.file"
    );
    fmod_error_check(result, "Debug_Initialize");

    result = FMOD_Studio_System_Create(&state.system, FMOD_VERSION);
    fmod_init_require(result, "Studio_System_Create");

    result = FMOD_Studio_System_Initialize(
        state.system,
        512,
        FMOD_STUDIO_INIT_NORMAL,
        FMOD_INIT_NORMAL,
        NULL
    );
    fmod_init_require(result, "Studio_System_Initialize");

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
    fmod_init_require(result, "GetCoreSystem");

    result = FMOD_System_GetMasterChannelGroup(state.core_system, &state.master_ch_group);
    fmod_init_require(result, "GetMasterChannelGroup");

    // 记录携带二进制的实际运行版本（0xaaaabbcc）；头文件宏只是编译期假设
    unsigned int fmod_runtime_version = 0;
    result = FMOD_System_GetVersion(state.core_system, &fmod_runtime_version);
    fmod_error_check(result, "System_GetVersion");
    if (result == FMOD_OK) {
        LOG_INFO("[sound] FMOD runtime version %x.%02x.%02x (header 0x%08x)\n",
                 fmod_runtime_version >> 16, (fmod_runtime_version >> 8) & 0xff,
                 fmod_runtime_version & 0xff, FMOD_VERSION);
    }

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
    cue_main_thread_tick();
}

void sound_shutdown(void) {
    if (!state.initialized) return;
    music_release_current();
    cue_release_all();
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
