# Phase 1: Asyncify 接入 — wasm 协程基础支持

**目标**：让 jtask stackful 协程在 wasm 浏览器上跑起来，业务代码 0 改动。

**前置**：Phase 0（清理废弃 quickjs_coroutine）已完成。Phase 0.5（QuickJS+Tina+fiber demo）已通过 native + Node + Chrome 三平台验证。

---

## 零、最高优先级：隔离原则

**所有 wasm 改动必须只在 wasm 编译路径生效。Native 编译路径字节级不变。**

这是 Phase 1 的最高优先级，凌驾于所有功能目标之上。任何改动如果可能污染 native，必须先想办法隔离再做。

### 0.1 隔离边界（按层）

| 层 | 隔离机制 | 隔离条件 |
|---|---|---|
| C 源码 | `#if TINA_ABI_wasm` / `#ifdef __EMSCRIPTEN__` | 预处理阶段跳过分支 |
| 头文件包含 | wasm-only 的 `#include <emscripten/...>` 必须在 `#ifdef` 块内 | native 编译不解析 |
| 全局变量/TLS | wasm-only 变量在 `#ifdef` 块内声明 | native 二进制无符号 |
| 结构体字段 | 实在要加 wasm-only 字段，用 `#ifdef` 包字段 | native 结构体大小/布局不变 |
| Makefile | `ifeq ($(PLATFORM),wasm)` 包住 wasm-only flag/源文件/依赖 | native 编译命令不变 |
| Shader | wasm 用 WGSL，native 用 Metal/D3D11/GL —— 已有平台分支机制 | 复用现有分支 |
| 产物路径 | wasm 产物和 native 产物输出到不同目录 | 不互相覆盖 |

### 0.2 不可接受的改动模式

❌ **修改 native 共用的 .c/.h 主流程**（即便是为 wasm 加分支）—— 用 `#ifdef` 包整段
❌ **在 native 用得到的全局符号上加 wasm 行为**
❌ **结构体加 wasm-only 字段不用 `#ifdef` 包**（即便字段不被 native 使用，也改变了 sizeof）
❌ **Makefile 在 native 路径下引入 wasm 工具链依赖**（emcc/wgsl shader compiler 等）
❌ **改动后 native 二进制大小、行为、性能任何一项变化**

### 0.3 隔离验证清单

每次提交 wasm 相关改动前，**强制**做以下三件事：

1. **Native build 不变**：
   ```
   make clean && make arc
   ```
   生成的 `blueprinter` 二进制大小和改动前一致（差异只在 timestamp/build path）

2. **Native test-auto 不变**：
   ```
   make test-auto 2>&1 | tee /tmp/test_after.log
   diff /tmp/test_before.log /tmp/test_after.log
   ```
   diff 只能有时间戳/帧数等非确定性行变化，业务输出完全一致

3. **Native frame profile 不变**：
   `[ScaffoldProfiler]` 输出的 avg/max/帧时间在 baseline ±5% 内

**任一不通过，立即 revert 那一笔改动**，重新设计隔离方式。

### 0.4 wasm 分支引入的物理可能性

C 预处理器规则保证：`#elif/#endif` 之间的代码在条件不满足时**完全不解析**，包括 `#include`。也就是说：

```c
#elif TINA_ABI_wasm
    #include <emscripten/fiber.h>
    // ... wasm 代码
#endif
```

native 编译（`__EMSCRIPTEN__` 未定义，`TINA_ABI_wasm` 为假）时，预处理器不会去查 `<emscripten/fiber.h>`，**即使 native 系统没装 emcc 也没问题**。

这是稳定的隔离机制。

---

## 一、本质对照（一句话）

soluna 用 lua VM → 自带堆栈协程能力 → wasm 不用付额外代价
我们用 QuickJS → 没自带 → wasm 上必须用 Asyncify 补上等价能力

**Asyncify 在我们方案里 = lua VM 在 soluna 里的协程堆栈能力**。代价（二进制+性能税）是用 QuickJS 必须付的。

---

## 二、Phase 0.5 已验证的核心结论

1. ✅ Tina 同步 API 在 wasm 上**可以**实现（不是语义冲突）
2. ✅ tina.h wasm 分支 ~120 行代码已写好（在 `/Volumes/thunderbolt/works/11/tinademo/tina.h`）
3. ✅ QuickJS 解释器栈跨多次 yield 完整保留
4. ✅ JS 局部变量（const）跨 yield 不丢
5. ✅ wasm 二进制 baseline：QuickJS + Asyncify ≈ 4.9MB
6. ✅ Chrome (V8) 行为跟 native 一致

**关键 fix**：`_tina_swap` 入口必须把 `*sp_from == NULL` 补成 TLS `current_fiber`。Tina native 假设 SP 现场可写，wasm fiber 要求传"已存在的 fiber 对象"。

---

## 三、Phase 1 改动清单

每一项标注 **隔离方式** 和 **native 不变性证明**。

### 3.1 tina.h wasm 分支移植

**位置**：`arc-mapgen/external/arc-engine/external/Tina/tina.h`

**操作**：把 Phase 0.5 验证过的 wasm 分支代码（~120 行）移植到 mapgen submodule 的 tina.h。

**隔离方式**：
- 整段包在 `#elif TINA_ABI_wasm ... #endif` 里
- `TINA_ABI_wasm` 定义为 `(__EMSCRIPTEN__)`，native 下为 0
- 所有 `_Thread_local` 静态变量、`#include <emscripten/fiber.h>` 都在 `#elif` 块内
- ABI 探测段加 `#define TINA_ABI_wasm (__EMSCRIPTEN__)` 一行

**native 不变性证明**：
- 改动只新增 ABI 分支，native 走的是已有 ABI 分支（aarch64/SysV_AMD64 等）
- 预处理阶段 native 不进入 wasm 分支
- tina 结构体不动（wasm 复用 `_stack_pointer` 字段存 fiber 指针）

**验证**：`make arc` `make test-auto` 输出与 baseline 一致。

### 3.2 jtask Makefile wasm target

**位置**：`arc-mapgen/external/arc-engine/external/jtask/Makefile`

**操作**：增加 wasm target，所有 wasm-only flag 用 `ifeq ($(PLATFORM),wasm)` 包住。

**隔离方式**：
```makefile
ifeq ($(PLATFORM),wasm)
    CC := emcc
    CFLAGS += -sASYNCIFY=1 -pthread ...
    LDFLAGS += -sASYNCIFY=1 ...
    BUILD_DIR := build/wasm_$(MODE)
endif
```

native 路径完全走 `else` 或默认分支，flags 不变。

**native 不变性证明**：
- 默认 `make`（不带 `PLATFORM=wasm`）走的是 native 路径，命令行字面相同
- 输出目录隔离（`build/macos_release/` vs `build/wasm_release/`）

**验证**：在 mapgen 跑 `make`（默认 native），命令行 diff 改动前后一致。

### 3.3 mapgen Makefile wasm target

**位置**：`arc-mapgen/Makefile`

**操作**：在现有平台检测段加 wasm 分支。

**隔离方式**：
```makefile
ifeq ($(PLATFORM),wasm)
    CC = emcc
    CFLAGS += -DSOKOL_WGPU -pthread --use-port=emdawnwebgpu
    LDFLAGS += -pthread --use-port=emdawnwebgpu -sASYNCIFY=1 ...
    SHADER_SLANG = wgsl
    TARGET_EXT = .html
endif
```

**native 不变性证明**：
- 平台检测优先级：Windows → macOS → Linux → wasm（最后）
- macOS 用户跑 `make` 不会触发 wasm 分支
- 现有 macOS/Linux/Windows 分支字面不动

### 3.4 Sokol 后端 wasm 分支

**位置**：`arc-mapgen/external/arc-engine/src/bedrock/engine_main.c`（或 sokol 入口处）

**操作**：在已有的 `__APPLE__/__linux__/_WIN32` 分支后加 `__EMSCRIPTEN__`：

```c
#if defined(_MSC_VER) || defined(__MINGW32__)
    #define SOKOL_D3D11
#elif defined(__APPLE__)
    #define SOKOL_METAL
#elif defined(__EMSCRIPTEN__)
    #define SOKOL_WGPU
#elif defined(__linux__)
    #define SOKOL_GLCORE
#endif
```

**隔离方式**：分支顺序保证 native 平台先匹配，wasm 永远走最后。

**native 不变性证明**：
- macOS 用户的 `__APPLE__` 分支不变
- Linux 用户的 `__linux__` 分支不变
- 加了 wasm 分支不影响匹配顺序

### 3.5 jtask worker pthread

**位置**：jtask `external/ltask/src/thread.h`

**操作**：**0 改动**。

`pthread_create` 在 emcc `-pthread` 模式下自动转 Web Worker。这是 emcc 的事，不是我们的事。

### 3.6 Shader WGSL 编译路径

**位置**：mapgen Makefile 中 `SOKOL_SHDC` 调用

**操作**：wasm target 下 slang 设为 `wgsl`，其他平台不动。

**隔离方式**：现有 Makefile 已经按平台设 `SHADER_SLANG`（macOS=metal_macos, Windows=hlsl5, Linux=glsl430）。加一个 wasm 分支即可。

### 3.7 ASYNCIFY_IMPORTS 名单

**位置**：mapgen wasm target 下 LDFLAGS

**操作**：从最小集合开始，运行时遇到 unwound stack 错误就加。

**初版**：空名单 `-sASYNCIFY_IMPORTS=[]`。看 demo Phase 0.5 跑通时也是空名单——QuickJS + Asyncify 自动改写覆盖了所有需要的路径。

可能要加的：
- emcc 内置 `emscripten_futex_wait`（jtask 的 worker 通知）
- sokol_app 的 frame callback 入口（如果 frame_cb 内部触发 yield）

**调试期**用 `-sASYNCIFY_ADVISE` 让 emcc 输出建议名单。

### 3.8 不改的部分

| 项 | 状态 |
|---|---|
| `quickjs.c` | 0 改动 |
| `quickjs_stackful_mini.c` 主体 | 0 改动 |
| jtask src 主体（service.c, jtask.c, jtask_api.c） | 0 改动 |
| 业务 JS 代码 | 0 改动 |
| jtask binding（8 个 yield 点） | 0 改动 |
| Tina 主体汇编（aarch64/SysV_AMD64 等） | 0 改动（只新增 wasm 分支） |
| jtask worker pthread 模型 | 0 改动 |
| jtask 主线程 cond_wait | 0 改动 |
| native Makefile 分支 | 0 改动 |
| native sokol 后端分支 | 0 改动 |

---

## 四、工作量

每一步都包含 **改动 + native 隔离验证**。

| 步骤 | 估计 | native 验证 |
|---|---|---|
| 3.1 tina.h wasm 分支移植 | 半天 | `make arc` + `make test-auto` 一致 |
| 3.2 jtask Makefile wasm target | 半天 | jtask `make` 命令 diff 一致 |
| 3.3 mapgen Makefile wasm target | 1 天 | `make arc` 命令 diff 一致 |
| 3.4 Sokol 后端 wasm 分支 | 半天 | macOS Metal 路径完整测试 |
| 3.5 ASYNCIFY_IMPORTS 调试 | 3-5 天 | （只影响 wasm） |
| 3.6 emdawnwebgpu 端口集成 | 2-3 天 | （只影响 wasm） |
| 3.7 最小 wasm demo（一个 service yield/resume） | 2-3 天 | native 一致 |
| 3.8 jtask 全业务在 wasm 跑通 | 3-5 天 | native test-auto 完整通过 |
| 3.9 性能 benchmark + 二进制大小 | 1-2 天 | native baseline 不变 |

**Phase 1 总计**：3-5 周。

每一步完成后做 §0.3 的隔离验证清单。**任一步骤的 native 验证不过，立刻回退**。

---

## 五、风险与代价

### 5.1 Asyncify 已知代价（只影响 wasm）

- 二进制 +30-80%（mapgen wasm 估计 5-7MB）
- 跨 yield 路径性能 -30-50%
- 维护 ASYNCIFY_IMPORTS 名单

### 5.2 隔离失误的风险

| 风险 | 后果 | 缓解 |
|---|---|---|
| 改动逃出 `#ifdef` 块影响 native | native 业务出 bug 或性能下降 | §0.3 强制验证清单 |
| Makefile flag 漏 `ifeq` 包 | native 编译命令变 | 每次 commit 前 `make -n` diff |
| 结构体加 wasm 字段忘 `#ifdef` | native sizeof 变化 | code review 强制检查 |
| 全局符号污染 | 链接冲突或运行时行为差异 | wasm-only 符号必须 `static` |

### 5.3 Tina 作者明确不推荐 Asyncify

[Tina/README.md:25](../../external/Tina/README.md#L25)：
> WASM _explicitly_ forbids multiple stacks. Workarounds such as Asyncify are problematic. :(

作者审美偏好。我们项目接受这个代价。**Phase 0.5 已经实证可行**。

### 5.4 emcc 版本

锁定 emcc 5.0.6（Phase 0.5 验证版本）。每次升级 emcc 时跑 Phase 0.5 demo + Phase 1 全业务回归。

### 5.5 SharedArrayBuffer 部署要求

`-pthread` 需要部署服务器返回 COOP/COEP HTTP 头：
- `Cross-Origin-Opener-Policy: same-origin`
- `Cross-Origin-Embedder-Policy: require-corp`

**部署期约束**，不影响开发。

---

## 六、验证标准

### 6.1 Phase 1 完成标准

**Native 不变性**（最高优先级）：
- [ ] `make arc` 二进制和改动前一致
- [ ] `make test-auto` 输出和 baseline 一致
- [ ] native 帧性能在 baseline ±5% 内

**Wasm 功能**：
- [ ] tina.h wasm 分支移植到 submodule，编译通过
- [ ] jtask Makefile wasm target 编译通过
- [ ] mapgen Makefile wasm target 编译通过
- [ ] emdawnwebgpu 端口集成成功
- [ ] 最小 wasm demo（一个 service yield/resume）在 Chrome 跑通
- [ ] jtask 全业务在 wasm 跑通（地图生成、AI、ScaffoldProfiler）
- [ ] 性能/二进制数据已收集

**文档**：
- [ ] `tina.h wasm 分支说明`
- [ ] `ASYNCIFY_IMPORTS 名单维护指南`
- [ ] `wasm 部署指南`（COOP/COEP 头）

完成后进 Phase 2（JSPI）。

### 6.2 退路

| 触发 | 退路 |
|---|---|
| Native 验证不过 | 立刻回退该笔改动，重新设计隔离 |
| ASYNCIFY_IMPORTS 调不出来 | 用 `-sASYNCIFY_ADVISE` 拿建议名单 |
| 二进制超过 10MB | 评估 `-sASYNCIFY_ONLY` 限制改写范围 |
| 性能税不可接受 | 直接进 Phase 2 JSPI |
| WebGPU 端口集成困难 | 退到 sokol_gfx WebGL（`SOKOL_GLES3`）|

---

## 七、与 Phase 2 的关系

Phase 2（JSPI）改动**和 Phase 1 完全相同**：tina.h wasm 分支不动，emcc 编译时换 `-sJSPI=1` 替代 `-sASYNCIFY=1`。

双轨编译：Chrome/Edge 走 JSPI（性能接近原生），Firefox/Safari 走 Asyncify（兼容兜底）。

---

## 八、待审批

请确认：

1. **§零 隔离原则**作为 Phase 1 最高优先级
2. **§0.3 验证清单** 强制执行（每一步改动后 native 三项验证不过即回退）
3. **§三 改动清单** 每一项的隔离方式正确
4. **§六 完成标准** native 不变性是先决条件，wasm 功能是次要目标
5. 工作量 3-5 周，含 native 隔离验证开销

---

## 附录 A：Phase 0.5 验证过的 tina.h wasm 分支代码

完整代码在 `/Volumes/thunderbolt/works/11/tinademo/tina.h`，从 `#elif TINA_ABI_wasm` 到 `#else #error` 之间。

核心结构：
- `_tina_wasm_aux` 结构体：含 `emscripten_fiber_t` + `asyncify_stack`
- TLS 状态：`_tina_wasm_main_fiber`, `_tina_wasm_current_fiber`, `_tina_wasm_value`
- `_tina_wasm_ensure_current_fiber()`：第一次调用初始化主 fiber
- `_tina_wasm_fiber_entry()`：fiber entry，跑 `_tina_start`
- `_tina_init_stack()`：单独 malloc aux，emcc fiber_init，第一次 swap
- `_tina_swap()`：核心切换原语，**关键**：`*sp_from == NULL` 时补 current_fiber

移植步骤：
1. 在 `arc-mapgen/external/arc-engine/external/Tina/tina.h` 的 ABI 探测段加 `#define TINA_ABI_wasm (__EMSCRIPTEN__)`
2. 在内联汇编大段的末尾、`#else #error` 之前，插入 `#elif TINA_ABI_wasm` 分支
3. 跑 native build 验证：`make arc` `make test-auto` 输出和 baseline 一致

---

## 附录 B：典型隔离 PR 模板

每次 wasm 相关改动按这个模板提交：

```
[wasm] <一句话改动描述>

What:
  - 改了 X 文件,新增 Y 行 wasm 分支代码

Isolation:
  - #ifdef/#ifeq 隔离机制: <具体说明>
  - native 不进入这段代码: <预处理验证>

Native verification:
  - make arc: 二进制大小 X bytes (改前 X bytes,一致)
  - make test-auto: diff 通过 (附 diff 输出)
  - frame profile: avg X.XXms (baseline X.XXms,在 ±5% 内)

Wasm verification:
  - <如果这一步引入 wasm 功能,描述如何验证>
```
