// miniaudio_config.h - 本引擎对 miniaudio 的统一编译配置。
//
// miniaudio_impl.c（实现单元）与 sound_miniaudio.c（使用单元）都必须先包含
// 本头再包含 miniaudio.h，保证两个编译单元看到同一组功能开关与结构布局。
//
// 只开实际交付素材用到的解码器：WAV（内建）+ Ogg Vorbis（stb_vorbis）。
// 需要其他编码时在这里放开对应开关，不要在单个 .c 里私自定义。
#ifndef ARC_MINIAUDIO_CONFIG_H
#define ARC_MINIAUDIO_CONFIG_H

#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_ENCODING
#define MA_NO_GENERATION

#endif
