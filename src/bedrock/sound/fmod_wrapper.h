#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct FMOD_SYSTEM FMOD_SYSTEM;
typedef struct FMOD_STUDIO_SYSTEM FMOD_STUDIO_SYSTEM;
typedef struct FMOD_STUDIO_BANK FMOD_STUDIO_BANK;
typedef struct FMOD_STUDIO_EVENTDESCRIPTION FMOD_STUDIO_EVENTDESCRIPTION;
typedef struct FMOD_STUDIO_EVENTINSTANCE FMOD_STUDIO_EVENTINSTANCE;
typedef struct FMOD_CHANNELGROUP FMOD_CHANNELGROUP;

typedef enum {
    FMOD_OK = 0,
    FMOD_ERR_BADCOMMAND,
    FMOD_ERR_CHANNEL_ALLOC,
    FMOD_ERR_CHANNEL_STOLEN,
    FMOD_ERR_DMA,
    FMOD_ERR_DSP_CONNECTION,
    FMOD_ERR_FILE_BAD,
    FMOD_ERR_FILE_COULDNOTSEEK,
    FMOD_ERR_FILE_DISKEJECTED,
    FMOD_ERR_FILE_EOF,
    FMOD_ERR_FILE_ENDOFDATA,
    FMOD_ERR_FILE_NOTFOUND,
} FMOD_RESULT;

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

FMOD_RESULT FMOD_Studio_System_Create(FMOD_STUDIO_SYSTEM** system, unsigned int headerversion);
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
        default: return "FMOD_ERR_UNKNOWN";
    }
}
