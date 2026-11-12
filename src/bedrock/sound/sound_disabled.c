/* Explicit browser-startup profile. Audio is unavailable, never reported playing.
 * Selected instead of sound_miniaudio.c only by the audio-disabled WASM build. */
#include "sound.h"
#include "../../log.h"
#include <stdio.h>
#include <string.h>
void sound_init(void) { LOG_INFO("[sound] AUDIO DISABLED: browser startup validation profile\n"); }
void sound_update(Vec2 p, float v) { (void)p; (void)v; }
void sound_shutdown(void) {}
bool sound_music_request_play(const char* p,float v) { (void)p;(void)v;return false; }
void sound_music_request_stop(void) {}
Sound_Music_Status sound_music_get_status(void) {
 return (Sound_Music_Status){.state=SOUND_MUSIC_FAILED,.error_code=-10000,.error_context="AudioDisabledForBrowserStartup"};
}
Sound_Cue_Request_Result sound_cue_request_play(const char* p,float v) { (void)p;(void)v;return SOUND_CUE_REJECT_INVALID; }
Sound_Cue_Status sound_cue_get_status(void) {
 return (Sound_Cue_Status){.last_error_code=-10000,.last_error_context="AudioDisabledForBrowserStartup"};
}
unsigned int sound_voice_request_play(const char* p,float g,float pi,float pa,bool l,unsigned int f) { (void)p;(void)g;(void)pi;(void)pa;(void)l;(void)f;return 0; }
bool sound_voice_request_set(unsigned int i,float g,float pi,float pa) { (void)i;(void)g;(void)pi;(void)pa;return false; }
bool sound_voice_request_stop(unsigned int i,unsigned int f) { (void)i;(void)f;return false; }
void sound_voice_request_stop_all(void) {}
int sound_voice_take_results(Sound_Voice_Result* o,int c) { (void)o;(void)c;return 0; }
Sound_Voice_Stats sound_voice_get_stats(void) { Sound_Voice_Stats s; memset(&s,0,sizeof s); return s; }
