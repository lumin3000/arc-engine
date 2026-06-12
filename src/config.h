#ifndef CONFIG_H
#define CONFIG_H

#define DEFAULT_WINDOW_WIDTH 1280

#define DEFAULT_WINDOW_HEIGHT 720

#define DEFAULT_ZOOM_LEVEL 2.0f

#define MIN_ZOOM_LEVEL 0.03f  // 大地图 B8: L3 全图档需视野 >800 格(实测视野≈37.5/zoom)

#define MAX_ZOOM_LEVEL 200.0f

#define ZOOM_IN_MULTIPLIER 1.01f

#define ZOOM_OUT_MULTIPLIER 0.99f

#define SCROLL_ZOOM_RATE 0.35f

#define ZOOM_SPEED 2.6f

#define ZOOM_TIGHTNESS 0.4f

#define CAMERA_BASE_SPEED 2000.0f

#define MIN_FRAME_TIME (1.0 / 20.0)

#define DEBUG_PRINT_INTERVAL 60

#include "log.h"

#define USE_JS_RENDER_ONLY 0

#endif
