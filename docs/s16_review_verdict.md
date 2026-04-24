# S1.6 Review 审核结果（reviewer: 另一个 AI）

审核基线：`docs/s16_review.md` 的 13-step 清单 + 文档末尾追加的 D4。

---

## 结论一句话

**13 个 step 的意图都落地了，代码质量 review 目标基本达成；但 D4 修复引入了构建回归，gunslinger 当前 `make clean && make` 失败。**

---

## 总览

| # | 目标 | 落地情况 |
|---|------|---------|
| 1 | A1 setter 移 internal header | ✅ `src/bedrock/js_runtime_internal.h` 建立，`js_runtime.h` 只剩公共面 |
| 2 | A4 engine_state.h 集中类型 | ✅ `src/bedrock/engine_state.h` 建立，`Game_State / Core_Context / Arc_Engine_State / JS_Runtime_Game_Bindings_Fn` 全在一处 |
| 3 | A5 engine_bindings 不再裸 extern | ✅ engine_bindings.c 已无 `^extern`；通过 `engine_state.h` 读共享状态 |
| 4 | A2+A3 Arc_Engine_State 单例 | ⚠️ **部分落地**。单例结构体建立、三个 js_runtime override 搬进去、dup_str helper 消除 strdup 样板；但 `window_w / window_h / g_master_volume / ctx / g_log_level` 仍以顶层 `int/float/Core_Context` 形式活着，未真正收纳（见"质量观察"） |
| 5 | A6 JSContext* 强类型 | ✅ gunslinger main.c `gunslinger_register_js_bindings(JSContext *)` 不再 cast void* |
| 6 | 方向 3 对象化 API | ✅ `arc_engine_create / arc_engine_run / arc_engine_js_context` 就位，gunslinger main.c 使用新形态 |
| 7 | B2 删无效 config.TILE_SIZE | ✅ gunslinger `3c31bb9` |
| 8 | B3 TEXTURE_PATHS 挂 GAME_CONFIG | ✅ gunslinger `d6ea5bb` + arc-engine `67a87c9`（`00_res_2_material.js` 同步） |
| 9 | B1 engine 注册 texture paths | ✅ `engine_asset.{c,h}` 新建，`engine_register_texture_search_paths()`；gunslinger `on_init` 调用 |
| 10 | C1+C2+C3 StartupFlow | ✅ `globalThis.StartupFlow.registerTitleScreen()`；99_startup.js 回到简单分支（单个 `_started` 布尔，无嵌套状态机） |
| 11 | D1 main.js.template | ✅ arc-engine 仅留 `scripts/main.js.template`，不再有可执行 main.js |
| 12 | D2 DUP_ALLOWLIST 精简 | ✅ gunslinger 清空（`export DUP_ALLOWLIST=""`） |
| 13 | D3 镜头/输入开关 | ✅ `Engine_Config.base_ortho_size / disable_default_camera_controls` 加入 |
| D4 | VIEW_* 宏去重 | ❌ **引入构建回归**（见下） |

---

## ❌ 阻塞问题：D4 fix 漏了 batch.c

**现象**：`cd gunslinger && make clean && make` 编译 `batch.c` 报

```
external/arc-engine/src/bindings/batch.c:329:45: error: use of undeclared identifier 'VIEW_tex0'
    sg_destroy_view(render_state.bind.views[VIEW_tex0]);
external/arc-engine/src/bindings/batch.c:342:29: error: use of undeclared identifier 'VIEW_tex0'
    render_state.bind.views[VIEW_tex0] = sg_make_view(
```

**根因**：commit `2061cd9`（"fix: drop duplicate VIEW_* macro definitions from types.h"）只删了 `src/types.h` 里的 5 行 `#define VIEW_*`。`batch.c` 原本靠 types.h（被多处 include）传递获取这些宏；删除后它没 include `bedrock/gfx/generated_shader.h`，找不到 `VIEW_tex0`。

`graphics.c` 和 `render.c` 显式 `#include "generated_shader.h"` 所以不受影响。D4 fix 的 commit 本身只扫了 types.h 的直接 diff，漏 grep 所有 `VIEW_*` 引用点检查其 include 情况。

**修复方案**（一行）：在 `src/bindings/batch.c` 的 include 段加入

```c
#include "bedrock/gfx/generated_shader.h"
```

应当独立 commit 作为 D4 的补丁（或 amend 到 `2061cd9`）。增量 build 看不到这个 error 是因为 batch.c 的 .o 文件已经存在；我用 `make clean` 才暴露。

这说明 **D4 fix 的验收没跑 `make clean && make`**，只跑了增量 make。

---

## ⚠️ 质量观察（不阻塞，记录供 S1.7 或 S2 考量）

### A2+A3 的单例没吞下全部 globals

`engine_state.h:54-62` 里仍暴露：

```c
extern Core_Context ctx;
extern int window_w;
extern int window_h;
extern float g_master_volume;
```

以及 `engine_main.c:34-40` 里仍是顶层定义：

```c
int window_w = DEFAULT_WINDOW_WIDTH;
int window_h = DEFAULT_WINDOW_HEIGHT;
int g_log_level = LOG_LEVEL_NORMAL;
float g_master_volume = 0.25f;
Core_Context ctx = {0};
```

这些是 review 文档 A3 明确点名要"收进 `Arc_Engine_State` 单例"的项。现在单例只收了新增字段（saved_argv/argc, last_frame_time, js 覆盖路径），老的 extern 蛛网保留。

结果：A4/A5 靠 `engine_state.h` 集中声明得到了"一处声明、编译期检查一致"的好处；但 A3 的"统一所有权"目标只是半完成。`ctx / window_w / window_h / g_master_volume` 仍然是"任何 include engine_state.h 的翻译单元都能改"的全局可变状态。

**不阻塞因为**：reviewer 理解这是刻意的折衷 —— 把 `ctx / window_w` 完全搬进 `Arc_Engine_State` 后，每次 engine_bindings.c 读它们都要走 `arc_engine_state()->ctx.gs->cam_pos[0]`，可读性会变差；而且 C API 上 `window_w` 被多处 binding 快速读取，accessor 开销虽小但存在。折衷的记录应该进 engine_state.h 的注释或 s16 review 文档，否则下一个 reviewer 还会踩这个点。

**建议**：s16_review.md 末尾加一条"S1.6 接受的设计折衷"，把这个决定写进去。

### js_runtime.c 仍 include 11 个 bindings

CLAUDE.md §禁止项 #1 明确指出这是 S1 过渡状态，目标是"改造为回调注入机制"。13 step 里没有对应 step 处理这块。js_runtime.c 第 4-13 行仍列出所有通用 binding 的 header。

这不属于 13 个 review 问题的范围（A1 只是把私有 setter 藏起来），但放在这里提醒：这是 CLAUDE.md 自己打脸的状态，S2 应处理。

### 注释里提到 gunslinger / blood_bindings

```
src/bedrock/engine_main.h:  const char *default_run_mode;    // default value for --mode; e.g. "gunslinger"
src/bedrock/engine_main.h:63:    // blood_bindings; registering later causes service workers to race
```

两处是"举例文档"，不是代码引用。CLAUDE.md §禁止项 #2 要求 grep 必须返回空；这两处虽是注释，严格讲也算违反。

**建议**：换成更通用的举例，例如 `"mygame"` / `"a game-specific module like audio or networking"`。

### D4 fix 的独立 commit 可保留或 amend

`2061cd9` 的 commit message 写得清楚，单独保留便于追溯；但因为它未通过 `make clean` 验证就 push 了，修复补丁作为新 commit 追加可能比 amend 更诚实地反映历史。

---

## 验收矩阵

| 项 | 结果 |
|---|---|
| arc-engine 13-step 全部有独立 commit | ✅ `cc52db1..1d31e7d` 加上游戏侧 7 个 |
| gunslinger submodule bump 每步跟随 | ✅ `111f189..b87be7e` |
| 引擎无游戏符号（src/ + scripts/） | ✅ 仅剩两处文档注释提到 gunslinger/blood（见质量观察） |
| bedrock 不依赖游戏专属 binding | ✅ |
| scripts 引擎无游戏符号 | ✅ |
| gunslinger macOS `make clean && make` | ❌ **D4 fix 后 batch.c 编译失败** |
| gunslinger macOS 运行时 TitleScreen + 进游戏无回归 | ⚠️ **无法验证，因为构建失败。** 若 D4 补丁加上即可验。 |
| gunslinger Windows 构建 | ⚠️ 未在本次审核范围内验证 |

---

## 建议的后续动作

1. 给 `src/bindings/batch.c` 加 `#include "bedrock/gfx/generated_shader.h"`，单独 commit 打包成 D4.1 补丁
2. gunslinger 侧重新 `make clean && make`，跑起游戏确认 TitleScreen + Start → 游戏无回归
3. 在 s16_review.md 末尾追加"S1.6 接受的设计折衷"章节，记录 A3 未完全收进单例的理由
4. engine_main.h 里"gunslinger"/"blood_bindings" 注释举例改成通用占位（零成本）

做完这 4 件事可以关闭 S1.6，进入 S1.7 文档收尾。
