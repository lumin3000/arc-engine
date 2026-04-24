
#include <stdio.h>
#include <string.h>

static int g_screenshot_pending = 0;
static char g_screenshot_path[1024] = {0};

int screenshot_request(const char *path) {
    if (!path || strlen(path) >= sizeof(g_screenshot_path)) {
        return -1;
    }
    strncpy(g_screenshot_path, path, sizeof(g_screenshot_path) - 1);
    g_screenshot_path[sizeof(g_screenshot_path) - 1] = '\0';
    g_screenshot_pending = 1;
    fprintf(stderr, "[screenshot] Screenshot requested but not implemented on Windows\n");
    return 0;
}

int screenshot_is_pending(void) {
    return g_screenshot_pending;
}

int screenshot_capture(void) {
    if (!g_screenshot_pending) {
        return 0;
    }
    g_screenshot_pending = 0;
    fprintf(stderr, "[screenshot] Capture not implemented on Windows D3D11\n");
    return -1;
}
