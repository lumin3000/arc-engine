#ifndef BALD_SOUND_H
#define BALD_SOUND_H

#include "../../types.h"

void sound_init(void);

void sound_update(Vec2 listener_pos, float volume);

// ---------------------------------------------------------------------------
// 单文件音乐流播放（同一音频设备；一次至多一路，重复 play 替换现曲）。
// 线程契约：request/get_status 任意线程可调（内部自旋锁保护的命令信箱与状态
// 快照，不做任何后端调用）；后端调用全部由主线程在 sound_update 内消费执行。

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
    int error_code;           // 后端错误码（miniaudio 为 ma_result；FAILED 时非 0）
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
// 单次 2D 短声效（cue）。与音乐同一音频设备、同一"任意线程请求/
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
    unsigned int failed_total;                // 后端/缓存失败次数
    int last_error_code;                      // 最近失败后端错误码（缓存满见后端定义）
    char last_error_context[SOUND_MUSIC_ERRCTX_MAX]; // 最近失败调用名
} Sound_Cue_Status;

// 请求播放一次短声效（绝对或工作目录相对路径）。volume ∈ [0,1]。
// 拒绝（INVALID/QUEUE_FULL）同步返回、不入队、不触碰任何播放中状态；
// 文件不存在等 IO/后端错误在主线程消费时暴露于 status（异步）。
Sound_Cue_Request_Result sound_cue_request_play(const char* path, float volume);

Sound_Cue_Status sound_cue_get_status(void);

// ---------------------------------------------------------------------------
// 声道（voice）：带 id 的单次/循环样本实例。与音乐同一设备、同一"任意线程请求/
// 主线程 sound_update 消费"模型。增益/声像/音高由调用方算好下发（不用后端空间化）。
// id 在请求时原子分配并立即返回（0 = 同步拒绝：参数无效或信箱满）；之后的
// set/stop 按 id 寻址，id 不复用，旧 id 不会操作到新声道。
// 每个 id 恰好产生一条终态结果（ended/stopped/dropped/failed），起播成功另有 started。

#define SOUND_VOICE_MAX 48              // 原生声道硬上限
#define SOUND_VOICE_CMD_CAP 256         // 单帧待消费命令上限
#define SOUND_VOICE_RESULT_CAP 1024     // 未取走结果上限（满不覆盖，计 overflow）
#define SOUND_SAMPLE_CAP 128            // 常驻样本条目上限
#define SOUND_SAMPLE_BUDGET_BYTES (64u * 1024u * 1024u) // 零引用样本超预算即淘汰

typedef enum {
    SOUND_VOICE_STARTED = 1,
    SOUND_VOICE_ENDED = 2,    // 自然播完
    SOUND_VOICE_STOPPED = 3,  // stop/stop_all（含淡出完成）
    SOUND_VOICE_DROPPED = 4,  // 声道满、或起播前已被 stop
    SOUND_VOICE_FAILED = 5,   // 解码/后端失败，见 code/context
} Sound_Voice_Result_Kind;

typedef struct {
    unsigned int id;
    int kind;                 // Sound_Voice_Result_Kind
    int code;                 // 后端错误码（FAILED）或 0
    char context[SOUND_MUSIC_ERRCTX_MAX];
} Sound_Voice_Result;

typedef struct {
    int active_voices;
    int pending_cmds;
    int sample_count;
    unsigned int sample_bytes;
    unsigned int results_pending;
    unsigned int results_overflow;   // 结果环满未能写入的终态数（应恒 0）
    unsigned int started_total, ended_total, stopped_total, dropped_total, failed_total;
} Sound_Voice_Stats;

// gain ∈ [0,1]，pitch ∈ (0,4]，pan ∈ [-1,1]，fade_in_ms ≥ 0。返回 id 或 0（拒绝）。
unsigned int sound_voice_request_play(const char* path, float gain, float pitch, float pan,
                                      bool loop, unsigned int fade_in_ms);
// 已终态或未知 id：主线程消费时忽略（不产生新结果）。返回 false = 参数无效/信箱满。
bool sound_voice_request_set(unsigned int id, float gain, float pitch, float pan);
bool sound_voice_request_stop(unsigned int id, unsigned int fade_out_ms);
// 立即停止全部在播/待播声道（独立控制位，信箱满也生效）；每个 id 各得一条 stopped/dropped。
void sound_voice_request_stop_all(void);
// 取走至多 cap 条结果，返回条数。
int sound_voice_take_results(Sound_Voice_Result* out, int cap);
Sound_Voice_Stats sound_voice_get_stats(void);

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
