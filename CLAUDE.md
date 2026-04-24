# arc-engine - Claude Code 指南

## 引擎边界规则（最重要）

arc-engine 是**纯引擎**，不认识任何具体游戏。违反下列规则的提交必须拒绝：

### 禁止项

1. **bedrock/ 禁止依赖游戏专属 binding** — 如 `blood_bindings.h` / `terrain_buffer.h` / `thing_mesh.h` 等任何和具体游戏玩法绑定的模块。bedrock/js_runtime.c 当前作为所有通用 binding 的注册调度点，持有对通用 bindings/ 的 include（batch/draw/font/engine/graphics/input/diag/imgui/loader_buffer/unified_mesh）— 这是 S1 过渡状态，S1.5 会改造为回调注入机制；但在任何时刻，**引擎代码不得认识游戏模块**。
2. **引擎源码里禁止出现游戏符号** — 包括但不限于 player/enemy/bullet/wave/blood/gun/terrain/thing/pawn/mapgen 等。违反者搜索：
   ```bash
   grep -rE "gunslinger|GunGame|Bullet|\bEnemy\b|\bPlayer\b|Blood|WaveSpawner|terrain_|thing_mesh|section_mesh" src/ scripts/
   ```
   必须返回空。sprite 标识符由游戏通过 `engine_register_sprites()` 动态注册，引擎仅持有 `Sprite_Name_nil` 哨兵。
3. **shader 业务分支禁止硬编码** — 核心 shader 提供扩展点（`#include "user_fragment.glsl"`），游戏专属片段由游戏提供。S2 完成。
4. **禁止通过 extern 跨层调用绑定层符号** — 如 `extern int terrain_buf_load(void)`。需要的能力通过引擎提供的注册接口注入（如 `engine_register_view()`）。

### 允许项

- 新增通用绑定（batch/input/font 等）进 `bindings/`，前提是**名字不带游戏语义**
- 新增通用工具进 `bedrock/utils/`
- 扩展 `engine_register_*` 系列注册接口，让游戏侧注入定制数据

## 核心原则（继承自主项目）

### Fail-Fast 错误处理

| 级别 | API | 行为 |
|------|-----|------|
| FATAL | `fatal("msg")` | exit(1) |
| ERROR | `throw new Error()` | 帧中断 |
| WARN | `jtask.log.warn()` | 仅打印 |

本地单机游戏，错误立即暴露，禁止防御式编程掩盖错误。

### 开发规范

- 日志用 `jtask.log("[TAG] msg")`，禁止 `console.log`
- QJS 报错用 `JS_ThrowInternalError`
- 新增 `globalThis.XX` 前先 grep 查重
- 禁止破坏性 git 操作（`git checkout/reset/restore/clean -fd`）
- 禁止 `| grep` 过滤日志，必须 `2>&1 | tee /tmp/test.log` 保全量

## 构建

arc-engine 本身不直接构建可执行文件，依赖消费者的 Makefile 将引擎源码参与编译。单元测试位于 `tests/`（S2+ 引入）。

单独验证 jtask 可用：
```bash
cd external/jtask && make MODE=release
```

## 目录索引

| 目录 | 内容 |
|------|------|
| `src/bedrock/` | gfx/input/sound/utils/bridge/js_runtime，无业务逻辑 |
| `src/bindings/` | JS→C 通用绑定 |
| `src/shader/` | GLSL shader 源（提供扩展点） |
| `scripts/bedrock/` | Time/GameInit/Current/Game/Find 等引擎 JS 骨架 |
| `scripts/render/` | Matrix/Graphics/Material/Mesh/Camera JS 封装 |
| `scripts/tick/` | TickManager |
| `scripts/rt/` | jtask 服务（start/loader/render） |
| `external/` | 第三方库（submodule + 源码入仓混合） |
| `tools/` | pack_atlas.py 等工具 |
| `docs/engine_api.md` | 公开 API 列表 |
| `docs/how_to_consume.md` | 消费者接入指南 |

## 消费者

当前消费者：
- `sokol-javascript-game-gunslinger`（首批接入，S1 完成）

S3 规划接入：
- `arc-mapgen`

## 变更通过条件

提交到 arc-engine 的变更，必须通过：
1. 所有现有消费者 `make` 成功（目前：gunslinger macOS + Windows）
2. 游戏行为无回归（视觉对照 + FrameProfiler 日志对照）
3. 上述 grep 规则全部通过
