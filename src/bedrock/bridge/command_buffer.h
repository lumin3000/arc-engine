/*
 * command_buffer.h - Command Buffer 系统
 *
 * 用于减少 JS→C FFI 调用次数，将多个绘制指令批量提交
 * 目标：每帧 FFI 调用从 400+ 次降到 1-2 次
 */

#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../types.h"

// ============================================================================
// Command Opcodes
// ============================================================================

typedef enum {
    // 坐标系统 (0x10-0x1F)
    CMD_PUSH_WORLD_SPACE  = 0x10,
    CMD_PUSH_CLIP_SPACE   = 0x11,
    CMD_PUSH_SCREEN_SPACE = 0x12,

    // 绘制指令 (0x20-0x2F)
    CMD_DRAW_SPRITE       = 0x20,
    CMD_DRAW_SPRITE_EX    = 0x21,
    CMD_DRAW_RECT         = 0x22,
    CMD_DRAW_RECT_EX      = 0x23,
    CMD_DRAW_TEXT         = 0x24,

    // 控制指令 (0x00-0x0F)
    CMD_NOP               = 0x00,
    CMD_END               = 0x01,
} Cmd_Opcode;

// ============================================================================
// Command Header - 所有指令的通用头部
// ============================================================================

#pragma pack(push, 1)

typedef struct {
    uint8_t opcode;     // Cmd_Opcode
    uint8_t flags;      // 保留
    uint16_t size;      // 整个指令的大小（包括头部）
} Cmd_Header;

// ============================================================================
// Payload 结构体 - 各指令的数据部分
// ============================================================================

// CMD_DRAW_SPRITE: 简单精灵绘制
// 大小: 4 (header) + 16 = 20 bytes
typedef struct {
    Cmd_Header header;
    float x;            // 4 bytes
    float y;            // 4 bytes
    int32_t sprite;     // 4 bytes - Sprite_Name
    int32_t pivot;      // 4 bytes - Pivot
} Cmd_Draw_Sprite;

// CMD_DRAW_SPRITE_EX: 完整精灵绘制（带所有选项）
// 大小: 4 (header) + 64 = 68 bytes
typedef struct {
    Cmd_Header header;
    float x;                    // 4 bytes
    float y;                    // 4 bytes
    int32_t sprite;             // 4 bytes - Sprite_Name
    int32_t pivot;              // 4 bytes - Pivot
    uint8_t flip_x;             // 1 byte
    uint8_t z_layer;            // 1 byte - ZLayer
    int16_t anim_index;         // 2 bytes
    float draw_offset_x;        // 4 bytes
    float draw_offset_y;        // 4 bytes
    float col[4];               // 16 bytes
    float col_override[4];      // 16 bytes
} Cmd_Draw_Sprite_Ex;

// CMD_DRAW_RECT: 简单矩形绘制
// 大小: 4 (header) + 32 = 36 bytes
typedef struct {
    Cmd_Header header;
    float x1, y1, x2, y2;       // 16 bytes - Rect
    float col[4];               // 16 bytes
} Cmd_Draw_Rect;

// CMD_DRAW_RECT_EX: 完整矩形绘制
// 大小: 4 (header) + 52 = 56 bytes
typedef struct {
    Cmd_Header header;
    float x1, y1, x2, y2;       // 16 bytes - Rect
    float col[4];               // 16 bytes
    float col_override[4];      // 16 bytes
    uint8_t z_layer;            // 1 byte
    uint8_t flags;              // 1 byte
    uint16_t _padding;          // 2 bytes
} Cmd_Draw_Rect_Ex;

// CMD_DRAW_TEXT: 文本绘制
// 大小: 4 (header) + 28 + text_len = 变长
typedef struct {
    Cmd_Header header;
    float x;                    // 4 bytes
    float y;                    // 4 bytes
    int32_t pivot;              // 4 bytes
    uint8_t z_layer;            // 1 byte
    uint8_t _padding[3];        // 3 bytes 对齐
    float col[4];               // 16 bytes
    float scale;                // 4 bytes
    uint16_t text_len;          // 2 bytes
    uint16_t _padding2;         // 2 bytes 对齐
    // char text[] follows
} Cmd_Draw_Text;

#pragma pack(pop)

// ============================================================================
// Command Buffer 执行
// ============================================================================

// 执行 command buffer 中的所有指令
// 返回执行的指令数量，-1 表示错误
int cmd_execute(const uint8_t *buffer, size_t size);

// 获取上次执行的统计信息
typedef struct {
    int cmd_count;          // 执行的指令数量
    int sprite_count;       // 绘制的精灵数量
    int rect_count;         // 绘制的矩形数量
    int text_count;         // 绘制的文本数量
    int error_count;        // 错误数量
} Cmd_Stats;

void cmd_get_stats(Cmd_Stats *stats);

#endif // COMMAND_BUFFER_H
