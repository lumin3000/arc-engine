#ifndef BALD_SOUND_H
#define BALD_SOUND_H

#include "../../types.h"

void sound_init(void);

void sound_update(Vec2 listener_pos, float volume);

typedef struct FMOD_STUDIO_EVENTINSTANCE FMOD_STUDIO_EVENTINSTANCE;

FMOD_STUDIO_EVENTINSTANCE* sound_play(const char* event_name, Vec2 pos, float cooldown_ms);

void sound_play_continuously(const char* event_name, const char* unique_id_suffix, Vec2 pos);

void sound_update_emitters(void);

// ---------------------------------------------------------------------------
// 单文件音乐流播放（同一 FMOD Core System；一次至多一路，重复 play 替换现曲）。
// 线程契约：request/get_status 任意线程可调（内部自旋锁保护的命令信箱与状态
// 快照，不做任何 FMOD 调用）；FMOD 调用全部由主线程在 sound_update 内消费执行。

typedef enum {
    SOUND_MUSIC_IDLE = 0,     // 从未播放
    SOUND_MUSIC_PLAYING = 1,  // 声道在播
    SOUND_MUSIC_ENDED = 2,    // 自然播完（EOS）
    SOUND_MUSIC_STOPPED = 3,  // 被 stop 请求停止
    SOUND_MUSIC_FAILED = 4,   // 打开/播放失败，见 error_code/error_context
} Sound_Music_State;

#define SOUND_MUSIC_PATH_MAX 1024
#define SOUND_MUSIC_ERRCTX_MAX 64

typedef struct {
    int state;                // Sound_Music_State
    unsigned int position_ms; // 当前播放位置（仅 PLAYING 有意义）
    unsigned int length_ms;   // 当前/最后一首长度（打开成功后有效）
    int error_code;           // FMOD_RESULT 数值（FAILED 时非 0）
    char error_context[SOUND_MUSIC_ERRCTX_MAX]; // 失败发生的调用名
    unsigned int play_serial; // 每次 play 请求被主线程受理后 +1
} Sound_Music_Status;

// 请求播放一个音频文件（绝对或工作目录相对路径）。volume ∈ [0,1]。
// 覆盖语义：受理时先停止并释放现曲，同一时刻最多一路。
// 返回 false = 请求级参数无效（空/超长路径、非有限或越界音量）被拒绝，
// 此时不入队、不修改全局状态（现曲继续播）；调用方应把 false 当错误上报。
bool sound_music_request_play(const char* path, float volume);

// 请求停止当前音乐；幂等，无曲时无效果。
void sound_music_request_stop(void);

Sound_Music_Status sound_music_get_status(void);

// ---------------------------------------------------------------------------
// 单次 2D 短声效（cue）。与音乐同一 FMOD Core System、同一"任意线程请求/
// 主线程 sound_update 消费"模型；命令信箱与状态完全独立于音乐，互不覆盖。
// 短样本按路径常驻缓存复用（CREATESAMPLE 全解码），实例播完自动回收。
// 只有一次性 2D 播放：无循环、无 3D 位置、无独立总线。

#define SOUND_CUE_QUEUE_CAP 8        // 单帧待消费命令上限（超出同步拒绝）
#define SOUND_CUE_MAX_VOICES 8       // 并发活实例上限（超出消费期丢弃）
#define SOUND_CUE_SAMPLE_CACHE_CAP 8 // 常驻样本缓存上限（按路径去重）

typedef enum {
    SOUND_CUE_ACCEPTED = 0,          // 已入队，主线程本帧消费
    SOUND_CUE_REJECT_INVALID = 1,    // 参数无效（空/超长路径、非有限或越界音量）
    SOUND_CUE_REJECT_QUEUE_FULL = 2, // 命令队列满
} Sound_Cue_Request_Result;

typedef struct {
    int active_voices;                        // 当前在播实例数
    unsigned int accepted_total;              // 入队成功次数
    unsigned int started_total;               // 主线程起播成功次数
    unsigned int ended_total;                 // 自然播完回收次数
    unsigned int rejected_queue_full_total;   // 请求期队列满拒绝次数
    unsigned int dropped_voice_limit_total;   // 消费期并发满丢弃次数
    unsigned int failed_total;                // FMOD/缓存失败次数
    int last_error_code;                      // 最近失败 FMOD_RESULT（缓存满=-1）
    char last_error_context[SOUND_MUSIC_ERRCTX_MAX]; // 最近失败调用名
} Sound_Cue_Status;

// 请求播放一次短声效（绝对或工作目录相对路径）。volume ∈ [0,1]。
// 拒绝（INVALID/QUEUE_FULL）同步返回、不入队、不触碰任何播放中状态；
// 文件不存在等 IO/FMOD 错误在主线程消费时暴露于 status（异步）。
Sound_Cue_Request_Result sound_cue_request_play(const char* path, float volume);

Sound_Cue_Status sound_cue_get_status(void);

// 主线程退出收尾：停音乐、释放 Studio/Core。仅 engine cleanup 调用。
void sound_shutdown(void);

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
