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

## Tag

| repo | tag | commit |
|---|---|---|
| arc-engine | `v0.3.0` | a4f1db3 |
| gunslinger | `v-post-s3` | 3173874 |
| arc-mapgen | `v-post-arc-engine-integration` | 0434ec9 |
