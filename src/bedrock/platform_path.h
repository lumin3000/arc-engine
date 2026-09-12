// 跨平台路径映射（W1 um560，2026-09-12）。
// 约定：仓库脚本/测试/Makefile 全用 POSIX 的 "/tmp/..." 作临时目录；Git Bash
// 把 /tmp 解析成 %TEMP%，而 C 运行库的 fopen("/tmp/x") 在 Windows 落到
// C:\tmp\，两边读写不到同一处。凡是接受来自 JS/脚本的路径再 fopen 的 C 代码
// 都必须先经 platform_path_resolve() 映射；非 Windows 平台原样返回。
#ifndef PLATFORM_PATH_H
#define PLATFORM_PATH_H

#include <stddef.h>

// 把 in 中的前缀 "/tmp" 换成本机临时目录写进 buf（cap 字节），返回 buf。
// 不需要映射时也把 in 拷进 buf 返回（调用方统一用返回值，不必区分）。
const char *platform_path_resolve(const char *in, char *buf, size_t cap);

#endif
