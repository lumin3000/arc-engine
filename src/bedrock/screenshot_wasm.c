
#include <stdio.h>
#include <string.h>

static int g_screenshot_pending = 0;

int screenshot_request(const char *path) {
    (void)path;
    g_screenshot_pending = 0;
    return -1;
}

int screenshot_is_pending(void) {
    return g_screenshot_pending;
}

int screenshot_capture(void) {
    g_screenshot_pending = 0;
    return -1;
}
