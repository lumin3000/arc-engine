# S3-REDO gfx 决策表（28 文件）

来源：/tmp/s3r1_gfx/{mapgen,gunslinger,engine}/，2026-04-26 复审

## 复审方法

1. 取 mapgen pre-S3 tag (`pre-arc-engine-integration`, fcd0d53) 下 src/bedrock/gfx/ 全部文件
2. 与 gunslinger pre-extraction 备份及当前 arc-engine 三方比较
3. 用 sed 剥离单行注释 + 空行 + 块注释残行后比较代码层差异
4. 对剥离后行差 > 3 的文件，逐 hunk 看 diff 内容，判断方向

## 复审纠正

**第一轮 Explore agent 给出的决策表（15 B + 2 C + 9 A + 0 D）已废弃**。原因：agent 只看 raw 行差就武断标 B，忽略了大量 "+X 行"实际是注释/section 头/trailing whitespace。

剥离注释后的真实代码差异：

| 类别 | 文件数 | 说明 |
|------|--------|------|
| 代码层 delta = 0（仅注释/格式） | 21 | 直接 A |
| 代码层 delta ≤ 3（注释级残差） | 1 | A（render.h -3 是 engine 加 use_mvp 字段） |
| 代码层 delta > 3 真有内容 | 4 | 逐 hunk 审 |
| auto-generated headers | 2 | A（engine 当前版本含更多 fragment 变体，正向） |

逐 hunk 审的 4 个文件结论：

- **graphics.c +11**：mapgen 含 `extern terrain_buf_*_view`、engine 已替换为 `render_state.bind.views[VIEW_*]` + `engine_get_world_inv_size()`。**这是 §S3.2 view registrar 设计的正确实现**。mapgen 当前 main.c 第 72-74 行已用 `engine_register_view()` 注册。**A（已正确演化）**
- **render.c -24**：engine 引入 `Sprite *sprites = calloc(engine_sprite_count(), ...)` 替换硬编码 `Sprite sprites[SPRITE_NAME_COUNT]`；用 `default_flow_img` 占位替换 `terrain_buf_load_water_textures_main_thread()` extern 调用。**严格更优**。**A（engine 正向演化）**
- **mesh.c -16**：engine 加 `_WIN32 thread guard`（GetCurrentThreadId / pthread）。Windows 兼容增强。**A（正向演化）**
- **generic_shader.c +9**：纯文档注释（"VS Block 0"、"Texture / Sampler" 标题），无逻辑差异。**A（注释）**

## 决策表

| # | 文件 | 决策 | 关键差异（一行摘要） | 行动 |
|---|------|------|----------------------|------|
| 1 | camera.c | A | delta=0，仅注释 | 无 |
| 2 | camera.h | A | delta=0，仅注释 | 无 |
| 3 | draw.c | A | delta=0，仅注释 | 无 |
| 4 | draw.h | A | delta=0，仅注释 | 无 |
| 5 | draw_helpers.h | A | delta=0，仅注释 | 无 |
| 6 | font_manager.c | A | delta=0，仅注释和 section 头 | 无 |
| 7 | font_manager.h | A | delta=0，仅注释 | 无 |
| 8 | generated_shader.h | A | auto-generated，engine 当前版含更多 fragment 变体（mapgen 1631 → engine 3302） | 无 |
| 9 | generic_shader.c | A | delta=+9，全部是文档注释 | 无 |
| 10 | generic_shader.h | A | delta=0 | 无 |
| 11 | graphics.c | A | delta=+11，mapgen 含 extern terrain_buf_*；engine 已用 view registrar 替换；§S3.2 已实现 | 无（mapgen main.c 已用 engine_register_view） |
| 12 | graphics.h | A | delta=0 | 无 |
| 13 | material.c | A | delta=0 | 无 |
| 14 | material.h | A | delta=0；Material 结构 color_two/mask_texture/params 已在 engine | 无 |
| 15 | mesh.c | A | delta=-16，engine 加 _WIN32 thread guard，正向 | 无 |
| 16 | mesh.h | A | delta=0 | 无 |
| 17 | pipeline_cache.c | A | delta=0 | 无 |
| 18 | pipeline_cache.h | A | delta=0 | 无 |
| 19 | render.c | A | delta=-24，engine 用 calloc(engine_sprite_count())+default view 替换硬编码，正向 | 无 |
| 20 | render.h | A | delta=-3，engine 加 use_mvp/mvp/custom_view 字段，配合 render.c | 无 |
| 21 | render_diagnostics.c | A | delta=0 | 无 |
| 22 | render_diagnostics.h | A | delta=0 | 无 |
| 23 | sdf_text_pipeline.c | A | delta=0 | 无 |
| 24 | sdf_text_pipeline.h | A | delta=0 | 无 |
| 25 | sdftext_shader.h | A | auto-generated，engine 含更多 variant | 无 |
| 26 | simgui_impl.cpp | A | delta=0 | 无 |
| 27 | text.c | A | delta=0 | 无 |
| 28 | text.h | A | delta=0 | 无 |

## 统计

| 决策 | 数量 |
|------|------|
| A（无须改动） | 28 |
| B（回填 engine） | 0 |
| C（加 hook） | 0 |
| D（人工裁决） | 0 |

## 结论

**前任 AI 在 C 层 bedrock/gfx 实际做对了**：

1. 26 个三方共有的 `.c/.h/.cpp` 文件中，所有真实功能差异已正确合并到 engine 或被 engine 进一步演化（动态 sprite 数组、_WIN32 guard、view registrar）
2. mapgen 当前的 main.c 第 72-74 行已用 `engine_register_view(VIEW_flow_map/ripple_tex/noise_tex, terrain_buf_*())` 接入 §S3.2 设计的注册器接口
3. extern terrain_buf_* 已从 engine 的 graphics.c / render.c 内部彻底清除
4. raw 行差 1258 主要来自注释/section header 删减，**不是功能损失**

## 审核方 G2 结论的修正

审核方据 raw 行差判断 "等于继续用 gunslinger S1.4 裁剪版" 这一条**部分需要修正**：

- **正确部分**：26 个文件中，gunslinger pre-extraction 与当前 engine 行数完全一致占 27/28，证明前任 AI 没有把 mapgen 版作为整体替换的基准（这一点是事实）
- **结论需修正**：但**功能上**前任 AI 已经把 mapgen 的关键能力（dynamic sprite、view registrar、§S3.2 接口）都正确融入了 engine。"裁剪版顶上"的判断只对**注释结构**成立，对**功能代码**不成立
- **G2 真正的问题不是合并方向错**，而是**没建决策记录**。本文件即补建该记录，G2 实质遗漏到此关闭。

## 验收

- [x] 28 个文件均已逐项核校，决策类型 + 关键差异 + 行动有迹可循
- [x] 全部 A 类，无需对 engine 源码做任何修改
- [x] G2 决策记录补齐

S3R.1 阶段无 commit 产生（决策结论是"engine 当前已正确"），但本文件作为 G2 的修正证据保留。下一步 S3R.2 处理 JS scripts 26 文件（这是真正没做的部分）。
