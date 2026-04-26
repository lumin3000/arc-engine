# arc-engine

基于 sokol_gfx + QuickJS + jtask 的 2D 游戏引擎，供多个小游戏项目并行复用。

## 特性

- **C 层渲染**：sokol_gfx 驱动，跨平台（macOS/Windows/Linux/WASM）
- **JS 游戏逻辑**：QuickJS 解释执行，jtask 协程调度
- **即时 GUI**：Dear ImGui 集成
- **资源管线**：atlas 打包、字体 SDF、音频（FMOD）
- **热加载**：开发期无重启迭代

## 架构

```
src/bedrock/         引擎核心（gfx/input/sound/utils/js_runtime）
src/bindings/        JS→C 通用绑定（batch/draw/graphics/input/font/imgui/diag/engine）
src/shader/          GLSL 着色器源
scripts/bedrock/     引擎 JS 骨架（time/types/loader/game/find/startup ...）
scripts/render/      渲染封装
scripts/tick/        tick 系统
scripts/rt/          jtask 服务（start/loader/render）
external/            第三方依赖
tools/               atlas 打包等
```

## 接入方式

消费者（游戏项目）通过 git submodule 接入：

```bash
git submodule add git@github.com:lumin3000/arc-engine.git external/arc-engine
```

游戏项目自身提供：
- `src/main.c` 调用 `engine_run(Engine_Config*)`
- `src/game/` 游戏专属 C 代码
- `scripts/game/<name>/` 游戏专属 JS 逻辑
- `res/` `data/` 资源

详见 [docs/how_to_consume.md](docs/how_to_consume.md)。

## 引擎边界

- `bedrock/` **禁止** 依赖 `bindings/`（单向依赖）
- 引擎不认识任何游戏专有名词（grep 不到任何游戏专属概念）
- shader 的业务分支通过扩展点外挂，不在核心 shader 里硬编码

详见 [CLAUDE.md](CLAUDE.md)。

## 依赖

| 库 | 用途 | License |
|----|------|---------|
| sokol | 跨平台图形/窗口/音频 | zlib |
| QuickJS-ng | JS 运行时 | MIT |
| jtask | 协程任务调度 | 私有 |
| ltask | jtask 依赖 | MIT |
| Tina | fiber 实现 | MIT |
| Dear ImGui (cimgui) | 即时 GUI | MIT |
| stb | 图像/数据结构 | Public Domain |
| FMOD | 音频中间件 | 专有（indie 免费） |

详见 [external/VERSIONS.md](external/VERSIONS.md)。
