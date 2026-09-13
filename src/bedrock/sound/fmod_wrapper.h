#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct FMOD_SYSTEM FMOD_SYSTEM;
typedef struct FMOD_SOUND FMOD_SOUND;
typedef struct FMOD_CHANNEL FMOD_CHANNEL;
typedef struct FMOD_STUDIO_SYSTEM FMOD_STUDIO_SYSTEM;
typedef struct FMOD_STUDIO_BANK FMOD_STUDIO_BANK;
typedef struct FMOD_STUDIO_EVENTDESCRIPTION FMOD_STUDIO_EVENTDESCRIPTION;
typedef struct FMOD_STUDIO_EVENTINSTANCE FMOD_STUDIO_EVENTINSTANCE;
typedef struct FMOD_CHANNELGROUP FMOD_CHANNELGROUP;
typedef struct FMOD_CREATESOUNDEXINFO FMOD_CREATESOUNDEXINFO;

typedef int FMOD_BOOL;
typedef unsigned int FMOD_MODE;
typedef unsigned int FMOD_TIMEUNIT;

// 与 FMOD 2.02 官方 fmod_common.h 的 FMOD_RESULT 数值逐项对齐（对照本仓
// external/fmod/core/fmod_common.odin RESULT 枚举核实）。旧版本文件的枚举
// 在 ERR_DSP_CONNECTION 之后漏掉 7 个 DSP 值，FILE_* 系数值整体前移 7，
// 错误字符串张冠李戴——本次修正为显式赋值。
typedef enum {
    FMOD_OK = 0,
    FMOD_ERR_BADCOMMAND = 1,
    FMOD_ERR_CHANNEL_ALLOC = 2,
    FMOD_ERR_CHANNEL_STOLEN = 3,
    FMOD_ERR_DMA = 4,
    FMOD_ERR_DSP_CONNECTION = 5,
    FMOD_ERR_DSP_DONTPROCESS = 6,
    FMOD_ERR_DSP_FORMAT = 7,
    FMOD_ERR_DSP_INUSE = 8,
    FMOD_ERR_DSP_NOTFOUND = 9,
    FMOD_ERR_DSP_RESERVED = 10,
    FMOD_ERR_DSP_SILENCE = 11,
    FMOD_ERR_DSP_TYPE = 12,
    FMOD_ERR_FILE_BAD = 13,
    FMOD_ERR_FILE_COULDNOTSEEK = 14,
    FMOD_ERR_FILE_DISKEJECTED = 15,
    FMOD_ERR_FILE_EOF = 16,
    FMOD_ERR_FILE_ENDOFDATA = 17,
    FMOD_ERR_FILE_NOTFOUND = 18,
    FMOD_ERR_FORMAT = 19,
    FMOD_ERR_HEADER_MISMATCH = 20,
    FMOD_ERR_INITIALIZATION = 26,
    FMOD_ERR_INITIALIZED = 27,
    FMOD_ERR_INTERNAL = 28,
    FMOD_ERR_INVALID_FLOAT = 29,
    FMOD_ERR_INVALID_HANDLE = 30,
    FMOD_ERR_INVALID_PARAM = 31,
    FMOD_ERR_INVALID_POSITION = 32,
    FMOD_ERR_MEMORY = 38,
    FMOD_ERR_NOTREADY = 46,
    FMOD_ERR_OUTPUT_INIT = 51,
    FMOD_ERR_OUTPUT_NODRIVERS = 52,
    FMOD_ERR_TOOMANYCHANNELS = 64,
    FMOD_ERR_UNINITIALIZED = 67,
    FMOD_ERR_UNSUPPORTED = 68,
    FMOD_ERR_VERSION = 69,
} FMOD_RESULT;

// FMOD_MODE flags（对照 fmod_common.odin MODE_* 常量）
#define FMOD_MODE_DEFAULT      0x00000000u
#define FMOD_MODE_LOOP_OFF     0x00000001u
#define FMOD_MODE_2D           0x00000008u
#define FMOD_MODE_CREATESTREAM 0x00000080u
#define FMOD_MODE_ACCURATETIME 0x00004000u

// FMOD_TIMEUNIT（对照 fmod_common.odin TIMEUNIT_*）
#define FMOD_TIMEUNIT_MS  0x00000001u
#define FMOD_TIMEUNIT_PCM 0x00000002u

typedef enum {
    FMOD_STUDIO_INIT_NORMAL = 0x00000000,
    FMOD_STUDIO_INIT_LIVEUPDATE = 0x00000001,
} FMOD_STUDIO_INITFLAGS;

typedef enum {
    FMOD_INIT_NORMAL = 0x00000000,
} FMOD_INITFLAGS;

typedef enum {
    FMOD_STUDIO_LOAD_BANK_NORMAL = 0x00000000,
} FMOD_STUDIO_LOAD_BANK_FLAGS;

typedef enum {
    FMOD_STUDIO_EVENT_PROPERTY_COOLDOWN = 7,
} FMOD_STUDIO_EVENT_PROPERTY;

typedef enum {
    FMOD_STUDIO_STOP_ALLOWFADEOUT = 0,
    FMOD_STUDIO_STOP_IMMEDIATE = 1,
} FMOD_STUDIO_STOP_MODE;

typedef struct {
    struct { float x, y, z; } position;
    struct { float x, y, z; } velocity;
    struct { float x, y, z; } forward;
    struct { float x, y, z; } up;
} FMOD_3D_ATTRIBUTES;

typedef enum {
    FMOD_DEBUG_LEVEL_NONE = 0x00000000,
    FMOD_DEBUG_LEVEL_ERROR = 0x00000001,
    FMOD_DEBUG_LEVEL_WARNING = 0x00000002,
    FMOD_DEBUG_LEVEL_LOG = 0x00000004,
} FMOD_DEBUG_FLAGS;

typedef enum {
    FMOD_DEBUG_MODE_TTY = 0,
    FMOD_DEBUG_MODE_FILE = 1,
    FMOD_DEBUG_MODE_CALLBACK = 2,
} FMOD_DEBUG_MODE;

#define FMOD_VERSION 0x00020215

FMOD_RESULT FMOD5_Debug_Initialize(FMOD_DEBUG_FLAGS flags, FMOD_DEBUG_MODE mode, void* callback, const char* filename);
FMOD_RESULT FMOD5_System_Create(FMOD_SYSTEM** system, unsigned int headerversion);
FMOD_RESULT FMOD5_System_GetMasterChannelGroup(FMOD_SYSTEM* system, FMOD_CHANNELGROUP** channelgroup);
FMOD_RESULT FMOD5_ChannelGroup_SetVolume(FMOD_CHANNELGROUP* channelgroup, float volume);

// Core 文件播放（签名对照 external/fmod/core/fmod_darwin.odin 与官方
// fmod.h 2.02；符号已用 nm 在 libfmod.dylib 核实存在）
FMOD_RESULT FMOD5_System_CreateSound(FMOD_SYSTEM* system, const char* name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO* exinfo, FMOD_SOUND** sound);
FMOD_RESULT FMOD5_System_PlaySound(FMOD_SYSTEM* system, FMOD_SOUND* sound, FMOD_CHANNELGROUP* channelgroup, FMOD_BOOL paused, FMOD_CHANNEL** channel);
FMOD_RESULT FMOD5_Sound_Release(FMOD_SOUND* sound);
FMOD_RESULT FMOD5_Sound_GetLength(FMOD_SOUND* sound, unsigned int* length, FMOD_TIMEUNIT lengthtype);
FMOD_RESULT FMOD5_Channel_Stop(FMOD_CHANNEL* channel);
FMOD_RESULT FMOD5_Channel_SetVolume(FMOD_CHANNEL* channel, float volume);
FMOD_RESULT FMOD5_Channel_IsPlaying(FMOD_CHANNEL* channel, FMOD_BOOL* isplaying);
FMOD_RESULT FMOD5_Channel_GetPosition(FMOD_CHANNEL* channel, unsigned int* position, FMOD_TIMEUNIT postype);

FMOD_RESULT FMOD_Studio_System_Create(FMOD_STUDIO_SYSTEM** system, unsigned int headerversion);
FMOD_RESULT FMOD_Studio_System_Release(FMOD_STUDIO_SYSTEM* system);
FMOD_RESULT FMOD_Studio_System_Initialize(FMOD_STUDIO_SYSTEM* system, int maxchannels, FMOD_STUDIO_INITFLAGS studioflags, FMOD_INITFLAGS flags, void* extradriverdata);
FMOD_RESULT FMOD_Studio_System_LoadBankFile(FMOD_STUDIO_SYSTEM* system, const char* filename, FMOD_STUDIO_LOAD_BANK_FLAGS flags, FMOD_STUDIO_BANK** bank);
FMOD_RESULT FMOD_Studio_System_GetCoreSystem(FMOD_STUDIO_SYSTEM* system, FMOD_SYSTEM** coresystem);
FMOD_RESULT FMOD_Studio_System_Update(FMOD_STUDIO_SYSTEM* system);
FMOD_RESULT FMOD_Studio_System_SetListenerAttributes(FMOD_STUDIO_SYSTEM* system, int listener, const FMOD_3D_ATTRIBUTES* attributes, const FMOD_3D_ATTRIBUTES* attenuationposition);
FMOD_RESULT FMOD_Studio_System_GetEvent(FMOD_STUDIO_SYSTEM* system, const char* pathOrID, FMOD_STUDIO_EVENTDESCRIPTION** event);

FMOD_RESULT FMOD_Studio_EventDescription_CreateInstance(FMOD_STUDIO_EVENTDESCRIPTION* eventdescription, FMOD_STUDIO_EVENTINSTANCE** instance);

FMOD_RESULT FMOD_Studio_EventInstance_Start(FMOD_STUDIO_EVENTINSTANCE* eventinstance);
FMOD_RESULT FMOD_Studio_EventInstance_Stop(FMOD_STUDIO_EVENTINSTANCE* eventinstance, FMOD_STUDIO_STOP_MODE mode);
FMOD_RESULT FMOD_Studio_EventInstance_Release(FMOD_STUDIO_EVENTINSTANCE* eventinstance);
FMOD_RESULT FMOD_Studio_EventInstance_Set3DAttributes(FMOD_STUDIO_EVENTINSTANCE* eventinstance, const FMOD_3D_ATTRIBUTES* attributes);
FMOD_RESULT FMOD_Studio_EventInstance_Get3DAttributes(FMOD_STUDIO_EVENTINSTANCE* eventinstance, FMOD_3D_ATTRIBUTES* attributes);
FMOD_RESULT FMOD_Studio_EventInstance_SetProperty(FMOD_STUDIO_EVENTINSTANCE* eventinstance, FMOD_STUDIO_EVENT_PROPERTY index, float value);

#define FMOD_Debug_Initialize FMOD5_Debug_Initialize
#define FMOD_System_Create FMOD5_System_Create
#define FMOD_System_GetMasterChannelGroup FMOD5_System_GetMasterChannelGroup
#define FMOD_ChannelGroup_SetVolume FMOD5_ChannelGroup_SetVolume
#define FMOD_System_CreateSound FMOD5_System_CreateSound
#define FMOD_System_PlaySound FMOD5_System_PlaySound
#define FMOD_Sound_Release FMOD5_Sound_Release
#define FMOD_Sound_GetLength FMOD5_Sound_GetLength
#define FMOD_Channel_Stop FMOD5_Channel_Stop
#define FMOD_Channel_SetVolume FMOD5_Channel_SetVolume
#define FMOD_Channel_IsPlaying FMOD5_Channel_IsPlaying
#define FMOD_Channel_GetPosition FMOD5_Channel_GetPosition

static inline const char* FMOD_ErrorString(FMOD_RESULT result) {
    switch (result) {
        case FMOD_OK: return "FMOD_OK";
        case FMOD_ERR_BADCOMMAND: return "FMOD_ERR_BADCOMMAND";
        case FMOD_ERR_CHANNEL_ALLOC: return "FMOD_ERR_CHANNEL_ALLOC";
        case FMOD_ERR_CHANNEL_STOLEN: return "FMOD_ERR_CHANNEL_STOLEN";
        case FMOD_ERR_DMA: return "FMOD_ERR_DMA";
        case FMOD_ERR_DSP_CONNECTION: return "FMOD_ERR_DSP_CONNECTION";
        case FMOD_ERR_FILE_BAD: return "FMOD_ERR_FILE_BAD";
        case FMOD_ERR_FILE_COULDNOTSEEK: return "FMOD_ERR_FILE_COULDNOTSEEK";
        case FMOD_ERR_FILE_DISKEJECTED: return "FMOD_ERR_FILE_DISKEJECTED";
        case FMOD_ERR_FILE_EOF: return "FMOD_ERR_FILE_EOF";
        case FMOD_ERR_FILE_ENDOFDATA: return "FMOD_ERR_FILE_ENDOFDATA";
        case FMOD_ERR_FILE_NOTFOUND: return "FMOD_ERR_FILE_NOTFOUND";
        case FMOD_ERR_FORMAT: return "FMOD_ERR_FORMAT";
        case FMOD_ERR_HEADER_MISMATCH: return "FMOD_ERR_HEADER_MISMATCH";
        case FMOD_ERR_INITIALIZATION: return "FMOD_ERR_INITIALIZATION";
        case FMOD_ERR_INITIALIZED: return "FMOD_ERR_INITIALIZED";
        case FMOD_ERR_INTERNAL: return "FMOD_ERR_INTERNAL";
        case FMOD_ERR_INVALID_FLOAT: return "FMOD_ERR_INVALID_FLOAT";
        case FMOD_ERR_INVALID_HANDLE: return "FMOD_ERR_INVALID_HANDLE";
        case FMOD_ERR_INVALID_PARAM: return "FMOD_ERR_INVALID_PARAM";
        case FMOD_ERR_INVALID_POSITION: return "FMOD_ERR_INVALID_POSITION";
        case FMOD_ERR_MEMORY: return "FMOD_ERR_MEMORY";
        case FMOD_ERR_NOTREADY: return "FMOD_ERR_NOTREADY";
        case FMOD_ERR_OUTPUT_INIT: return "FMOD_ERR_OUTPUT_INIT";
        case FMOD_ERR_OUTPUT_NODRIVERS: return "FMOD_ERR_OUTPUT_NODRIVERS";
        case FMOD_ERR_TOOMANYCHANNELS: return "FMOD_ERR_TOOMANYCHANNELS";
        case FMOD_ERR_UNINITIALIZED: return "FMOD_ERR_UNINITIALIZED";
        case FMOD_ERR_UNSUPPORTED: return "FMOD_ERR_UNSUPPORTED";
        case FMOD_ERR_VERSION: return "FMOD_ERR_VERSION";
        default: return "FMOD_ERR_UNKNOWN";
    }
}
