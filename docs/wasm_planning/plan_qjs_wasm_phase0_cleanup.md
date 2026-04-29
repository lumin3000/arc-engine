# Phase 0：清理废弃的 quickjs_coroutine 代码

**前置任务**。在 wasm 协程改造之前，先把已确认死代码清掉。

---

## 一、为什么先做这个

`quickjs_coroutine.c` 是早期 Generator 路线的尝试，被 Tina 的 stackful 路线取代后**没清理**。验证结论：

- `JS_NewCoroutineManager` / `JS_EnableCoroutines` / `JS_FreeCoroutineManager` 被调用（创建+注册+销毁）
- 但它注册的所有协程 API（`JS_CoroutineWait` / `JS_CoroutineResume` / `JS_HandleYieldValue` / `JS_ServiceExecute` / `JS_CallGeneratorNext` / `JS_IsGenerator`）—— **0 处真实使用**
- 它注册的全局函数 `__coroutine_wait` / `__coroutine_resume` / `__coroutine_session` / `__is_generator` —— **整个 jtask + arc-mapgen 仓库 0 处调用**

它就是一个被遗留的空壳。占字段、初始化时 malloc、析构时 free，中间什么都不做。

**保留它的代价**：
- 后面引入 wasm 协程改造会在 quickjs 里加不少东西，留着这堆废代码会让人困惑两套机制并存
- 维护时容易踩坑（看到 JSCoroutineManager 以为有用）
- 编译多一份对象文件 + 多一份链接

清掉之后，`stackful_*`（Tina/Lua 后端）成为唯一的协程层，干净。

---

## 二、清理范围

### 2.1 jtask 项目（`external/jtask/`）

**需要修改的文件**：

#### `src/service.c`

删除字段：
- [service.c:50](../external/jtask/src/service.c#L50) `JSCoroutineManager *coroutine_mgr;` — struct service 字段

删除引用：
- [service.c:132-133](../external/jtask/src/service.c#L132) `if (S->coroutine_mgr != NULL) JS_FreeCoroutineManager(...)` — 析构
- [service.c:340](../external/jtask/src/service.c#L340) `s->coroutine_mgr = NULL;` — 初始化
- [service.c:484-495](../external/jtask/src/service.c#L484) 创建 + 启用块（含错误处理）
- [service.c:500-503, 522-525, 535-538](../external/jtask/src/service.c#L500) 错误清理路径
- [service.c:696-698](../external/jtask/src/service.c#L696) 析构路径

删除 include：
- 顶部 `#include "quickjs_coroutine.h"`（如果有）

#### `src/main.c`

删除引用：
- [main.c:225](../external/jtask/src/main.c#L225) `JSCoroutineManager *coroutine_mgr = NULL;`
- [main.c:259-261](../external/jtask/src/main.c#L259) 创建 + 启用块
- [main.c:331-332](../external/jtask/src/main.c#L331) 析构

删除 include：
- 顶部 `#include "quickjs_coroutine.h"`（如果有）

#### `Makefile`

删除编译条目：
- [Makefile:113](../external/jtask/Makefile#L113) `$(QUICKJS_DIR)/quickjs_coroutine.c \`

### 2.2 quickjs 项目（`external/quickjs/`）

**需要删除的文件**：

| 文件 | 说明 |
|---|---|
| `quickjs_coroutine.c` | 414 行，废弃的 Generator 调度器 |
| `quickjs_coroutine.h` | 对应头文件 |
| `Makefile.coroutine` | 单独构建脚本，仅为废代码服务 |
| `build_coroutine.sh` | 单独构建脚本 |
| `README_COROUTINE.md` | 废代码文档 |
| `COROUTINE_INTEGRATION_STATUS.md` | 废代码集成文档 |

**README.md 修改**：
- [README.md:39-40, 165-205](../external/quickjs/README.md) 删除 Generator 协程相关章节

**保留**：
- `quickjs_stackful.c` / `quickjs_stackful.h`（ucontext 版本，虽然 jtask 不用，但保留作为参考）
- `quickjs_stackful_mini.c` / `quickjs_stackful_mini.h`（**当前 jtask 实际用的 Tina 后端**）
- `STACKFUL_README.md` / `STACKFUL_MAGIC.md` / `TINA_MIGRATION_PLAN.md` / `YIELD_RESUME_DATA.md`（stackful 路线的文档）
- `tina.h` / `test_tina_*.c`

---

## 三、操作清单

### Step 1：jtask 改动（src/service.c, src/main.c, Makefile）

**目标**：所有 `coroutine_mgr` 字段、`JS_NewCoroutineManager` / `JS_EnableCoroutines` / `JS_FreeCoroutineManager` 调用全部删除。

需要确保：
- struct service 字段移除后，service 创建/销毁路径仍然完整
- main.c 的 ctx 初始化流程仍然完整
- Makefile 不再尝试编译 quickjs_coroutine.c

### Step 2：quickjs 改动（删文件 + 改 README）

```
rm external/quickjs/quickjs_coroutine.c
rm external/quickjs/quickjs_coroutine.h
rm external/quickjs/Makefile.coroutine
rm external/quickjs/build_coroutine.sh
rm external/quickjs/README_COROUTINE.md
rm external/quickjs/COROUTINE_INTEGRATION_STATUS.md
```

修改 `README.md` 删除 Generator 协程章节。

### Step 3：构建验证

```bash
cd arc-engine/external/jtask
make clean
make            # native 构建必须成功
```

预期：
- 编译通过
- 链接通过（确认没有未定义符号 `JS_NewCoroutineManager` 等）

### Step 4：运行验证

```bash
cd arc-mapgen
make arc        # 启动正式游戏,确认 jtask 业务正常
```

预期：
- 主菜单显示
- 选场景能进入
- 协程相关功能（service 创建、消息收发）正常工作

### Step 5：commit

按 jtask / quickjs 两个项目分别 commit。

---

## 四、风险评估

| 风险 | 等级 | 说明 |
|---|---|---|
| 漏改某处导致编译失败 | 低 | 编译器立即报错，容易定位 |
| 漏改某处导致运行时错误 | **极低** | 这些 API 0 处真实调用，不会运行时触发 |
| 误删了真在用的代码 | 低 | 已经全仓 grep 验证 0 引用 |
| 影响 native 业务 | 极低 | 删除的是死代码，行为应当一致 |

---

## 五、可放弃的退路

如果 Step 3 / Step 4 失败：
- 如果是漏改的小问题，直接 fix
- 如果发现某个看似废弃的 API 其实有间接调用（不太可能），revert + 重新分析

---

## 六、Done 标准

- [ ] jtask src/service.c 不再有 coroutine_mgr 字段和相关调用
- [ ] jtask src/main.c 不再有 coroutine_mgr 相关调用
- [ ] jtask Makefile 不再编译 quickjs_coroutine.c
- [ ] quickjs_coroutine.c / .h 文件已删除
- [ ] quickjs 仓库内的协程相关辅助脚本和废文档已删除
- [ ] `make arc` 在 arc-mapgen 下正常运行游戏
- [ ] 两个项目分别 commit

---

## 七、完成后

进入 Phase 1：QuickJS 堆栈化改造（见 `plan_qjs_wasm_coroutine.md`）。

---

## 八、等待审批

- 同意清理范围（删 quickjs_coroutine.c/.h + 5 个相关文件 + 修两个 jtask 源文件 + 改一行 Makefile）
- 同意保留 quickjs_stackful.c（ucontext 版本，虽然 jtask 不用）
- 同意完成后立即 commit（按项目分两次 commit）
