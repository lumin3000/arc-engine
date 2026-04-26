# arc-engine S3 changelog

S3 把 arc-engine 推进到双消费者架构（gunslinger + arc-mapgen）。本文只列 **新增** API、新机制、合并进引擎的能力；详细推导见 `plan_arc_mapgen_integration.md`。

## 引擎新增 API（C）

### 帧生命周期回调

`Engine_Config` 新增三个可选回调：

| 字段 | 时机 | 用途 |
|---|---|---|
| `on_render_ready` | `render_init()` 之后 | sokol 上下文已就绪；游戏在这里加载需要 GPU 的纹理并通过 `engine_register_view()` 注入 view slot |
| `on_post_mesh_render` | 每帧 mesh queue 0..4000 提交之后、主 quad draw loop 之前 | 跳板给骨骼动画 / 自定义渲染路径；arc-mapgen 用它挂 `ozz_render_in_pass` |

旧回调（`on_init`/`on_register_js_bindings`/`on_frame`/`on_cleanup`）行为不变。

### 资产注册 API

`bedrock/engine_asset.h`：

```c
void   engine_register_view(int slot, sg_view view);
void   engine_set_world_inv_size(float value);
float  engine_get_world_inv_size(void);
```

- `engine_register_view(slot, view)` — 覆盖 `render_state.bind.views[slot]`，slot 是 `generated_shader.h` 里的 `VIEW_*` 宏。引擎在 render_init 时给所有 slot 填了 1×1 dummy；游戏侧在 `on_render_ready` 里覆盖。arc-mapgen 用它注册 flow_map / ripple_tex / noise_tex。
- `engine_set_world_inv_size(v)` — Unity `Shader.SetGlobalFloat` 风格的全局；引擎每次 mesh submit 时把它写进 `Shader_Data.params[1]`。arc-mapgen 用作 flow-map UV 归一化（`1.0 / (map_w * tile_size)`）。默认 0.0 让 flow-map 路径退化为 noop，gunslinger 不需要管。

### Sprite 名字查找

`bindings/draw_bindings.c` 新增 JS：`render.sprite_id_by_name(name) -> int`。

游戏脚本不再写死 `render.SPRITE_BG_REPEAT_TEX0` 这类常量；改成在启动时
`const ID = render.sprite_id_by_name("bg_repeat_tex0")` 缓存。返回 -1 表示
未注册。这把 sprite 注册的实现从游戏层耦合中切了出来。

## 合并进 arc-engine 的能力（来自 arc-mapgen）

### bedrock/

| 新增 | 来源 |
|---|---|
| `bedrock/bridge/command_buffer.{c,h}` | 通用 JS→C 命令缓冲（CMD_DRAW_SPRITE/RECT/TEXT 等 opcode） |
| `bedrock/noise/noise.{c,h}` | Perlin/NoiseUtils 计算引擎 |
| `bedrock/ozz/ozzutil.{cc,h}` + `bedrock/ozz/skinned_shader.h` | ozz-animation 的 C wrapper（C++ 实现；游戏 Makefile 选择是否编译） |

ozz 是引擎可选能力：源码包含、shader 提供（`src/shader/skinned.glsl`）、外部库通过 submodule 提供（见下），但**不强制**消费者编译。gunslinger 不引用 → 不付出运行/二进制代价。

### external/

新增 submodule：

| 路径 | 上游 | tag |
|---|---|---|
| `external/ozz-animation` | https://github.com/guillaumeblanc/ozz-animation | 0.16.0 (6cbdc79) |

替代了 arc-mapgen 旧 Makefile 里硬编码的 `/Volumes/thunderbolt/works/11/ozz-animation` 绝对路径。

### bindings/ 合并

九个原本两份分叉的 binding 合并为单一引擎版本：

| binding | 决策 |
|---|---|
| `diag_bindings` | 用 arc-mapgen 版（注释更全） |
| `font_bindings` | 用 arc-mapgen 版（注释更全） |
| `graphics_bindings` | 用 arc-mapgen 版（注释更全） |
| `imgui_bindings` | 用 arc-mapgen 版（清理过的纯 cimgui API；arc-engine 老版混了 nuklear 命名）；保留 gunslinger 用到的 `set_keyboard_focus_here` |
| `input_bindings` | 用 arc-mapgen 版（无功能差异） |
| `loader_buffer` | 保留 arc-engine 版（带 Windows FindFirstFile guard，arc-mapgen 缺） |
| `draw_bindings` | 保留 arc-engine 版（带 Windows popen shim，且没有 arc-mapgen 的 TERRAIN_COLORS 游戏耦合）；新增 `render.sprite_id_by_name` |
| `batch` | 保留 arc-engine 版（已升级 GPU-MVP 管线 + zlayer/tex/view 参数；arc-mapgen 那份是死代码） |
| `unified_mesh` | 保留 arc-engine 版（带 `engine_texture_search_path` API；arc-mapgen 那份硬编码 TEXTURE_PATH_PRIMARY/FALLBACK） |

## 向后兼容

| 既有 API | S3 后行为 |
|---|---|
| 所有现有 `Engine_Config` 字段 | 不变 |
| `engine_register_sprites` / `engine_register_texture_search_paths` | 不变 |
| 所有现有 `bindings/` JS 函数名 | 不变（imgui 实现内部清理，对外接口一致） |
| `Shader_Data.params[1]` | 默认仍为 0；游戏不调 `engine_set_world_inv_size` 就感觉不到 |

gunslinger 在 S3 期间每个里程碑（S3.1a/b/c/d/e/scripts、S3.4 binding 合并）都验证过 clean build + runtime smoke 无回归。

## 双消费者验收（macOS）

同一个 arc-engine commit `a4f1db3` 下：
- `cd sokol-javascript-game-gunslinger && make clean && make && ./gunslinger` — TitleScreen + 游戏循环 OK
- `cd arc-mapgen && make clean && make && ./blueprinter` — Tier-3 测试 24/24 通过 + WindowStack initialized + 地图加载循环 OK

## Windows 验证

Windows 机器恢复后跑通双消费者 build：

| consumer | 平台 | exe | 大小 |
|---|---|---|---|
| gunslinger | macOS / Windows D3D11 | gunslinger / gunslinger.exe | 3.3 MB |
| arc-mapgen | macOS / Windows D3D11 | blueprinter / blueprinter.exe | 3.6 MB (Windows含 ozz) |

Windows 端遇到的可修复问题：
1. `js:` Makefile target 在 Windows 没有 `bash`，必须像 gunslinger 那样改成「检查 build/game_service.js 已存在」分支（commit `e63d111`）。consumer 用 Git Bash 预先跑 `bash scripts/build_game_service.sh`，再 `make` 链接。
2. 一些 submodule 的 https URL 在某些网络环境下 clone 失败（github.com:443 connect timeout）。consumer 在 `.gitmodules` 里把 https 换成 ssh URL 就能拉。这是 git 分发问题，跟代码无关。

代码层面 game-side 的 POSIX-only 调用预先全部 guard 过（`section_mesh.c` 的 `gettimeofday` 替换为 `seconds_since_init`）；引擎层 Windows 路径在 gunslinger 上前轮已经验证。

## Tag（S3 第一轮，已被审核方判 不通过）

| repo | tag | commit |
|---|---|---|
| arc-engine | `v0.3.0` | a4f1db3 |
| gunslinger | `v-post-s3` | 3173874 |
| arc-mapgen | `v-post-arc-engine-integration` | 0434ec9 |

> 这一组 tag 标记了**第一轮 S3 的完成状态**。审核方 2026-04-26 复审后判
> S3 不通过（原因见下文 §S3-REDO），重做后另立 `v0.3.1` / `v-post-s3-redo`
> 系列。这一组 tag **保留** 作为历史"假完成"标记，不删除。

---

# S3-REDO（2026-04-26）

## 起因

`v0.3.0` 当时的"双消费者就绪"声明在 JS 层是假的。审核方复审 + AI 第二轮调
查发现的事实：

- arc-mapgen 的 `scripts/` 下仍保留 26 个与 `arc-engine/scripts/` 同路径的
  forked .js 文件（bedrock/ + render/ + tick/ + rt/ + config/ 五个目录）
- `arc-mapgen/scripts/main.js` 的 `service_path` 仍指自家拷贝
  `scripts/rt/?.js`，而不是 `external/arc-engine/scripts/rt/?.js`
- `arc-mapgen/scripts/build_game_service.sh` 是 200+ 行自包含脚本，从本地
  `scripts/bedrock/` 打包，**没**走 engine 的 bundler template
- 实际 `arc-mapgen/build/game_service.js` 含 14 个 `[bedrock]` 段落，**运行
  时跑的是本地拷贝**

也就是 mapgen 在 JS 层根本不是 arc-engine 的消费者，只在 C 层接入。
"v0.3.0 双消费者就绪" 仅对 C 层成立。

## 三大遗漏（S3 第一轮）

1. **G1 — §S3.0 备份没建**：计划要求 `_backup/arc-mapgen-pre-s3/` 物理备份，
   实际只有 `_backup/gunslinger-pre-extraction/`。git tag `pre-arc-engine-
   integration` 仍在，可作替代来源。
2. **G2 — §S3.1.4 bedrock/gfx 未"以 mapgen 版作基准"**：26 个 C 文件中
   27/28 与 gunslinger pre-extraction 行数完全一致，证明前轮直接用
   gunslinger S1.4 裁剪版顶替，未做 mapgen 版三方决策。复查后判定**功能层
   面 engine 已正确演化**（动态 sprite 数组 / view registrar / Windows
   thread guard 都在），G2 实质问题是**没建决策记录**，已由
   `docs/s3_redo_gfx_decisions.md` 补齐。
3. **G3 — §S3.4 第 4-5 条 JS scripts 完全跳过**：见上文起因。这是 S3-REDO
   真正要做的工作。

详见 `plan_arc_mapgen_integration.md` 的 §S3-REDO 章节。

## S3-REDO 主要交付

### 0. 备份（G1 修正）

`_backup/arc-mapgen-pre-s3-redo/` (22M, 含 src+scripts+docs+Makefile)、
`_backup/arc-engine-pre-s3-redo/` (22M)、`_backup/gunslinger-pre-s3-redo/`
(41M)，三仓都打 `pre-s3-redo` tag。

### 1. gfx 26-file 决策表（G2 补齐）

`docs/s3_redo_gfx_decisions.md`：逐文件三方 diff（mapgen pre-S3 / gunslinger
pre-extraction / 当前 engine），剥离注释后判定全部 28 个为 A 类（engine
已正确演化）。无源码改动，只补决策证据。

### 2. JS scripts 26-file 迁移（G3 真正修复）

#### 引擎侧：3 个新 hook

- **`StartupFlow.registerStartupSteps(steps)`**：`bedrock/99_startup.js` 暴
  露的扩展点，让游戏在 GameInit DATA_LOADED 与 MAP_READY 之间插入异步
  generator 步骤（mapgen 用来跑 DefDatabase 加载、atlas 预加载、terrain
  prebuild 等 124 行原本 fork 在 99_startup.js 里的逻辑）。
- **`RenderFrameCallbacks.register(fn, name)`**：`bedrock/03_frame_callbacks.js`
  暴露的 mainthread 帧 hook（终点是 `rt/render_service.js` 的
  `render_frame`），让游戏注入 GPU-touching 的每帧工作（atlas upload、
  section_mesh.upload_gpu、terrain precollect 等）而不必 fork
  render_service.js。
- **`Loader.registerSummaryHandler({name, handler})`**：`bedrock/03_loader.js`
  从 game-specific（pawn / plant / building_door 等硬编码 RimWorld 文件
  名）改为 path-list-driven 框架，文件清单完全由游戏注入。

#### 引擎侧：边界净化

- `rt/render_service.js` 删除内置的 RenderSetup / Coord.* / HUD
  ScaffoldCallback chain（gunslinger 时代的遗留默认值，跟 mapgen 自家 chain
  撞 priority）。两个消费者各自注册自己的渲染管线 chain。
- `bedrock/03_loader.js` 删除 `pawn_add` / `entity_add` / `EntityType` /
  `loadEntitiesFromArray` 等 mapgen 业务符号。

#### 引擎侧：bundler template 扩展

- `scripts/build_game_service.sh.template` 加 `GAME_TOPLEVEL_DIRS` 环境变
  量，支持 mapgen 这种顶层目录有 38 个、加载顺序敏感的项目（之前 template
  只认识 `scripts/game/<name>/` 子目录）。
- 删除 template 内 game-side 的 `bedrock/render/tick/config` 自动 mirror
  pass（违反 GAME_TOPLEVEL_DIRS 声明的顺序，导致 mapgen `things/` 在
  `render/` 之后加载，触发 GenAdj is not defined 运行时错误）。
- 加 dedup + 限定扫描范围，避免误报跨 react/node_modules 的 console.log /
  globalThis 重复定义。

#### 消费者侧（arc-mapgen）

- `scripts/main.js` 的 `service_path / bootstrap_path / jslib_path /
  builtin_service_path` 全部改成 `external/arc-engine/scripts/...` 与
  `external/arc-engine/external/jtask/...` 前缀。
- `scripts/build_game_service.sh` 改成 12 行 wrapper：仅 export 路径 /
  GAME_TOPLEVEL_DIRS / DUP_ALLOWLIST / BUNDLE_EXTRA_SERVICES，调
  `external/arc-engine/scripts/build_game_service.sh.template`。
- 删除 25 个 forked .js（bedrock/00_time, render/00_graphics 等所有真为
  E1 级 trailing-whitespace-only 的拷贝）。
- `scripts/rt/render_service.js` (800 行) 重构为
  `scripts/game/arc/00z_mapgen_render.js`：删掉 service 注册框架（engine
  rt/render_service 已提供），保留 mapgen 专属的 prebuildTerrainSections /
  draw_terrain_section_mesh / draw_buildings / draw_dynamic_things / 全套
  ScaffoldCallback 注册。
- 新增 `scripts/defs/00_run_mode_manifest.js` 注入 17 个 mapgen run mode
  到 `globalThis.__RUN_MODE_MANIFEST__`（engine RunMode 是 lazy getter，从
  这里读）。defs/ 是第一个 GAME_TOPLEVEL_DIR，确保 mapgen/ 任何模块读
  `RunMode.config` 之前已就绪。
- 新增 `scripts/game/arc/000_engine_ext.js` 安装 mapgen 的 4 个引擎扩展：
  Loader summary handler / 6 个 startup steps / 1 个 RenderFrameCallback
  （section_mesh.upload_gpu + terrain_precollect mainthread）。

### 3. 命名与边界 cleanup（验收期发现的 2 个 bug）

- **GAME_CONFIG namespace 统一**：arc-engine S16-B3 把 `globalThis.config`
  重命名为 `globalThis.GAME_CONFIG`（`render/00_res_2_material.js:46` 读它
  拿 TEXTURE_PATHS）。mapgen 的 `game_bindings.c` 仍只挂 `config`，导致
  MaterialPool 拿不到 TEXTURE_PATHS，所有 actor / 物品 / 植物 sprite 加载
  失败（terrain 走预烘焙 atlas，不受影响）。修复：mapgen `game_bindings.c`
  统一只挂 `GAME_CONFIG`，删除 legacy `config` 别名；`render/10_texture_atlas.js`
  唯一一处读 `globalThis.config?.TEXTURE_PATHS` 改成 GAME_CONFIG。
- **ScaffoldCallback chain 归属**：见上文"引擎侧 边界净化"。

### 4. Singleton lock（process-level 单实例锁）

`Engine_Config.app_name` 字段（opt-in）→ `arc_engine_create()` 通过
`flock(LOCK_EX | LOCK_NB)` 在 `/tmp/<app_name>.lock` 上拿排他锁，第二个
同名实例读出 lock 文件里的 pid / started / cwd / argv 后 stderr 打印再
exit(1)。kernel 在 process 退出时自动释放，无需 atexit。fail-open（open
失败时 warn 后 allow，避免开发期被锁死）。文档见
`docs/singleton_lock.md`。

起因：S3-REDO 验证期间一次 AI 自动化失控同时启动 14 个 blueprinter 实
例，机器接近无响应。根本修复是引擎层禁止多实例，而非依赖 AI / 用户自觉。

mapgen / gunslinger 各自在 main.c 设 `.app_name = "blueprinter"` /
`"gunslinger"` opt in。跨消费者不互斥（不同 app_name → 不同 lock 文件）。

## 验收

S3R.4 macOS 双消费者验收（同一 arc-engine commit）：

| 消费者 | 测试 | 结果 |
|---|---|---|
| arc-mapgen | `make test-auto` | ✅ 11 ScaffoldProfiler frame reports / 27 unified_mesh slot allocs / 0 missing texture warnings / actor + 物品 + 植物可见（用户视觉确认） |
| gunslinger | `./gunslinger` | ✅ 启动到 imgui TitleScreen，等用户点 Start（已知行为） |
| 双消费者 | 同 arc-engine commit `d64a36d` 下两个分别 build | ✅ 都通过，互不冲突 |

Singleton lock 验收：8/8 PASS（详见 commit `d64a36d` 的实现报告）。

S3R.5 Windows 验收：**本次 S3-REDO 范围外**，留作后续 follow-up（计划见
`plan_arc_mapgen_integration.md` §S3R.5）。

## Tag（S3-REDO，真双消费者就绪）

| repo | tag | 说明 |
|---|---|---|
| arc-engine | `v0.3.1` | 含本 changelog 的最终 commit；上一位是 singleton lock。`git rev-parse v0.3.1` 取 hash |
| arc-mapgen | `v-post-s3-redo` | feature/combat 分支 HEAD（singleton lock opt-in） |
| gunslinger | `v-post-s3-redo` | main 分支 HEAD（singleton lock opt-in） |

> commit hash 不写在文档里 —— 它会随 amend 漂移。tag 名字稳定，`git
> show v0.3.1` 自然展示完整信息。
