# S1.6 Review：我认定需要重构的混乱点

本文只分析不动代码。你确认方向后我再按清单整改。

---

## A 类：引擎入口 + js_runtime 回调机制（最核心）

### 现状

引擎对游戏暴露了 **两层互相交叠的 API**：

**1. `Engine_Config`（engine_main.h）**，游戏填完传给 `engine_run()`：
- 窗口配置：`window_title / window_w / window_h`
- 启动状态：`default_run_mode / master_volume / zoom_level / camera_initial_pos / camera_initial_rot`
- 一次性动作：`startup_ambiance_event`
- JS 路径覆盖：`jtask_bootstrap_path / js_main_script_path`
- 生命周期回调：`on_init / on_register_js_bindings / on_frame / on_cleanup`

**2. `js_runtime.h` 的三个全局 setter**，引擎内部用、但暴露在 header 里：
- `js_runtime_set_bootstrap_path(path)`
- `js_runtime_set_main_script_path(path)`
- `js_runtime_set_game_bindings_registrar(fn)`

`engine_run()` 的职责就是把 Engine_Config 里对应字段转发给这三个 setter，然后调 `js_runtime_init()`。

### 问题

| # | 问题 | 后果 |
|---|------|------|
| A1 | **两条平行 API 做同一件事** | 新消费者看到 `js_runtime.h` 的三个 setter 不知道自己该不该直接用；看到 Engine_Config 也不知道这些字段最后是不是就绕到 setter 上。两套 API 的演化路径容易漂移。 |
| A2 | **三个 setter 的生命周期管理是复制粘贴的 boilerplate** | `strdup` + `free(old)` + `NULL` 检查在 js_runtime.c 里写了两遍（`g_bootstrap_path_override` / `g_main_script_path_override`），第三个 `g_game_bindings_fn` 只是指针所以没 strdup — 三个一组但代码不对称。 |
| A3 | **全局可变状态过多** | js_runtime.c 现在有 7 个 static globals（`g_runtime / g_context / g_coroutine_mgr / g_initialized / g_bootstrap_path_override / g_main_script_path_override / g_game_bindings_fn`）+ engine_main.c 有 6 个（`window_w / window_h / g_log_level / g_run_mode / g_master_volume / g_cfg / g_saved_argv / g_saved_argc / ctx / _game_state / last_frame_time`）。这里面部分是历史遗留的 C 风格（`extern int window_w`），部分是我 S1.6 新加的（`g_cfg` 拷贝），**生命周期和可见性没有统一原则**。 |
| A4 | **Game_State 和 Core_Context 结构体重复声明** | engine_main.c（文件作用域，作为定义）+ engine_bindings.c（14-29 行，作为一份独立的 typedef）— 两份声明**字段必须完全一致**否则 UB。C 里这种 UB 链接器不检查，一不留神就崩。 |
| A5 | **extern 跨文件的全局变量网络** | `window_w / window_h / g_master_volume / app_get_argv / app_get_argc` 被 engine_bindings.c 跨文件 extern 拿。对游戏侧也是一样 — 如果游戏的 C 代码想读窗口尺寸，得 `extern int window_w;`。这是 80s 风格，不是我现代化引擎应有的样子。 |
| A6 | **`on_register_js_bindings` 的"void\* JSContext"签名** | 为了避免 engine_main.h 里 include `quickjs.h`，回调签名用 void*，游戏每个 binding 函数调用时都得 `(JSContext *)js_ctx` 手工 cast。代码是对的但每个消费者都得重复这个 cast。 |

### 建议重构方向（**不要立刻做**，仅列选项）

- **方向 1（最小清理）**：保留 Engine_Config 为唯一公共 API，把 `js_runtime.h` 的三个 setter 改成 internal header（如 `js_runtime_internal.h`），只让 engine_main.c 引用。A1/A2 解决，A3~A6 不动。
- **方向 2（中等清理）**：把所有引擎全局状态收进一个单例结构体 `Arc_Engine_State`（含 window / run_mode / master_volume / cfg / ctx / game_state / js_runtime overrides），提供 `arc_engine_state()` accessor。engine_bindings.c 通过这个 accessor 访问，不再 extern。Game_State 和 Core_Context 提到新的 `arc_engine/src/bedrock/engine_state.h` 共享头文件。A1~A5 解决。
- **方向 3（彻底）**：Engine_Config 改成纯 POD 参数包，引擎入口重写为 `Arc_Engine *arc_engine_create(cfg)` + `arc_engine_run(eng)` + `arc_engine_destroy(eng)` 面向对象风格；所有回调改成 `void (*)(Arc_Engine *eng, ...)`，游戏通过 `arc_engine_js_context(eng)` 这种函数拿 JSContext（强类型，因为头文件 include 顺序是"接受 QJS include"）。A1~A6 全解决，但 API 破坏性大，消费者要同步改。

我**倾向方向 2**，在 S1.7 完成前做完。理由：

- 方向 1 太保守，只是把 setter 藏起来，底层全局变量蛛网不动
- 方向 3 会让 gunslinger 的 main.c 和 arc-mapgen 将来接入时都要同时重写入口代码，S1 阶段风险过大
- 方向 2 改引擎内部，对 Engine_Config 公共面不破坏，gunslinger 不改；但内部从 extern 蛛网变成结构体 + accessor，可读性质的提升

---

## B 类：JS config 对象的"半挂半落"状态

### 现状

历史遗留：`engine_bindings.c`（原名 `game_bindings.c`）曾经在 `js_init_engine_module()` 里向 `globalThis` 注册了一个 `config` 对象，塞满 gunslinger 的常量（`TILE_SIZE / TILE_COLOR_LIGHT / TEXTURE_PATH_PRIMARY` 等）。S1.4 我把这块删了 —— 因为"引擎不该知道游戏常量"。

迁移后的现状：
- **JS 侧**：gunslinger 的 `scripts/game/gunslinger/00_gunslinger_config.js` 重新注入 `globalThis.config.TEXTURE_PATHS / TILE_SIZE / HALF_TILE_SIZE`
- **C 侧**：`arc-engine/src/bindings/unified_mesh.c` 里 **硬编码**：
  ```c
  #define TEXTURE_PATH_PRIMARY  "res/images"
  #define TEXTURE_PATH_FALLBACK "res/images"
  ```
- 唯一 JS 侧读 `config.TEXTURE_PATHS` 的是 `arc-engine/scripts/render/00_res_2_material.js:46`，fallback 空数组

### 问题

| # | 问题 | 后果 |
|---|------|------|
| B1 | **"贴图搜索路径"在两个地方各自决定** | C 侧 unified_mesh 自己硬编码 "res/images"；JS 侧走 `config.TEXTURE_PATHS`。两个路径规则不统一，游戏想改贴图根目录得同时改 C 代码 + JS config — 违反 DRY。 |
| B2 | **`TILE_SIZE / HALF_TILE_SIZE`"注入到 config 但游戏代码里也没人读它"** | 我 S1.6 时 grep 过 gunslinger 的 JS 代码，没有一处读 `config.TILE_SIZE`。这是我防御式写的无效代码，应删。 |
| B3 | **两个"config"概念重名** | `globalThis.config`（游戏贴图常量）vs `RunMode.config`（run-mode manifest 的 "config" 子字段）。代码里出现 `config.foo` 时歧义 — 需要是 `globalThis.config.foo` 才对上。 |

### 建议重构方向

- B1：`unified_mesh.c` 不该自己硬编码贴图路径。正确做法是引擎提供 `engine_set_texture_search_paths(const char **, int)` API，游戏 `on_init` 里调用；或者让 JS 层在调 unified_mesh 的相关 API 时直接传完整路径、C 层不搜索。S2 做。
- B2：删掉 `config.TILE_SIZE / HALF_TILE_SIZE` 的无效注入。S1.7 就做，成本极低。
- B3：把游戏常量挂在 `globalThis.GAME_CONFIG` 或 `globalThis.GunData`（gunslinger 已有）下，不要叫 `config`。S1.7 就做。

---

## C 类：startup 决策链 + RunMode 的 lazy 化

### 现状

两处被我"lazy 化"来解决加载顺序问题：

**C-1. `99_startup.js`**：
- `if (typeof TitleScreen !== 'undefined' && TitleScreen.active)` 判断从文件顶层搬到 ScaffoldCallback 内部
- 引入 `_decided` 布尔（闭包变量）保证 `beginStartup` 只调用一次
- 用 `globalThis.TitleScreen` 绕过 bundle 时的 undefined 引用

**C-2. `00_run_mode.js`**：
- `MODE / _manifest / config` 全部改成 getter
- 每次访问都读 `globalThis.__RUN_MODE__` / `globalThis.__RUN_MODE_MANIFEST__`

### 问题

| # | 问题 | 后果 |
|---|------|------|
| C1 | **两处"lazy"风格不一致** | 99_startup.js 用"第一次 scaffold tick 时决策 + 闭包变量"；00_run_mode.js 用"属性 getter"。同类问题不同解法，新人读代码要理解两种模式。 |
| C2 | **99_startup.js 的控制流现在很拧巴** | `_decided / _systemReady` 两个布尔 + 嵌套 if + 每帧检查 `globalThis.TitleScreen`。功能是对的，但**读一遍代码要 30 秒才能 reason 清楚**。原来是简单的 `if-else`，现在是状态机。 |
| C3 | **"游戏可以覆写引擎 startup 行为"的能力是隐式的** | 游戏通过定义 `globalThis.TitleScreen = {...}` 隐式地被 99_startup.js 的 ScaffoldCallback 发现。没有 registrar、没有 API 文档、没有类型。这对 arc-engine 要支持多游戏的目标是反面教材。 |

### 建议重构方向

- C1+C2+C3 一起解决：把"startup flow"抽成 arc-engine 的一个命名 API，例如：
  ```javascript
  globalThis.StartupFlow = {
      // 引擎启动序列，游戏注册钩子点
      registerPreBootHook: function(fn) { ... },   // 决定是否进入 beginStartup
      registerTitleScreen: function(obj) { ... },  // 注册 TitleScreen 对象（若有）
  };
  ```
  gunslinger 的 98_title_screen.js 在定义完 TitleScreen 后调 `StartupFlow.registerTitleScreen(TitleScreen)`。引擎的 99_startup.js 变成"如果有 TitleScreen 就等它、否则直接 beginStartup"，代码回到简单分支。

这个改造对 arc-engine 将来接 arc-mapgen 尤其重要 —— arc-mapgen 有更复杂的 startup（主菜单 → 选场景 → 选地块），需要明确的 hook 协议。

S1.7 做 C1+C2 最小清理（至少让 99_startup.js 回到可读状态）。C3 的 StartupFlow registrar API 留到 S2。

---

## D 类：S1 过渡期的其它遗留（bonus，我边 review 边想起来的）

| # | 问题 | 成本 |
|---|------|------|
| D1 | **`scripts/main.js` 在 arc-engine 和 gunslinger 各一份**，内容相似只是路径前缀不同。arc-engine 版本有个 "REFERENCE IMPLEMENTATION" 注释但没被任何代码 include — 应该改为真·模板文件（如 `scripts/main.js.template`）让消费者 cp 而不是看到后以为能用 | 低 |
| D2 | **bundler 的 `DUP_ALLOWLIST` 在 gunslinger 侧包含 `Find RealTime __BP_VERBOSE__ graphics Current config`** — `config` 在这里是因为 B 类问题（游戏和引擎都写 globalThis.config）；其它几个是 RimWorld/arc-mapgen 遗留的白名单，gunslinger 根本没这些符号。应当精简到 gunslinger 实际需要的 | 低 |
| D3 | **engine_main.c 的 on_frame 里硬编码了 WASD/QE/scroll 的相机控制** 和 `BASE_ORTHO_SIZE = 10.0f`。对 gunslinger 没问题，但对将来的新游戏（比如纯 UI 的小工具）这些输入/镜头控制是多余的。应当是可通过 Engine_Config 开关的 | 中 |

---

## 推荐动作清单（全部 S1 做完，不留尾巴进 S2）

原本拟了"最小/中等/大改动"三档，把部分挪 S2。用户指出 S2 理应在一个干净起点上启动，13 项全部 S1 做完。唯一消费者 gunslinger 在本仓库，API 变更可同步修改，"破坏性"不是推迟理由。

执行顺序（每 step 小改动 → gunslinger 构建跑通 → 独立 commit）：

| # | 问题 | 动作 |
|---|------|------|
| 1 | A1 | 三个 setter 和 `JS_Runtime_Game_Bindings_Fn` 移到 `js_runtime_internal.h`，只 engine_main.c 引用 |
| 2 | A4 | 建 `engine_state.h`，集中 `Game_State / Core_Context / extern globals`，消除重复声明 |
| 3 | A5 | 上述 extern 全部改走 `engine_state.h`，engine_bindings.c 不再裸 extern |
| 4 | A2+A3 | 建 `Arc_Engine_State` 单例 + accessor，消除分散 static globals 和 strdup boilerplate |
| 5 | A6 | `on_register_js_bindings` 签名改成 `JSContext *`（engine_main.h 直接 include quickjs.h）；或引入强类型 handle |
| 6 | A 方向 3 | Engine_Config 面貌改为 `arc_engine_create(cfg)` + `arc_engine_run(eng)` + `arc_engine_js_context(eng)` 对象化 API，gunslinger main.c 同步修改 |
| 7 | B2 | 删除 `config.TILE_SIZE / HALF_TILE_SIZE` 无效注入 |
| 8 | B3 | 游戏常量 `TEXTURE_PATHS` 从 `globalThis.config` 改挂 `GAME_CONFIG`（释放 `config` 名字）；`arc-engine/scripts/render/00_res_2_material.js` 同步 |
| 9 | B1 | 引擎提供 `engine_set_texture_search_paths()` C API + JS 侧的对应注册；`unified_mesh.c` 不再硬编码 "res/images" |
| 10 | C1+C2+C3 | 建 `globalThis.StartupFlow`：`registerTitleScreen(obj)` + `registerPreBootHook(fn)`；`99_startup.js` 回到简单 if-else；`00_run_mode.js` 的 getter 保留（这个是对的） |
| 11 | D1 | `arc-engine/scripts/main.js` 改名 `main.js.template`，arc-engine 自己不再有可执行的 main.js |
| 12 | D2 | gunslinger `DUP_ALLOWLIST` 精简到实际使用的符号（去掉 RimWorld 遗留 `Find / RealTime / Current` 等） |
| 13 | D3 | engine_main.c 的 WASD/QE/scroll 相机控制 + `BASE_ORTHO_SIZE` 提取到 `Engine_Config` 的可选字段；默认保持现状使 gunslinger 不改 |

执行纪律：
- 每 step 一个独立 commit
- 每 step 后 gunslinger 必须构建通过 + TitleScreen 显示 + 点 Start 进游戏无回归
- 失败立即停，汇报用户，不扩大范围
- 禁止破坏性 git 操作
- 引擎侧改动 push arc-engine；gunslinger 侧 submodule pointer 跟随 bump
- 每 step 完成后更新 MEMORY.md 当前任务块

---

## 发现的预存问题（本次重构范围外）

### D4 (preexisting) — `VIEW_*` 宏双重定义

**证据**：step 9 全量重编 unified_mesh.c 时报 5 条
`'VIEW_tex0'/'VIEW_flow_map'/'VIEW_ripple_tex'/'VIEW_noise_tex'/'VIEW_font_tex' macro redefined`
warning。

两处定义点：
- `src/types.h:97-101` — 明文整数（`#define VIEW_tex0 0`）
- `src/bedrock/gfx/generated_shader.h:108-112` — 括号整数（`#define VIEW_tex0 (0)`）

`generated_shader.h` 是 sokol-shdc 自动生成的产物；`types.h` 的手写定义与生成器输出重复。

**后果**：
- warning 噪音；
- 真实 bug 风险低（两边值一致），但若 sokol-shdc 输出改变（例如 slot 重排），两份定义就会悄悄错位。

**根因**：S1.4 移植时，types.h 把 VIEW 宏作为"引擎共享常量"复制过来，未察觉 generated_shader.h 已是权威来源。

**修复方向（不在 13 step 内做）**：
- 删 `types.h` 里的 5 个 `#define VIEW_*`，让 `generated_shader.h` 成唯一定义点；
- 或反过来删生成器的 `#define` 段（需改 sokol-shdc 模板），保留 types.h —— 更不推荐因为对抗生成器。

走第一条，2061cd9 落地。

### D4.1 (D4 回归修复) — `batch.c` 清理后找不到 `VIEW_tex0`

2061cd9 假设所有 VIEW_* 消费者都直接 include generated_shader.h，未核查
transitive include 链。`src/bindings/batch.c` 原本没 include 这个头
文件，靠 types.h → 某条间接链拿到 VIEW_tex0。D4 fix 后，增量 make 因
`batch.o` 已缓存没察觉，`make clean && make` 报：

```
batch.c:329: error: use of undeclared identifier 'VIEW_tex0'
batch.c:342: error: use of undeclared identifier 'VIEW_tex0'
```

**修复**：2c8ca7c — batch.c 加 `#include "bedrock/gfx/generated_shader.h"`。
审计其余两个 VIEW_* 消费者（render.c / graphics.c）都已直接 include，
无其他遗漏。

**教训**：删 header 内容前必须 `make clean && make`，不能只信增量 make。

---

## S1.6 接受的设计折衷

下列选择不是"遗漏"，是 s1.6 阶段刻意的折衷；记录下来避免未来 reviewer
当成 bug 再提一次。

### A3 折衷 — `Arc_Engine_State` 仅收新增状态，老全局变量保留 `extern`

Step 4 (6cf8dc2) 建的 `Arc_Engine_State` 结构体**只包含 s1.6 新加或原本
分散在 `js_runtime.c` 里的 7 个 static 中的 3 个**：

收进单例：
- `saved_argv / saved_argc` (原 engine_main.c static)
- `last_frame_time` (原 engine_main.c static)
- `js_bootstrap_path / js_main_script_path / js_game_bindings_fn`
  (原 js_runtime.c 的 3 个 static)

**没收进单例，保留原 `extern`**：
- `ctx` (Core_Context) — engine_state.h 统一声明，但存储还在 engine_main.c 文件作用域
- `window_w` / `window_h` — 同上
- `g_master_volume` — 同上
- `g_log_level` / `g_run_mode` — 引擎级简单全局
- `g_runtime` / `g_context` / `g_coroutine_mgr` / `g_initialized` — js_runtime.c 内部 QJS 运行时对象
- 一堆 `service_id` / atomic ready flag — jtask wrapping 细节

**折衷理由**：
1. `ctx / window_w / window_h` 被 `engine_bindings.c` 和 `helpers.c` 热
   路径直接读，搬进 `arc_engine_state()->xxx` 要改 30+ 处调用点，改动
   大于收益（已经通过 step 3 统一到 `engine_state.h` 的声明，UB 风险
   消除了）。
2. `g_runtime / g_context / service_ids` 是 QJS + jtask 的运行时对象，
   属于 "js_runtime 模块私有状态"而不是"引擎配置状态"。把它们塞进
   `Arc_Engine_State` 会把单例变成一个大杂烩。
3. `g_log_level / g_run_mode` 是 parse_args 写一次、全程只读的平坦配置
   值，收进结构体没换来生命周期管理收益。

换言之 `Arc_Engine_State` 的当前边界是 **"跨 TU 共享的引擎配置状态 +
需要 strdup/free 生命周期管理的字段"**，不是"所有引擎全局"。这条边界
是 s1.6 的终点，不是 s2+ 不能再动 —— 如果将来 "引擎内部状态搬家成本"
下降了（例如 ctx 访问已经 wrap 进 accessor），可以再挪。

### A6 折衷 — `js_runtime_get_context()` 保留（即使 `arc_engine_js_context` 是公共入口）

Step 6 (b451810) 新加了对象化 `arc_engine_js_context(eng)` 作为公共面
API。但底层 `js_runtime_get_context()` 没删 —— `arc_engine_js_context`
就是 wrap 它的一行。

**折衷理由**：
- `js_runtime.h` 其余函数（init / shutdown / render / handle_external_command）
  都是 engine_main.c 内部消费，`get_context` 放在同一个 header 里并无
  污染；
- 删它需要把 `arc_engine_js_context` 的实现挪到 js_runtime.c 内部或
  把 g_context 改成 extern，前者破坏分层（engine_main.c 的公共 API 实
  现应该在 engine_main.c），后者又回到裸 extern 蛛网。
- 结论：`js_runtime_get_context` 是 "engine-internal accessor"，`arc_engine_js_context`
  是 "game-facing wrapper"，两个各司其职，不重复。

### S2 开放的议题（不是折衷，是已知待办）

- `js_runtime.c` 现在还 include 了 11 个 bindings/ 头文件直接注册，
  根据 `arc-engine/CLAUDE.md` §"引擎边界规则"，S1.5 规划过用回调注入
  机制改造，本次 s1.6 范围未包含。S2 处理。
- shader 业务分支的硬编码（CLAUDE.md §3）同上，S2 处理。
