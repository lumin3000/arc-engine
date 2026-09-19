// sound_miniaudio.c - sound.h 的 miniaudio 后端。
//
// 消费者源列表选本文件 + miniaudio_impl.c 即得到音频底座：
// 原生经 miniaudio 的平台设备后端输出，浏览器经其 Web Audio 后端输出
// （默认 ScriptProcessor 路径；AudioWorklet 路径要求全程序 Asyncify，本引擎
// 的 pthread WASM 构建不启用）。浏览器须在用户手势后才真正出声，miniaudio
// 自带手势解锁；解锁前 state=playing 但 position 不推进，如实反映。
//
// 线程契约见 sound.h：request/get_status 任意线程，
// 只碰自旋锁保护的信箱/快照；全部 miniaudio 调用由主线程在 sound_update 内
// 执行。error_code 为 ma_result（负值）；cue 样本缓存满记 SOUND_MA_CACHE_FULL。

#include "sound.h"
#include "miniaudio_config.h"
#include "../../../external/miniaudio/miniaudio.h"
#include "../../log.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SOUND_MA_CACHE_FULL 1  // 非 ma_result（ma_result 全部 <= 0）

static struct {
    bool initialized;
    ma_engine engine;
    float applied_master_volume;
#if defined(__EMSCRIPTEN__)
    // pthread WASM 的线程池由运行库占满，资源管理器不另起作业线程：
    // 作业在引擎回调与 sound_update 内（同为浏览器主线程）处理。
    ma_resource_manager resource_manager;
#endif
} state = {0};

// 输出峰值表：音频线程在每次混音后记下送往设备的最大采样幅度（×1e6 存整数），
// 主线程在音乐停止/自然结束时读出并清零写进日志——证明送进设备的不是静音。
static _Atomic uint32_t output_peak_micro = 0;

static void engine_on_process(void* user, float* frames, ma_uint64 frame_count) {
    ma_uint64 n = frame_count * ma_engine_get_channels((ma_engine*)user);
    float peak = 0.0f;
    for (ma_uint64 i = 0; i < n; i++) {
        float a = fabsf(frames[i]);
        if (a > peak) peak = a;
    }
    uint32_t micro = (uint32_t)(fminf(peak, 4.0f) * 1000000.0f);
    if (micro > atomic_load(&output_peak_micro)) atomic_store(&output_peak_micro, micro);
}

static float output_peak_take(void) {
    return (float)atomic_exchange(&output_peak_micro, 0) / 1000000.0f;
}

// ---------------------------------------------------------------------------
// 单文件音乐流

static atomic_flag music_lock = ATOMIC_FLAG_INIT;

static struct {
    // 命令信箱（任意线程写，主线程消费；后写覆盖先写——单曲语义）
    int pending_cmd;  // 0=none 1=play 2=stop
    char pending_path[SOUND_MUSIC_PATH_MAX];
    float pending_volume;
    // 状态快照（主线程写，任意线程读）
    Sound_Music_Status status;
    // 主线程私有
    ma_sound sound;
    bool loaded;
} music = {0};

static void music_lock_acquire(void) {
    while (atomic_flag_test_and_set_explicit(&music_lock, memory_order_acquire)) {}
}
static void music_lock_release(void) {
    atomic_flag_clear_explicit(&music_lock, memory_order_release);
}

bool sound_music_request_play(const char* path, float volume) {
    // 请求级参数错误：拒绝本次请求即可，不触碰 status/pending——现曲继续播
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

// 主线程专用：停并释放现曲。不改 status（调用者决定终态）。
static void music_release_current(void) {
    if (!music.loaded) return;
    ma_sound_uninit(&music.sound);  // uninit 内部先停再摘节点
    music.loaded = false;
}

static void music_set_failed(ma_result code, const char* context) {
    LOG_ERROR("[sound] music failed at %s: %s (%d)\n", context,
              ma_result_description(code), (int)code);
    music_lock_acquire();
    music.status.state = SOUND_MUSIC_FAILED;
    music.status.error_code = (int)code;
    snprintf(music.status.error_context, SOUND_MUSIC_ERRCTX_MAX, "%s", context);
    music.status.position_ms = 0;
    music_lock_release();
}

// 流式数据源走 miniaudio 的 push 模式 Vorbis，取不到总长（恒 0）；长度改用
// 一次性文件解码器（pull 模式）读取后立即关闭，对各编码走同一条路径。
static ma_result music_query_length_ms(const char* path, unsigned int* out_ms) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder dec;
    ma_result r = ma_decoder_init_file(path, &cfg, &dec);
    if (r != MA_SUCCESS) return r;
    ma_uint64 frames = 0;
    r = ma_decoder_get_length_in_pcm_frames(&dec, &frames);
    ma_uint32 rate = dec.outputSampleRate;
    ma_decoder_uninit(&dec);
    if (r != MA_SUCCESS) return r;
    if (rate == 0) return MA_INVALID_DATA;
    *out_ms = (unsigned int)((frames * 1000u) / rate);
    return MA_SUCCESS;
}

// 主线程每帧调用（sound_update 内）：先消费命令，再刷新播放状态。
static void music_main_thread_tick(void) {
    music_lock_acquire();
    int cmd = music.pending_cmd;
    char path[SOUND_MUSIC_PATH_MAX];
    float volume = music.pending_volume;
    if (cmd == 1) memcpy(path, music.pending_path, SOUND_MUSIC_PATH_MAX);
    music.pending_cmd = 0;
    music_lock_release();

    if (cmd == 2) {
        bool had = music.loaded;
        music_release_current();
        if (had) {
            music_lock_acquire();
            music.status.state = SOUND_MUSIC_STOPPED;
            music.status.position_ms = 0;
            music_lock_release();
            LOG_INFO("[sound] music stopped (output peak %.4f)\n", output_peak_take());
        }
    } else if (cmd == 1) {
        music_release_current();  // 单曲：新播替换现曲，不叠播
        ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION |
                          MA_SOUND_FLAG_NO_PITCH;
        ma_result r = ma_sound_init_from_file(&state.engine, path, flags, NULL, NULL,
                                              &music.sound);
        if (r != MA_SUCCESS) {
            music_set_failed(r, "ma_sound_init_from_file");
            return;
        }
        unsigned int length_ms = 0;
        r = music_query_length_ms(path, &length_ms);
        if (r != MA_SUCCESS) {
            ma_sound_uninit(&music.sound);
            music_set_failed(r, "music_query_length");
            return;
        }
        // 未起播先设音量，避免初始一瞬满音量
        ma_sound_set_volume(&music.sound, volume);
        r = ma_sound_start(&music.sound);
        if (r != MA_SUCCESS) {
            ma_sound_uninit(&music.sound);
            music_set_failed(r, "ma_sound_start");
            return;
        }
        music.loaded = true;
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

    // 刷新在播状态：at_end = 数据源读到流尾（自然 EOS）。所有离开 PLAYING 的
    // 分支都释放资源。
    if (music.loaded) {
        if (ma_sound_at_end(&music.sound)) {
            music_release_current();
            music_lock_acquire();
            music.status.state = SOUND_MUSIC_ENDED;
            music.status.position_ms = 0;
            music_lock_release();
            LOG_INFO("[sound] music ended (EOS, output peak %.4f)\n", output_peak_take());
        } else {
            float seconds = 0.0f;
            ma_result r = ma_sound_get_cursor_in_seconds(&music.sound, &seconds);
            if (r == MA_SUCCESS) {
                music_lock_acquire();
                music.status.position_ms = (unsigned int)(seconds * 1000.0f);
                music_lock_release();
            } else {
                // 位置查询失败不是可继续状态，按错误收束（不吞）
                music_release_current();
                music_set_failed(r, "ma_sound_get_cursor_in_seconds");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 单次 2D 短声效（cue）。样本以 DECODE 方式整段解码常驻（按路径去重），每次
// 播放从常驻样本 init_copy 出一个声位实例；ma_sound 挂在节点图上不可搬移，
// 所以声位是固定槽 + active 标记，不做数组压缩。

static atomic_flag cue_lock = ATOMIC_FLAG_INIT;

typedef struct {
    char path[SOUND_MUSIC_PATH_MAX];
    float volume;
} Sound_Cue_Cmd;

typedef struct {
    char path[SOUND_MUSIC_PATH_MAX];
    ma_sound sound;  // 只作解码数据的常驻持有者，自身从不起播
} Sound_Cue_Sample;

typedef struct {
    ma_sound sound;
    bool active;
} Sound_Cue_Voice;

static struct {
    // 命令环（任意线程写，主线程整批消费；容量 = 单帧上限）
    Sound_Cue_Cmd pending[SOUND_CUE_QUEUE_CAP];
    int pending_count;
    // 状态快照（主线程写，任意线程读；rejected_queue_full 在请求线程累加）
    Sound_Cue_Status status;
    // 主线程私有
    Sound_Cue_Sample samples[SOUND_CUE_SAMPLE_CACHE_CAP];
    int sample_count;
    Sound_Cue_Voice voices[SOUND_CUE_MAX_VOICES];
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

static void cue_set_failed(int code, const char* context) {
    LOG_ERROR("[sound] cue failed at %s: %s (%d)\n", context,
              code == SOUND_MA_CACHE_FULL ? "sample cache full"
                                          : ma_result_description((ma_result)code),
              code);
    cue_lock_acquire();
    cue.status.failed_total++;
    cue.status.last_error_code = code;
    snprintf(cue.status.last_error_context, SOUND_MUSIC_ERRCTX_MAX, "%s", context);
    cue_lock_release();
}

// 主线程专用：查缓存，未命中则整段解码并常驻。失败返回 NULL（已记 status）。
static ma_sound* cue_sample_acquire(const char* path) {
    for (int i = 0; i < cue.sample_count; i++) {
        if (strcmp(cue.samples[i].path, path) == 0) return &cue.samples[i].sound;
    }
    if (cue.sample_count >= SOUND_CUE_SAMPLE_CACHE_CAP) {
        cue_set_failed(SOUND_MA_CACHE_FULL, "Cue_SampleCacheFull");
        return NULL;
    }
    Sound_Cue_Sample* slot = &cue.samples[cue.sample_count];
    ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION |
                      MA_SOUND_FLAG_NO_PITCH;
    ma_result r = ma_sound_init_from_file(&state.engine, path, flags, NULL, NULL,
                                          &slot->sound);
    if (r != MA_SUCCESS) {
        cue_set_failed((int)r, "Cue_InitFromFile");
        return NULL;
    }
    strncpy(slot->path, path, SOUND_MUSIC_PATH_MAX);
    cue.sample_count++;
    LOG_INFO("[sound] cue sample cached: %s\n", path);
    return &slot->sound;
}

// 主线程每帧调用：先回收已播完声位，再消费命令。顺序不可倒——否则本帧已
// 结束的声位会被容量判断误当仍占用，声位满时丢掉本该能播的新请求。
static void cue_main_thread_tick(void) {
    for (int i = 0; i < SOUND_CUE_MAX_VOICES; i++) {
        Sound_Cue_Voice* v = &cue.voices[i];
        if (!v->active || !ma_sound_at_end(&v->sound)) continue;
        ma_sound_uninit(&v->sound);
        v->active = false;
        cue.voice_count--;
        cue_lock_acquire();
        cue.status.ended_total++;
        cue.status.active_voices = cue.voice_count;
        cue_lock_release();
        LOG_INFO("[sound] cue ended (voices %d)\n", cue.voice_count);
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
        ma_sound* sample = cue_sample_acquire(batch[i].path);
        if (!sample) continue;  // 失败已入 status
        Sound_Cue_Voice* v = NULL;
        for (int k = 0; k < SOUND_CUE_MAX_VOICES; k++) {
            if (!cue.voices[k].active) { v = &cue.voices[k]; break; }
        }
        // voice_count < MAX 已保证必有空槽
        ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
        ma_result r = ma_sound_init_copy(&state.engine, sample, flags, NULL, &v->sound);
        if (r != MA_SUCCESS) {
            cue_set_failed((int)r, "Cue_InitCopy");
            continue;
        }
        ma_sound_set_volume(&v->sound, batch[i].volume);  // 起播前设音量
        r = ma_sound_start(&v->sound);
        if (r != MA_SUCCESS) {
            ma_sound_uninit(&v->sound);
            cue_set_failed((int)r, "Cue_Start");
            continue;
        }
        v->active = true;
        cue.voice_count++;
        cue_lock_acquire();
        cue.status.started_total++;
        cue.status.active_voices = cue.voice_count;
        cue_lock_release();
        LOG_INFO("[sound] cue started: %s (voices %d)\n", batch[i].path, cue.voice_count);
    }
}

// 主线程退出收尾：先停声位（其数据引用常驻样本），再释放样本。
static void cue_release_all(void) {
    for (int i = 0; i < SOUND_CUE_MAX_VOICES; i++) {
        if (!cue.voices[i].active) continue;
        ma_sound_uninit(&cue.voices[i].sound);
        cue.voices[i].active = false;
    }
    cue.voice_count = 0;
    for (int i = 0; i < cue.sample_count; i++) {
        ma_sound_uninit(&cue.samples[i].sound);
        cue.samples[i].path[0] = '\0';
    }
    cue.sample_count = 0;
    cue_lock_acquire();
    cue.pending_count = 0;
    cue.status.active_voices = 0;
    cue_lock_release();
}

// ---------------------------------------------------------------------------
// 生命周期。初始化失败即 fatal（Fail-Fast：基础设施损坏 exit(1)），不允许
// 假 initialized=true 继续。

static void ma_init_require(ma_result result, const char* context) {
    if (result != MA_SUCCESS) {
        LOG_ERROR("[sound] FATAL: %s failed: %s (%d) — sound system unusable\n",
                  context, ma_result_description(result), (int)result);
        exit(1);
    }
}

void sound_init(void) {
    ma_engine_config cfg = ma_engine_config_init();
#if defined(__EMSCRIPTEN__)
    ma_resource_manager_config rm_cfg = ma_resource_manager_config_init();
    rm_cfg.decodedFormat = ma_format_f32;
    rm_cfg.decodedChannels = 0;
    rm_cfg.decodedSampleRate = 0;  // 设备采样率此时未知：保留源采样率，由声音节点重采样
    rm_cfg.jobThreadCount = 0;
    rm_cfg.flags |= MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
    ma_init_require(ma_resource_manager_init(&rm_cfg, &state.resource_manager),
                    "ma_resource_manager_init");
    cfg.pResourceManager = &state.resource_manager;
#endif
    cfg.onProcess = engine_on_process;
    cfg.pProcessUserData = &state.engine;
    ma_init_require(ma_engine_init(&cfg, &state.engine), "ma_engine_init");

    ma_device* dev = ma_engine_get_device(&state.engine);
    LOG_INFO("[sound] miniaudio %s backend=%s device=\"%s\" %u Hz %u ch\n",
             ma_version_string(),
             dev ? ma_get_backend_name(dev->pContext->backend) : "none",
             dev ? dev->playback.name : "",
             ma_engine_get_sample_rate(&state.engine),
             ma_engine_get_channels(&state.engine));

    state.applied_master_volume = -1.0f;
    state.initialized = true;
}

void sound_update(Vec2 listener_pos, float master_volume) {
    (void)listener_pos;  // 本后端只有 2D 声音
    if (!state.initialized) {
        LOG_ERROR("[sound] sound_update called but sound not initialized\n");
        return;
    }

    float vol = fminf(fmaxf(master_volume, 0.0f), 1.0f);
    if (vol != state.applied_master_volume) {
        ma_result r = ma_engine_set_volume(&state.engine, vol);
        if (r != MA_SUCCESS) {
            LOG_ERROR("[sound] ma_engine_set_volume failed: %s (%d)\n",
                      ma_result_description(r), (int)r);
        } else {
            state.applied_master_volume = vol;
        }
    }

#if defined(__EMSCRIPTEN__)
    // 无作业线程：流式换页等作业在此补充处理（与音频回调同在主线程，无并发）。
    for (int i = 0; i < 8; i++) {
        if (ma_resource_manager_process_next_job(&state.resource_manager) != MA_SUCCESS) break;
    }
#endif

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
    ma_engine_uninit(&state.engine);
#if defined(__EMSCRIPTEN__)
    ma_resource_manager_uninit(&state.resource_manager);
#endif
    state.initialized = false;
    LOG_INFO("[sound] shutdown complete\n");
}
