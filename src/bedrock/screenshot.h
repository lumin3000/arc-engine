
#ifndef SCREENSHOT_H
#define SCREENSHOT_H

int screenshot_request(const char *path);

int screenshot_is_pending(void);

int screenshot_capture(void);

#endif
