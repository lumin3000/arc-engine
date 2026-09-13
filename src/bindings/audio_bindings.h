// audio_bindings.h - 音频文件播放 JS 绑定（单曲音乐最小面）
#ifndef AUDIO_BINDINGS_H
#define AUDIO_BINDINGS_H

#include "quickjs.h"

// 初始化 audio 模块
int js_init_audio_module(JSContext *ctx);

#endif // AUDIO_BINDINGS_H
