// miniaudio_impl.c - miniaudio 单头库的唯一实现编译单元。
//
// 与 sound_miniaudio.c 成对加入消费者的源列表。第三方代码的告警在本单元内
// 关闭，避免淹没引擎自身代码的 -Wall -Wextra 输出。
//
// 包含顺序按 miniaudio 手册 Vorbis 一节：先以 HEADER_ONLY 方式声明 stb_vorbis，
// 再展开 miniaudio 实现，最后才展开 stb_vorbis 实现体（其内部短宏名如 L/R/C
// 会污染后续代码，必须放在文件末尾）。

#if defined(__clang__)
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#include "miniaudio_config.h"

#define STB_VORBIS_HEADER_ONLY
#include "../../../external/miniaudio/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "../../../external/miniaudio/miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "../../../external/miniaudio/stb_vorbis.c"
