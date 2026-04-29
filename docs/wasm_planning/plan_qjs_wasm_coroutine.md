# QuickJS WASM 协程支持改造计划

**目标**：让 jtask 在 wasm 下能跑 stackful 协程，业务 JS 代码和 jtask binding 一行不改。

**核心约束**：
- 不引入 Asyncify（性能税太重，二进制膨胀大）
- 不要求业务代码改 generator/async 风格
- native 平台行为零变化（用 `#ifdef` 隔离）
- jtask 接口零改动

---

## 一、问题根源

wasm 不允许保留 C 栈状态。Tina 切 C 栈这条路在 wasm 上物理无解。

QuickJS 普通 JS 函数把局部变量和值栈用 `alloca` 分配在 C 栈上（[quickjs.c:16331](../external/quickjs/quickjs.c#L16331)）。一旦 yield 时 C 栈被 unwind，这些数据就丢了，下次 resume 没法从中断点继续。

---

## 二、对齐 Lua 的设计

Lua 在 wasm 下能跑 stackful 协程，**不是因为 wasm 给了 Lua 特权**，而是因为 Lua VM 的设计本来就和 wasm 限制兼容：

1. **frame 全堆化**：Lua 的值栈（`lua_State->stack`）是预分配的 StackValue 数组，在堆上
2. **每协程独立 stack**：每个 coroutine 是一个 `lua_State`，有自己的独立 stack 数组和 ci 链
3. **yield 用 longjmp 出去**：[ldo.c:86 `LUAI_THROW = longjmp`](../../_refs/misc/deepfuture/soluna/3rd/lua/ldo.c#L86)。wasm 允许往外的 longjmp（编为 wasm exception throw）
4. **resume 重新进解释器**：[ldo.c:847 unroll](../../_refs/misc/deepfuture/soluna/3rd/lua/ldo.c#L847) 遍历 ci 链，从堆上的 PC 继续

QuickJS 的字节码状态（PC、JSStackFrame 链）**已经在堆上**，唯独值栈还在 alloca。**只要把 alloca 改成"从预分配堆栈数组取空间"，QuickJS 就具备和 Lua 同样的 stackless 协程能力**。

切换 C 协程的工作交给嵌入的 Lua VM 完成（lua_resume / lua_yield）—— Lua 已经替我们解决了"yield 时 longjmp 出去 / resume 时重建 C 栈"这件事，包括 wasm 上跑通过的边界处理。

---

## 三、整体架构

```
JS 业务代码 (不改)
  ↓
jtask binding (不改)
  ↓
stackful_* API (接口不变,实现按平台分)
  ↓
[native]                     [wasm]
quickjs_stackful_mini.c   quickjs_stackful_lua.c
(Tina 后端,现有)            (Lua VM 后端,新建)
                              ↓
                          嵌入的 Lua VM (协程引擎)
                              ↓
                          Lua longjmp 炸 C 栈
                              ↓
                          QuickJS frame 不丢
                          (因为 alloca → 堆栈化)
```

---

## 四、QuickJS 改造（必要,用 #ifdef QJS_WASM_COROUTINE 隔离）

### 4.1 设计原则

**关键：每个协程独立 call_stack（对齐 Lua 每协程独立 lua_State->stack）**

如果所有协程共享一个 ctx 上的 call_stack，协程 A yield 后 B 跑会覆盖 A 的 frame 数据。必须每协程一份独立 stack buffer。

JSContext 上持有 **指向当前活跃协程 stack 的字段**（运行时切换）：

```c
struct JSContext {
    // ... 原有字段 ...
#ifdef QJS_WASM_COROUTINE
    JSValue* call_stack_base;  // 指向当前活跃协程的 stack base
    JSValue* call_stack_top;   // 当前 top
    JSValue* call_stack_end;   // 当前 end
    JSValue* default_stack_base;  // 主线程默认 stack(没有活跃协程时用)
    JSValue* default_stack_end;
#endif
};
```

主线程默认 stack：QuickJS 启动时分配（对齐 Lua 的 main thread）。

进协程时切换为协程的 stack；协程 yield 或结束时切回默认 stack。

### 4.2 改动清单

**所有改动用 `#ifdef QJS_WASM_COROUTINE` 包裹。Native 编译路径字节级不变。**

| # | 文件 | 位置 | 改动 | 行数 |
|---|---|---|---|---|
| 1 | quickjs.h | JSContext 结构 | 加 5 个字段（base/top/end + default_base/default_end） | +5 |
| 2 | quickjs.c | JS_NewContext | 分配 default_stack（64K JSValue = 1MB），设 call_stack_* 指向它 | +10 |
| 3 | quickjs.c | JS_FreeContext | 释放 default_stack | +3 |
| 4 | quickjs.c | [5456 alloca](../external/quickjs/quickjs.c#L5456) | 替换为从 call_stack_top 取空间 + 出口恢复 | ~10 |
| 5 | quickjs.c | [16067 alloca](../external/quickjs/quickjs.c#L16067) | 同上 | ~10 |
| 6 | quickjs.c | [16178 alloca](../external/quickjs/quickjs.c#L16178) | 同上 | ~10 |
| 7 | quickjs.c | [16331 alloca](../external/quickjs/quickjs.c#L16331) | 同上（核心字节码函数路径） | ~20 |
| 8 | quickjs.c | 文件末尾 | grow_call_stack 函数（学 Lua correctstack，遍历 sf 链修正指针） | ~40 |
| 9 | quickjs.c | 文件末尾 | JS_CallInternalWithFrame 函数（暴露 generator restart 路径给外部） | ~30 |
| 10 | quickjs.c | 文件末尾 | JS_SwitchCallStack 函数（切换活跃 call_stack） | ~15 |
| 11 | quickjs.h | 文件末尾 | JS_CallInternalWithFrame / JS_SwitchCallStack 声明 | +4 |

**总计**：~150 行新增 + 4 处 alloca 替换。

### 4.3 核心改动详细

**alloca 替换模板**（4 处通用）：

```c
#ifdef QJS_WASM_COROUTINE
    /* 从当前活跃 call_stack 取空间 */
    local_buf = ctx->call_stack_top;
    ctx->call_stack_top += alloca_size_in_jsvalues;
    if (unlikely(ctx->call_stack_top > ctx->call_stack_end)) {
        if (grow_call_stack(ctx, alloca_size_in_jsvalues) < 0)
            return JS_ThrowStackOverflow(ctx);
        local_buf = ctx->call_stack_top - alloca_size_in_jsvalues;
    }
#else
    local_buf = alloca(alloca_size);
#endif
```

**函数返回路径**（恢复 top）：

```c
#ifdef QJS_WASM_COROUTINE
    ctx->call_stack_top = local_buf;
#endif
```

**grow_call_stack 函数**（学 Lua [correctstack](../../_refs/misc/deepfuture/soluna/3rd/lua/ldo.c)）：

```c
#ifdef QJS_WASM_COROUTINE
static int grow_call_stack(JSContext *ctx, size_t need) {
    size_t old_size = ctx->call_stack_end - ctx->call_stack_base;
    size_t new_size = old_size * 2;
    while (new_size < old_size + need) new_size *= 2;
    
    JSValue *new_base = js_realloc(ctx, ctx->call_stack_base, 
                                    new_size * sizeof(JSValue));
    if (!new_base) return -1;
    
    ptrdiff_t offset = new_base - ctx->call_stack_base;
    if (offset != 0) {
        /* 修正所有 sf 链上的 var_buf/arg_buf 指针 */
        JSStackFrame *sf = ctx->rt->current_stack_frame;
        while (sf) {
            if (sf->var_buf >= ctx->call_stack_base && 
                sf->var_buf < ctx->call_stack_end) {
                sf->var_buf += offset;
                sf->arg_buf += offset;
                if (sf->cur_sp) sf->cur_sp += offset;
            }
            sf = sf->prev_frame;
        }
    }
    
    ctx->call_stack_base = new_base;
    ctx->call_stack_top = new_base + (ctx->call_stack_top - 
                                       (new_base - offset));
    ctx->call_stack_end = new_base + new_size;
    return 0;
}
#endif
```

**JS_SwitchCallStack 函数**（协程切换时调用）：

```c
#ifdef QJS_WASM_COROUTINE
void JS_SwitchCallStack(JSContext *ctx, 
                       JSValue *base, JSValue *top, JSValue *end) {
    ctx->call_stack_base = base;
    ctx->call_stack_top = top;
    ctx->call_stack_end = end;
}
#endif
```

**JS_CallInternalWithFrame 函数**（重入解释器，从 sf->cur_pc 继续）：

抽取 [quickjs.c:16275-16295 generator restart 路径](../external/quickjs/quickjs.c#L16275) 为独立函数。本质上是：
- 把 sf 设为 current_stack_frame
- 跳到解释器的 restart 标签
- 不需要改解释器主循环

---

## 五、嵌入 Lua VM

**目的**：作为协程引擎使用，不跑业务代码。

**裁剪后的 Lua**：只保留协程切换必需的核心模块。

| 模块 | 保留 | 用途 |
|---|---|---|
| lstate | ✅ | lua_State 创建/销毁 |
| ldo | ✅ | longjmp 协程切换核心 |
| lvm | ✅ | luaV_execute 解释器循环 |
| lcorolib | ✅ | coroutine.create/resume/yield |
| lapi | ✅ | C API |
| lmem | ✅ | 内存分配 |
| lobject / lstring | ✅ | 基础对象（lua_State 创建依赖） |
| lgc | ✅ | GC（lua_State 依赖） |
| 标准库 (lbaselib/lmathlib/lstrlib/...) | ❌ | 不需要 |
| ltable | ⚠️ | 看 lua_newthread 是否依赖 |
| lparser/lcode/llex | ❌ | 不解析 lua 源码 |
| lopcodes/ldebug | ⚠️ | luaV_execute 可能依赖 |

**估计代码量**：~5000–8000 行。需要实测裁剪。

**位置**：`external/quickjs/lua_coro/`（作为 quickjs 改造的附属物，不污染其他地方）。

**Makefile**：只在 wasm target 下编译。

---

## 六、quickjs_stackful_lua.c（新建,wasm 替换 stackful_mini）

**目的**：实现和 quickjs_stackful_mini.h 完全一致的 API，底层用 Lua VM。

### 6.1 数据结构

```c
struct stackful_coroutine_lua {
    /* 每协程独占的 call_stack（对齐 Lua 设计） */
    JSValue *call_stack_base;
    JSValue *call_stack_top;
    JSValue *call_stack_end;
    
    /* Lua 协程载体 */
    lua_State *lua_co;
    int lua_ref;          /* luaL_ref 防 GC */
    
    /* 关联的 QuickJS context */
    JSContext *js_ctx;
    
    /* 用户 body 和数据 */
    stackful_func body;
    void *user_data;
    
    /* yield/resume 数据传递 */
    void *yielded_value;
    void *resume_value;
    
    /* 状态 */
    stackful_status_t status;
    int self_id;
};

struct stackful_schedule {
    JSRuntime *rt;
    JSContext *main_ctx;
    
    /* 全局 lua_State（所有 lua_co 都从这个派生） */
    lua_State *lua_main;
    
    /* 协程表 */
    struct stackful_coroutine_lua **coroutines;
    int cap;
    int count;
    
    /* 数据存储（保持和 mini 一致） */
    tina_storage *storages;
};
```

### 6.2 关键 API 实现

**stackful_open**：
```c
stackful_schedule* stackful_open(JSRuntime *rt, JSContext *main_ctx) {
    stackful_schedule *S = malloc(sizeof(*S));
    S->rt = rt;
    S->main_ctx = main_ctx;
    S->lua_main = luaL_newstate();
    /* ... 初始化数组 ... */
    return S;
}
```

**stackful_new**（创建协程）：
```c
int stackful_new(stackful_schedule *S, stackful_func func, void *ud) {
    int id = find_or_alloc_slot(S);
    coro *co = malloc(sizeof(*co));
    
    /* 分配独立 call_stack */
    co->call_stack_base = malloc(STACKFUL_STACK_SIZE * sizeof(JSValue));
    co->call_stack_top = co->call_stack_base;
    co->call_stack_end = co->call_stack_base + STACKFUL_STACK_SIZE;
    
    /* 创建 lua 协程 */
    co->lua_co = lua_newthread(S->lua_main);
    co->lua_ref = luaL_ref(S->lua_main, LUA_REGISTRYINDEX);
    
    co->body = func;
    co->user_data = ud;
    co->js_ctx = S->main_ctx;
    co->status = STACKFUL_STATUS_SUSPENDED;
    co->self_id = id;
    
    /* 在 lua 协程上注册 entry function */
    lua_pushlightuserdata(co->lua_co, co);
    lua_pushcclosure(co->lua_co, lua_coro_entry, 1);
    
    S->coroutines[id] = co;
    return id;
}

/* lua 这边的协程入口 */
static int lua_coro_entry(lua_State *L) {
    coro *co = lua_touserdata(L, lua_upvalueindex(1));
    co->body(co->user_data, co->resume_value);
    return 0;
}
```

**stackful_resume_with_value**：
```c
void* stackful_resume_with_value(stackful_schedule *S, int id, void *value) {
    coro *co = S->coroutines[id];
    co->resume_value = value;
    
    /* 切换 ctx 的 call_stack 到这个协程的 */
    JSValue *saved_base = co->js_ctx->call_stack_base;
    JSValue *saved_top = co->js_ctx->call_stack_top;
    JSValue *saved_end = co->js_ctx->call_stack_end;
    
    JS_SwitchCallStack(co->js_ctx, 
                      co->call_stack_base,
                      co->call_stack_top,
                      co->call_stack_end);
    
    /* 调 lua_resume —— lua VM 内部 setjmp + 跑 entry */
    int nresults = 0;
    int status = lua_resume(co->lua_co, NULL, 0, &nresults);
    
    /* 取出协程更新后的 call_stack_top（可能 grow 过） */
    co->call_stack_base = co->js_ctx->call_stack_base;
    co->call_stack_top = co->js_ctx->call_stack_top;
    co->call_stack_end = co->js_ctx->call_stack_end;
    
    /* 恢复 ctx 的 default call_stack */
    JS_SwitchCallStack(co->js_ctx, saved_base, saved_top, saved_end);
    
    if (status == LUA_YIELD) {
        co->status = STACKFUL_STATUS_SUSPENDED;
        return co->yielded_value;
    } else if (status == LUA_OK) {
        co->status = STACKFUL_STATUS_DEAD;
        cleanup(co);
        return NULL;
    } else {
        /* 错误 */
        return NULL;
    }
}
```

**stackful_yield_with_value**：
```c
void* stackful_yield_with_value(stackful_schedule *S, void *value) {
    coro *co = current_coroutine();
    co->yielded_value = value;
    
    /* lua_yield —— LUAI_THROW → longjmp 出去
       C 栈炸光,但 ctx->call_stack 在堆上不丢 */
    lua_yield(co->lua_co, 0);
    
    /* 恢复后从这里继续 */
    return co->resume_value;
}
```

### 6.3 关键设计点

**为什么 yield 时 QuickJS frame 不丢**：

```
yield 时 C 栈:
  stackful_resume_with_value
    → lua_resume → luaV_execute → lua_coro_entry
      → co->body (user)
        → JS_Call → JS_CallInternal
          → js_jtask_yield (native binding)
            → stackful_yield_with_value
              → lua_yield → longjmp
              ↓ 整条 C 栈炸光

但是:
  - JSStackFrame 链在堆上(quickjs 自带)
  - JSStackFrame::var_buf 指向堆化的 call_stack(改造后)
  - sf->cur_pc 在 sf 上(quickjs 自带)
  → 所有 JS 状态完整保留
```

**resume 时如何继续**：

```
stackful_resume_with_value 再次被调:
  → lua_resume(co->lua_co, ...)
    → lua VM 重新 setjmp + 进 luaV_execute
      → 从 lua_yield 那一行继续(lua VM 自动)
        → stackful_yield_with_value 返回 co->resume_value
          → js_jtask_yield 返回值给字节码
            → JS_CallInternal 从 sf->cur_pc 继续跑下一条指令
              → 业务 JS 代码继续
```

注意：**resume 不需要我们手动 unroll**。lua VM 内部会处理 setjmp、重入解释器。我们只需要保证 QuickJS 的状态在 longjmp 时不丢（通过堆栈化）。

---

## 七、Makefile 改动

```makefile
# jtask Makefile

ifeq ($(PLATFORM), wasm)
    CFLAGS += -DQJS_WASM_COROUTINE
    QUICKJS_SRCS += $(QUICKJS_DIR)/quickjs_stackful_lua.c
    QUICKJS_SRCS += $(LUA_DIR)/lstate.c $(LUA_DIR)/ldo.c \
                    $(LUA_DIR)/lvm.c $(LUA_DIR)/lcorolib.c \
                    $(LUA_DIR)/lapi.c $(LUA_DIR)/lmem.c \
                    # ... lua 核心模块
else
    QUICKJS_SRCS += $(QUICKJS_DIR)/quickjs_stackful_mini.c
endif

# quickjs.c 在两边都用相同源码,只是 wasm 编译时 -DQJS_WASM_COROUTINE 启用堆栈化分支
QUICKJS_SRCS += $(QUICKJS_DIR)/quickjs.c
```

---

## 八、jtask 改动

**0 行**。

jtask_api.c 继续 include `quickjs_stackful_mini.h`，继续调 `stackful_*` 函数。底下是 Tina 还是 lua 它不知道。

---

## 九、业务代码改动

**0 行**。

JS 业务代码继续写同步风格 `let x = jtask.recv(); use(x)`。

---

## 十、性能预估

| 场景 | 影响 |
|---|---|
| Native 编译路径 | **0%**（`#ifdef` 隔离，连编译都不进） |
| Wasm 字节码密集 JS（不调函数） | **0%** |
| Wasm 函数调用密集 | **+1-3%**（alloca 改成堆栈推进，多 1 次内存读写） |
| Wasm 协程 yield/resume | 比 Tina native 慢 ~30%（多一层 lua VM）<br>比 Asyncify 快数倍 |
| 二进制大小（wasm） | +200KB（嵌入 lua 核心） |

对比：
- Asyncify：wasm 全局 +30-50% 性能税，二进制 +30-80%
- 本方案：wasm 热路径 +1-3%，二进制 +200KB

---

## 十一、风险评估

| 风险 | 等级 | 缓解 |
|---|---|---|
| grow_call_stack 时指针修正出错 | 高 | 初始 stack 给足（1MB），先实现"禁止 grow"版本，第二阶段做 grow |
| Lua VM 嵌入后体积超预期 | 中 | 实测裁剪后大小，必要时进一步裁剪 |
| QuickJS test262 回归 | 中 | Phase 1 必须跑过 |
| `JS_CallInternalWithFrame` 抽取出错 | 中 | 对照 generator 路径实现，generator 测试用例验证 |
| Lua VM 在 wasm 上的边界 case | 低 | ltask 已经验证可用 |
| native 二进制变化 | 0 | `#ifdef` 隔离 |

---

## 十二、实施分期

### Phase 1：QuickJS 堆栈化改造（1-2 周）

- 在 quickjs.c 加 `#ifdef QJS_WASM_COROUTINE` 分支，4 处 alloca 替换
- 实现 grow_call_stack（先做"禁止 grow"版本，stack 用满直接 throw）
- 实现 JS_CallInternalWithFrame
- 实现 JS_SwitchCallStack
- **不引入 lua VM，不改协程**
- 跑 quickjs test262（wasm 编译版），验证语义正确
- benchmark 性能影响

**通过标准**：test262 全过 + 性能税 <5%。

### Phase 2：Lua VM 嵌入（1 周）

- 裁剪 lua 核心模块到 `external/quickjs/lua_coro/`
- 验证嵌入后能跑（最小 demo：lua_newstate + 跑一个 lua 协程 yield/resume）
- 确认 wasm 编译能过

**通过标准**：lua VM 在 wasm 下 yield/resume 正常。

### Phase 3：quickjs_stackful_lua.c 实现（1 周）

- 写完全部 stackful_* API
- native 上先验证（虽然 native 不会用这份，但能跑通是基础信心）
- 写最小 demo：JS 业务调 native function → native 调 stackful_yield → 跨 yield 恢复值正确

**通过标准**：最小 demo 能跑通。

### Phase 4：jtask wasm 集成测试（1-2 周）

- jtask Makefile 加 wasm target
- emcc 编译，浏览器跑 jtask
- 跑 jtask 全套现有测试
- 协程切换 RTT benchmark

**通过标准**：jtask 业务行为和 native 一致。

**总工期**：4-6 周。

---

## 十三、可放弃的退路

| 触发条件 | 退路 |
|---|---|
| Phase 1 test262 大量回归 | 退到 Asyncify 方案 |
| Phase 2 lua 裁剪失败/体积超大 | 评估是否换其他 stackless 协程库 |
| Phase 4 性能不可接受 | 评估 JSPI（用 wasm 原生 fiber） |

---

## 十四、文件清单

新增：
- `external/quickjs/quickjs_stackful_lua.c` (~400 行)
- `external/quickjs/lua_coro/` (lua 核心 ~5000-8000 行)
- `docs/plan_qjs_wasm_coroutine.md` (本文档)

修改：
- `external/quickjs/quickjs.c` (~150 行新增，全部 `#ifdef` 隔离)
- `external/quickjs/quickjs.h` (~10 行新增字段和声明)
- `external/jtask/Makefile` (wasm target)

不动：
- 所有 jtask 源码（jtask_api.c / service.c / ...）
- 所有业务 JS 代码
- `quickjs_stackful_mini.c`（native 继续用 Tina）
- `quickjs_stackful_mini.h`（接口完全不变）

---

## 十五、待审批

请审批：

1. **总体方向**：用 lua VM 当协程引擎 + quickjs frame 堆栈化（每协程独立 stack）
2. **改造范围**：quickjs.c ~150 行新增（`#ifdef` 隔离）+ 嵌入 lua VM + 新建 stackful_lua.c
3. **接口契约**：jtask 0 改动，业务 0 改动
4. **Phase 1 优先**：先做 quickjs 堆栈化改造 + test262 验证，再做后续
5. **退路条件**：Phase 1 失败退到 Asyncify

如果有约束我没考虑到的，请指出。
