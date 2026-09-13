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
