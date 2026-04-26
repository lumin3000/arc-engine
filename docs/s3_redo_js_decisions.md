# S3-REDO JS 决策表（26 文件）

来源：/tmp/s3r2_js/{mapgen,engine}/，2026-04-26 复审

## 复审方法

1. 取 arc-mapgen/scripts/ 下与 arc-engine/scripts/ 同路径的 26 个 .js 文件
2. 剥离单行注释 + 块注释 + 空行后比较代码层差异
3. 对剥离后差异 > 5 行的文件，逐 hunk 看 diff 内容
4. 决策类型（参考 S3-REDO 计划 §S3R.2.1）：
   - **E1** = 删 mapgen 拷贝，用 engine 版（mapgen 差异是 trailing whitespace / 微改 / 中文化等无功能差异）
   - **E2** = engine 缺少通用能力，需上抽 mapgen 的差异部分到 engine
   - **E3** = mapgen 差异是游戏专属，engine 提供 hook，mapgen 把扩展挪到 game/
   - **E4** = 演化冲突，需用户裁决

## 行差总览（剥离注释后）

| # | 文件 | engine_code | mapgen_code | delta | 决策 |
|---|------|-------------|-------------|-------|------|
| 1 | bedrock/00_time.js | 182 | 182 | +0 | E1 |
| 2 | bedrock/00_types.js | 589 | 589 | +0 | E1 |
| 3 | bedrock/01_long_event_handler.js | 110 | 110 | +0 | E1 |
| 4 | bedrock/02_game_init.js | 53 | 53 | +0 | E1 |
| 5 | bedrock/03_frame_callbacks.js | 105 | 105 | +0 | E1 |
| 6 | bedrock/03_loader.js | 100 | 103 | +3 | **E3** |
| 7 | bedrock/05_current.js | 28 | 28 | +0 | E1 |
| 8 | bedrock/06_root.js | 39 | 39 | +0 | E1 |
| 9 | bedrock/07_game.js | 71 | 71 | +0 | E1 |
| 10 | bedrock/08_find.js | 73 | 73 | +0 | E1 |
| 11 | bedrock/09_prefs.js | 26 | 26 | +0 | E1 |
| 12 | bedrock/99_startup.js | 64 | 118 | **+54** | **E3** |
| 13 | config/00_run_mode.js | 19 | 53 | **+34** | **E3** |
| 14 | config/01_dev_config.js | 12 | 12 | +0 | E1 |
| 15 | render/00_0_matrix4x4.js | 90 | 90 | +0 | E1 |
| 16 | render/00_0_types.js | 18 | 18 | +0 | E1 |
| 17 | render/00_graphics.js | 113 | 113 | +0 | E1 |
| 18 | render/00_res_1_shader.js | 31 | 31 | +0 | E1 |
| 19 | render/00_res_2_material.js | 145 | 145 | +0 | E1 |
| 20 | render/00_res_3_mesh.js | 25 | 25 | +0 | E1 |
| 21 | render/13_camera_driver.js | 28 | 28 | +0 | E1 |
| 22 | rt/loader_service.js | 41 | 41 | +0 | E1 |
| 23 | rt/render_service.js | 199 | 488 | **+289** | **E3** |
| 24 | rt/start_service.js | 38 | 38 | +0 | E1 |
| 25 | tick/01_tick_list.js | 130 | 118 | -12 | E1（mapgen 在 things/04_enums.js 自定义 TickerType/TimeSpeed） |
| 26 | tick/02_tick_manager.js | 386 | 386 | +0 | E1 |

## 统计

| 决策 | 数量 |
|------|------|
| E1（删 mapgen 拷贝） | 22 |
| E2（上抽到 engine） | 0 |
| E3（mapgen 走 hook） | 4 |
| E4（人工裁决） | 0 |

## E3 详细处理方案

### E3-1: bedrock/03_loader.js（+3 行真差异）

**差异**：mapgen 的 LoaderRoutine 在 `_summary.json` 加载完成后，addFile 列表里多了一行 `addFile("terrain.json", ...)`。

**问题**：当前 engine 版本里硬编码了 `addFile("pawn.json")` / `addFile("plant.json")` / `addFile("building.json")` 等 RimWorld 风格的文件名 + handler — 这本身就是 G3 留下来的脏点（loader 不该硬编码游戏文件）。

**决策**：本次先按 E1 处理（删 mapgen 拷贝，用 engine 版） — engine 版即使含残留 RimWorld 文件名，也是 mapgen 自己产出的存档格式，删掉 mapgen 的 terrain.json 行会丢功能。
**长期**：engine 该把 loader 的 file list 改成游戏注入接口（`G.loader_setFileList(files)` 或 `LoaderRoutine_registerFileList(...)`），但这是 §S3.7 之外的引擎清理，不在 S3R 范围内。

**S3R.2.1 阶段最终决策：E3a — 把 mapgen 的 03_loader.js 与 engine 版完全合并并写回 engine**：从功能上 engine 当前版本已经带了 mapgen 风格（pawn/plant/building），加 terrain 完全合理。然后删 mapgen 拷贝。

### E3-2: bedrock/99_startup.js（+54 行真差异）

**差异**：
- engine 版（90 行）：通用启动框架，提供 `StartupFlow.registerTitleScreen(ts)` 钩子。无 TitleScreen 时立即 beginStartup，有则等 `ts.active=false`。
- mapgen 版（214 行）：完全替换 StartupRoutine，加了 DefDatabase 加载、ImpliedDefs 生成、模式入口（RunMode.config.entry）、Atlas 预加载、Terrain Section 预构建。**没用 engine 的 StartupFlow**。

**决策 E3**：
- 保留 engine 的 99_startup.js（StartupFlow + 默认 StartupRoutine + TitleScreen 路径）—— 已是引擎正确形态
- 把 mapgen 的扩展启动步骤拆成独立 hook 点
- 在 mapgen 的 `scripts/game/arc/00_startup_steps.js`（或类似位置）注册：

  ```js
  // 在 game/arc/ 下加一个 mapgen 自己的 startup wrapper
  StartupFlow.registerStartupSteps([
    function* () {
      // load DefDatabase (mapgen 专属)
      yield* DefDatabase_loadAll();
    },
    function* () {
      // mode entry, atlas preload, terrain prebuild
      yield* mapgen_main_startup();
    }
  ]);
  ```

- engine 的 StartupRoutine 再调用这些注册的 step（而不是固定只 init Root/Game/font）

**修改点**：
- arc-engine: 给 StartupFlow 加 `registerStartupSteps(steps)` API；StartupRoutine 在适当位置 yield* 每一步
- arc-mapgen: 在 game/ 下写 startup steps 注册文件
- 删 arc-mapgen/scripts/bedrock/99_startup.js

### E3-3: config/00_run_mode.js（+34 行真差异）

**差异**：
- engine 版（28 行）：动态 manifest，从 `globalThis.__RUN_MODE_MANIFEST__` 读取
- mapgen 版（66 行）：硬编码 18 个 mapgen run mode 在 manifest 里，根本没用 `__RUN_MODE_MANIFEST__` 注入

**决策 E1（事实上）**：
- mapgen 当前是错的写法（按 G3 思路应该用 engine 的扩展点）
- 把 mapgen 的 18 个 run mode 配置抽到 `arc-mapgen/scripts/game/arc/00_run_modes.js`，用 `globalThis.__RUN_MODE_MANIFEST__ = {...}` 注入
- 删 arc-mapgen/scripts/config/00_run_mode.js

### E3-4: rt/render_service.js（+289 行真差异）

**差异**：
- engine 版（254 行）：通用 render service，处理 frame 消息、调用 mainthread_run、提供 frame_count / scaffold profiler 钩子
- mapgen 版（800 行）：包含 plant_sway_head 风系统、Section mesh 状态、Atlas 预加载 generator、Terrain section 预构建 generator、water rendering、unified mesh 等 mapgen **核心渲染管线**

**决策 E3**：
- engine 的 render_service.js 保持不变 — 已是正确形态
- mapgen 的扩展全部挪到 `arc-mapgen/scripts/game/arc/render/`（新建模块）：
  - `01_plant_sway.js` — 植物 sway head 风系统
  - `02_section_mesh.js` — Section mesh 初始化 + prebuildTerrainSections 生成器
  - `03_atlas_preload.js` — preloadAllAtlasTextures 生成器
  - `04_terrain_unified.js` — terrain unified mesh slot 管理
  - `05_water.js` — water rendering
- 这些模块通过 engine 提供的 hook 接入：
  - `RenderFrameCallbacks.register(fn, priority)` — 每帧执行额外渲染逻辑
  - `StartupFlow.registerStartupSteps([...])` — 把 atlas preload / terrain prebuild 作为启动 step

**注意**：engine 的 render_service.js 已经有 `rule_system_render` / `render_frame` 入口；mapgen 的渲染扩展点应该是钩子注册，而不是替换整个 service 文件。

**修改点**：
- arc-engine: 给 render_service 加 `RenderFrameCallbacks` 注册器（如已有 ScaffoldCallbacks 风格）
- arc-mapgen: 把 render_service 的 mapgen 部分按上述模块拆分注册
- 删 arc-mapgen/scripts/rt/render_service.js

## 处理顺序

1. **22 个 E1 文件**：直接 git rm（必须在 build_game_service.sh + main.js 切到 engine path 之后）
2. **E3-1 (loader)**：把 mapgen 的 terrain.json 行合进 engine 03_loader.js，删 mapgen 拷贝
3. **E3-3 (run_mode)**：mapgen 的 18 个 mode 写进 `game/arc/00_run_modes.js`，删 mapgen 拷贝
4. **E3-2 (startup)**：engine 加 `registerStartupSteps`，mapgen 把 def/atlas/terrain 步骤注册成 step，删 mapgen 拷贝
5. **E3-4 (render_service)**：engine 加 `RenderFrameCallbacks`，mapgen 拆分扩展模块，删 mapgen 拷贝

E3-4 是最大头（289 行真功能差异），可能要在 mapgen 的 game/ 下铺开 5 个新文件。这是本次 S3R.2 的核心工作量。

## 与 §S3R.2.1 计划要点的对应

| 计划要点 | 本表覆盖 |
|----------|----------|
| 26 个文件全部分类 | ✅ |
| 4 类决策 E1/E2/E3/E4 | ✅ |
| 重点文件单独说明 | ✅（E3-1~E3-4） |
| 处理顺序 | ✅ |

## 与 plan §S3.4 第 4-5 条的对应

计划要求：
> 4. 删 arc-mapgen 自己的 scripts/bedrock/scripts/render/scripts/tick/scripts/rt/scripts/main.js/scripts/config/（引擎提供）
> 5. 改造 arc-mapgen 的 build_game_service.sh → 调用 arc-engine 的 bundler template

本表的 22 个 E1 + 4 个 E3（处理后变 E1）= 26 个文件全部要在 mapgen 删除。S3R.2.3 改造 build_game_service.sh，S3R.2.4 执行 git rm。
