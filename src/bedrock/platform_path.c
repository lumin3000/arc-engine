#include "platform_path.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

static const char *win_temp_dir(void) {
  static char s_dir[MAX_PATH] = {0};
  if (!s_dir[0]) {
    DWORD n = GetTempPathA(MAX_PATH, s_dir);
    if (n == 0 || n >= MAX_PATH) {
      strcpy(s_dir, "C:\\tmp");
    }
    size_t len = strlen(s_dir);
    while (len > 0 && (s_dir[len - 1] == '\\' || s_dir[len - 1] == '/')) {
      s_dir[--len] = '\0';
    }
  }
  return s_dir;
}
#endif

const char *platform_path_resolve(const char *in, char *buf, size_t cap) {
  if (!in || !buf || cap == 0) return in;
#if defined(_WIN32)
  if (strncmp(in, "/tmp", 4) == 0 && (in[4] == '\0' || in[4] == '/')) {
    snprintf(buf, cap, "%s%s", win_temp_dir(), in + 4);
    return buf;
  }
#endif
  snprintf(buf, cap, "%s", in);
  return buf;
}
