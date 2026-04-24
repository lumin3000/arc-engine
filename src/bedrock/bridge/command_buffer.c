/*
 * command_buffer.c - Command Buffer 指令执行
 *
 * 解析并执行 JS 提交的 command buffer
 */

#include "command_buffer.h"
#include "../gfx/draw.h"
#include "../gfx/render.h"
#include "../gfx/text.h"
#include "../helpers.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// 全局统计
// ============================================================================

static Cmd_Stats g_stats;

void cmd_get_stats(Cmd_Stats *stats) {
  if (stats) {
    *stats = g_stats;
  }
}

// ============================================================================
// 指令执行器
// ============================================================================

static void exec_push_world_space(void) {
  Coord_Space world_space = get_world_space();
  push_coord_space(world_space);
}

static void exec_push_clip_space(void) {
  Coord_Space clip_space;
  xform_identity(clip_space.proj);
  xform_identity(clip_space.camera);
  push_coord_space(clip_space);
}

static void exec_push_screen_space(void) {
  Coord_Space screen_space = get_screen_space();
  push_coord_space(screen_space);
}

static void exec_draw_sprite(const Cmd_Draw_Sprite *cmd_ptr) {
  Cmd_Draw_Sprite cmd;
  memcpy(&cmd, cmd_ptr, sizeof(cmd));

  Vec2 pos = {cmd.x, cmd.y};

  Draw_Sprite_Opts opts = DRAW_SPRITE_OPTS_DEFAULT;
  opts.pivot = (Pivot)cmd.pivot;

  draw_sprite_ex(pos, (Sprite_Name)cmd.sprite, opts);
  g_stats.sprite_count++;
}

static void exec_draw_sprite_ex(const Cmd_Draw_Sprite_Ex *cmd_ptr) {
  Cmd_Draw_Sprite_Ex cmd;
  memcpy(&cmd, cmd_ptr, sizeof(cmd));

  Vec2 pos = {cmd.x, cmd.y};

  Draw_Sprite_Opts opts = DRAW_SPRITE_OPTS_DEFAULT;
  opts.pivot = (Pivot)cmd.pivot;
  opts.flip_x = cmd.flip_x != 0;
  opts.z_layer = (ZLayer)cmd.z_layer;
  opts.anim_index = cmd.anim_index;
  opts.draw_offset[0] = cmd.draw_offset_x;
  opts.draw_offset[1] = cmd.draw_offset_y;
  memcpy(opts.col, cmd.col, sizeof(Vec4));
  memcpy(opts.col_override, cmd.col_override, sizeof(Vec4));

  draw_sprite_ex(pos, (Sprite_Name)cmd.sprite, opts);
  g_stats.sprite_count++;
}

static void exec_draw_rect(const Cmd_Draw_Rect *cmd_ptr) {
  Cmd_Draw_Rect cmd;
  memcpy(&cmd, cmd_ptr, sizeof(cmd));

  Rect rect = {cmd.x1, cmd.y1, cmd.x2, cmd.y2};

  Draw_Rect_Opts opts = DRAW_RECT_OPTS_DEFAULT;
  memcpy(opts.col, cmd.col, sizeof(Vec4));

  draw_rect_ex(rect, opts);
  g_stats.rect_count++;
}

static void exec_draw_rect_ex(const Cmd_Draw_Rect_Ex *cmd_ptr) {
  Cmd_Draw_Rect_Ex cmd;
  memcpy(&cmd, cmd_ptr, sizeof(cmd));

  Rect rect = {cmd.x1, cmd.y1, cmd.x2, cmd.y2};

  Draw_Rect_Opts opts = DRAW_RECT_OPTS_DEFAULT;
  memcpy(opts.col, cmd.col, sizeof(Vec4));
  memcpy(opts.col_override, cmd.col_override, sizeof(Vec4));
  opts.z_layer = (ZLayer)cmd.z_layer;
  opts.flags = cmd.flags;

  draw_rect_ex(rect, opts);
  g_stats.rect_count++;
}

static void exec_draw_text(const Cmd_Draw_Text *cmd_ptr, const char *text) {
  Cmd_Draw_Text cmd;
  memcpy(&cmd, cmd_ptr, sizeof(cmd));

  Vec2 pos = {cmd.x, cmd.y};

  Draw_Text_Opts opts = DRAW_TEXT_OPTS_DEFAULT;
  opts.pivot = (Pivot)cmd.pivot;
  opts.z_layer = (ZLayer)cmd.z_layer;
  opts.scale = cmd.scale;
  memcpy(opts.col, cmd.col, sizeof(Vec4));

  draw_text_ex(pos, text, opts, NULL);
  g_stats.text_count++;
}

// ============================================================================
// 主执行循环
// ============================================================================

int cmd_execute(const uint8_t *buffer, size_t size) {
  if (!buffer || size < sizeof(Cmd_Header)) {
    return -1;
  }

  // 重置统计
  memset(&g_stats, 0, sizeof(g_stats));
  
  fprintf(stderr, "[CMD] cmd_execute called: buffer=%p size=%zu\n", buffer, size);

  size_t offset = 0;

#ifdef BP_DEBUG
  fprintf(stderr, "[CMD] Execute start: size=%zu\n", size);
#endif

  while (offset + sizeof(Cmd_Header) <= size) {
    // 使用 memcpy 读取 header，避免未对齐访问
    Cmd_Header header;
    memcpy(&header, buffer + offset, sizeof(Cmd_Header));

    // 验证指令大小
    if (header.size < sizeof(Cmd_Header) || offset + header.size > size) {
#ifdef BP_DEBUG
      fprintf(stderr,
              "[CMD] Invalid size: opcode=%02x size=%d offset=%zu total=%zu\n",
              header.opcode, header.size, offset, size);
#endif
      g_stats.error_count++;
      break;
    }

#ifdef BP_DEBUG
    fprintf(stderr, "[CMD] opcode=%02x size=%d offset=%zu\n", header.opcode,
            header.size, offset);
#endif

    const uint8_t *cmd_ptr = buffer + offset;

    switch (header.opcode) {
    case CMD_NOP:
      // 空操作
      break;

    case CMD_END:
      // 结束标记
      goto done;

    case CMD_PUSH_WORLD_SPACE:
      exec_push_world_space();
      break;

    case CMD_PUSH_CLIP_SPACE:
      exec_push_clip_space();
      break;

    case CMD_PUSH_SCREEN_SPACE:
      exec_push_screen_space();
      break;

    case CMD_DRAW_SPRITE:
      if (header.size >= sizeof(Cmd_Draw_Sprite)) {
        exec_draw_sprite((const Cmd_Draw_Sprite *)cmd_ptr);
      } else {
        g_stats.error_count++;
      }
      break;

    case CMD_DRAW_SPRITE_EX:
      if (header.size >= sizeof(Cmd_Draw_Sprite_Ex)) {
        exec_draw_sprite_ex((const Cmd_Draw_Sprite_Ex *)cmd_ptr);
      } else {
        g_stats.error_count++;
      }
      break;

    case CMD_DRAW_RECT:
      if (header.size >= sizeof(Cmd_Draw_Rect)) {
        exec_draw_rect((const Cmd_Draw_Rect *)cmd_ptr);
      } else {
        g_stats.error_count++;
      }
      break;

    case CMD_DRAW_RECT_EX:
      if (header.size >= sizeof(Cmd_Draw_Rect_Ex)) {
        exec_draw_rect_ex((const Cmd_Draw_Rect_Ex *)cmd_ptr);
      } else {
        g_stats.error_count++;
      }
      break;

    case CMD_DRAW_TEXT:
      if (header.size >= sizeof(Cmd_Draw_Text)) {
        // 先安全读取 Cmd_Draw_Text 部分
        Cmd_Draw_Text text_cmd;
        memcpy(&text_cmd, cmd_ptr, sizeof(Cmd_Draw_Text));

        // 文本数据紧跟在固定结构之后
        const char *text = (const char *)(cmd_ptr + sizeof(Cmd_Draw_Text));
        size_t text_offset = sizeof(Cmd_Draw_Text);

        // 验证文本长度
        if (text_offset + text_cmd.text_len <= header.size) {
          // 创建临时 null-terminated 字符串
          char text_buf[256];
          size_t copy_len = text_cmd.text_len < 255 ? text_cmd.text_len : 255;
          memcpy(text_buf, text, copy_len);
          text_buf[copy_len] = '\0';
          exec_draw_text((const Cmd_Draw_Text *)cmd_ptr, text_buf);
        } else {
          g_stats.error_count++;
        }
      } else {
        g_stats.error_count++;
      }
      break;

    default:
#ifdef BP_DEBUG
      fprintf(stderr, "[CMD] Unknown opcode: %02x\n", header.opcode);
#endif
      g_stats.error_count++;
      break;
    }

    g_stats.cmd_count++;
    offset += header.size;
  }

done:
#ifdef BP_DEBUG
  fprintf(stderr, "[CMD] Execute done: count=%d error=%d\n", g_stats.cmd_count,
          g_stats.error_count);
#endif
  return g_stats.cmd_count;
}
