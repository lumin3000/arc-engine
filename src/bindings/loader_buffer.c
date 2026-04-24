
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <sys/stat.h>
#include <direct.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "quickjs.h"

static char *g_buffer = NULL;
static size_t g_size = 0;
static size_t g_capacity = 0;

static JSValue js_loader_read(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "loader.read requires (path)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;

  FILE *f = fopen(path, "rb");
  if (!f) {
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
  }

  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (file_size + 1 > g_capacity) {
    size_t new_cap = file_size + 1;
    if (new_cap < 1024 * 1024) new_cap = 1024 * 1024;
    char *new_buf = realloc(g_buffer, new_cap);
    if (!new_buf) {
      fclose(f);
      JS_FreeCString(ctx, path);
      return JS_ThrowOutOfMemory(ctx);
    }
    g_buffer = new_buf;
    g_capacity = new_cap;
  }

  size_t read_size = fread(g_buffer, 1, file_size, f);
  fclose(f);
  JS_FreeCString(ctx, path);

  if (read_size != file_size) {
    return JS_ThrowInternalError(ctx, "read failed");
  }

  g_buffer[read_size] = '\0';
  g_size = read_size;

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "__ptr", JS_NewInt64(ctx, (int64_t)(intptr_t)g_buffer));
  JS_SetPropertyStr(ctx, obj, "size", JS_NewInt64(ctx, (int64_t)g_size));
  return obj;
}

static JSValue js_loader_get_string(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "loader.get_string requires (__ptr, size)");
  }

  int64_t ptr;
  if (JS_ToInt64(ctx, &ptr, argv[0]) < 0) return JS_EXCEPTION;

  int64_t size;
  if (JS_ToInt64(ctx, &size, argv[1]) < 0) return JS_EXCEPTION;

  if (ptr == 0 || size <= 0) {
    return JS_UNDEFINED;
  }

  const char *data = (const char *)(intptr_t)ptr;
  return JS_NewStringLen(ctx, data, (size_t)size);
}

static JSValue js_loader_free(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  if (g_buffer) {
    free(g_buffer);
    g_buffer = NULL;
    g_size = 0;
    g_capacity = 0;
  }
  return JS_UNDEFINED;
}

static JSValue js_loader_get_size(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
  return JS_NewInt64(ctx, (int64_t)g_size);
}

static JSValue js_loader_read_string(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "loader.read_string requires (path)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;

  FILE *f = fopen(path, "rb");
  if (!f) {
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
  }

  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = js_malloc(ctx, file_size + 1);
  if (!buf) {
    fclose(f);
    JS_FreeCString(ctx, path);
    return JS_ThrowOutOfMemory(ctx);
  }

  size_t read_size = fread(buf, 1, file_size, f);
  fclose(f);
  JS_FreeCString(ctx, path);

  if (read_size != file_size) {
    js_free(ctx, buf);
    return JS_ThrowInternalError(ctx, "read failed");
  }

  buf[read_size] = '\0';

  JSValue result = JS_NewStringLen(ctx, buf, read_size);
  js_free(ctx, buf);

  return result;
}

static JSValue js_loader_readdir(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "loader.readdir requires (path)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;

  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;

#if defined(_WIN32)
  char search_path[MAX_PATH];
  snprintf(search_path, sizeof(search_path), "%s\\*", path);
  WIN32_FIND_DATAA fd;
  HANDLE hFind = FindFirstFileA(search_path, &fd);
  if (hFind == INVALID_HANDLE_VALUE) {
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
  }
  do {
    if (fd.cFileName[0] == '.' &&
        (fd.cFileName[1] == '\0' ||
         (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))) {
      continue;
    }
    JS_SetPropertyUint32(ctx, arr, idx++, JS_NewString(ctx, fd.cFileName));
  } while (FindNextFileA(hFind, &fd));
  FindClose(hFind);
#else
  DIR *dir = opendir(path);
  if (!dir) {
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }
    JS_SetPropertyUint32(ctx, arr, idx++, JS_NewString(ctx, entry->d_name));
  }
  closedir(dir);
#endif

  JS_FreeCString(ctx, path);
  return arr;
}

static JSValue js_loader_isdir(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "loader.isdir requires (path)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;

  struct stat st;
  int result = stat(path, &st);
  JS_FreeCString(ctx, path);

  if (result != 0) {
    return JS_FALSE;
  }
  return S_ISDIR(st.st_mode) ? JS_TRUE : JS_FALSE;
}

static JSValue js_loader_write_string(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(ctx, "loader.write_string requires (path, content)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (!path) return JS_EXCEPTION;

  size_t content_len;
  const char *content = JS_ToCStringLen(ctx, &content_len, argv[1]);
  if (!content) {
    JS_FreeCString(ctx, path);
    return JS_EXCEPTION;
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);
    return JS_ThrowInternalError(ctx, "Cannot open file for writing: %s", path);
  }

  size_t written = fwrite(content, 1, content_len, f);
  fclose(f);

  JS_FreeCString(ctx, path);
  JS_FreeCString(ctx, content);

  if (written != content_len) {
    return JS_ThrowInternalError(ctx, "Write incomplete");
  }

  return JS_NewInt64(ctx, (int64_t)written);
}

int js_init_loader_module(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue loader = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, loader, "read",
                    JS_NewCFunction(ctx, js_loader_read, "read", 1));
  JS_SetPropertyStr(ctx, loader, "get_string",
                    JS_NewCFunction(ctx, js_loader_get_string, "get_string", 2));
  JS_SetPropertyStr(ctx, loader, "free",
                    JS_NewCFunction(ctx, js_loader_free, "free", 0));
  JS_SetPropertyStr(ctx, loader, "get_size",
                    JS_NewCFunction(ctx, js_loader_get_size, "get_size", 0));
  JS_SetPropertyStr(ctx, loader, "read_string",
                    JS_NewCFunction(ctx, js_loader_read_string, "read_string", 1));
  JS_SetPropertyStr(ctx, loader, "readdir",
                    JS_NewCFunction(ctx, js_loader_readdir, "readdir", 1));
  JS_SetPropertyStr(ctx, loader, "isdir",
                    JS_NewCFunction(ctx, js_loader_isdir, "isdir", 1));
  JS_SetPropertyStr(ctx, loader, "write_string",
                    JS_NewCFunction(ctx, js_loader_write_string, "write_string", 2));

  JS_SetPropertyStr(ctx, global, "loader", loader);
  JS_FreeValue(ctx, global);

  return 0;
}
