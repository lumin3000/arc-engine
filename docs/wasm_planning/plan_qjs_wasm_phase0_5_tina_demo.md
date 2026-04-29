# Phase 0.5: QuickJS + Tina + emscripten_fiber 最小 wasm Demo

**目标**：在投入 Phase 1 之前，**最小代价直接验证核心风险**：QuickJS 在 wasm + Asyncify + emscripten_fiber 环境下能不能正常跑、native function 能不能正确 yield/resume 跨 QuickJS 解释器栈。

**前置**：Phase 0（清理废弃 quickjs_coroutine）已完成。

---

## 一、为什么直接做 qjs+tina demo

Phase 1 工期 4-6 周。最大的未知风险**不在 Tina API 对接**，而在：

> Asyncify 会改写 QuickJS 那 50000 行 C 代码，再叠加 Tina 切栈，会不会出问题？

只测纯 Tina + fiber 答不了这个问题。**真正能买到信心的是用 QuickJS 跑一遍**。

如果 QuickJS + Asyncify + fiber 这三方在一起出问题（比如 unwound stack 错误、二进制爆炸、跨 yield 行为不一致），**Phase 1 数周时间会浪费**。Phase 0.5 用 1 周买这个信心非常值。

**核心问题**直接命中：
- ✅ Tina 的 wasm 分支语义对吗？
- ✅ emscripten_fiber 能跨 QuickJS 解释器栈 yield 吗？
- ✅ Asyncify 改写 QuickJS 之后跑得动吗？
- ✅ 二进制大小 / 性能在哪个量级？

---

## 二、Demo 范围

### 2.1 最小代码量

新建测试目录 `external/Tina/wasm_demo/`：

```
wasm_demo/
├── qjs_tina_wasm_demo.c   # 主程序
├── Makefile               # native + wasm 双 target
├── shell.html             # wasm 加载页面
└── README.md              # 跑通流程
```

**单文件 C 程序**，~200 行：
- 内嵌 Tina（`#define TINA_IMPLEMENTATION`）
- 链接 QuickJS（直接编进同一个二进制）
- 在 Tina 协程里跑 QuickJS：JS 代码调一个 native binding，binding 内部 `tina_yield` 出去
- 主函数 dispatcher：resume 协程几次，每次给一个值
- 验证 JS 端拿到正确的值

### 2.2 业务场景（最小）

```js
// JS 代码 (字符串 eval)
const a = my_yield(0xAAAA);   // 调 native binding,binding 内部 tina_yield
print("got a=" + a.toString(16));

const b = my_yield(0xBBBB);
print("got b=" + b.toString(16));

const c = my_yield(0xCCCC);
print("got c=" + c.toString(16));
```

**协程 body**：
```c
static void coro_body(void* user_data, void* value) {
    JSContext* ctx = (JSContext*)user_data;
    // 把上面那段 JS 字符串 eval
    JS_Eval(ctx, JS_CODE, ..., JS_EVAL_TYPE_GLOBAL);
}
```

**native binding `my_yield`**：
```c
static JSValue js_my_yield(JSContext* ctx, JSValueConst this_val,
                           int argc, JSValueConst* argv) {
    int32_t input;
    JS_ToInt32(ctx, &input, argv[0]);
    printf("[binding] my_yield called with %x, yielding...\n", input);

    // 这里是核心:tina_yield 把控制权交给 dispatcher
    void* dispatcher_value = tina_yield(g_current_coro, (void*)(intptr_t)input);

    int32_t result = (int32_t)(intptr_t)dispatcher_value;
    printf("[binding] resumed, got %x\n", result);
    return JS_NewInt32(ctx, result);
}
```

**dispatcher (主函数)**：
```c
int main() {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    // 注册 my_yield + print binding
    register_bindings(ctx);

    // 创建 Tina 协程
    void* buf = malloc(1024 * 1024);
    tina* coro = tina_init(buf, 1024 * 1024, coro_body, ctx);

    // resume #1
    void* yielded = tina_resume(coro, NULL);  // 进入 body,跑到 my_yield(0xAAAA)
    printf("[main] coro yielded 0x%lx (expect AAAA)\n", (long)yielded);
    void* reply = (void*)(intptr_t)0x1111;

    // resume #2
    yielded = tina_resume(coro, reply);  // 把 0x1111 还给 my_yield 返回值
    printf("[main] coro yielded 0x%lx (expect BBBB)\n", (long)yielded);
    reply = (void*)(intptr_t)0x2222;

    // resume #3
    yielded = tina_resume(coro, reply);
    printf("[main] coro yielded 0x%lx (expect CCCC)\n", (long)yielded);
    reply = (void*)(intptr_t)0x3333;

    // resume #4 — 协程 body 结束
    yielded = tina_resume(coro, reply);
    printf("[main] coro completed=%d\n", coro->completed);

    // 期望 JS 端打印:
    //   got a=1111
    //   got b=2222
    //   got c=3333
}
```

**预期输出**（native + wasm 一致）：
```
[main] coro yielded 0xAAAA (expect AAAA)
[binding] my_yield called with aaaa, yielding...
[binding] resumed, got 1111
got a=1111
[binding] my_yield called with bbbb, yielding...
[main] coro yielded 0xBBBB (expect BBBB)
...
```

### 2.3 验证项

| 项 | 通过标准 |
|---|---|
| native 跑通 | 输出预期序列 |
| wasm 编译成功 | emcc 编译无错 |
| wasm 浏览器跑通 | Console 输出与 native 一致 |
| **QuickJS 解释器栈跨 yield 完整保留** | resume 后 JS 代码继续从 `my_yield()` 返回值后那一行执行 |
| **JS 局部变量保留** | `const a = ...; ... const b = ...` 中的 a 在 b 那行依然有值 |
| value 双向传递 | yield 出来的 0xAAAA 在主函数收到，主函数传 0x1111 在 JS 端收到 |
| 协程结束 | body return 后 coro->completed 为 true |
| **二进制大小记录** | 数字写在 README 里（用作 Phase 1 baseline） |

### 2.4 不验证的项

- 多协程并发
- 嵌套 resume
- 错误处理（throw / longjmp 与 fiber 交互）
- pthread + fiber
- 性能测量（只记录大小，不测速）
- jtask / sokol / mapgen

---

## 三、改动清单

### 3.1 tina.h 加 wasm 分支

**位置**：[external/Tina/tina.h](../../external/Tina/tina.h)

按 Phase 1 文档附录 A 的伪代码实现，重点解决三个 TODO：

1. **value 传递**：在 tina 结构体加 `_yielded_value` 字段（或用 thread-local），swap 前写、swap 后读
2. **初次 init 流程**：tina_init 期望 `_tina_init_stack` 让协程"启动并 yield 一次"。在 wasm 下用 fiber_swap 模拟
3. **对齐**：emscripten_fiber_t 和 asyncify_stack 16 字节对齐

```c
#define TINA_ABI_wasm (__EMSCRIPTEN__)

#elif TINA_ABI_wasm
    // 详细实现见 Phase 1 文档附录 A
#endif
```

**估计代码量**：~80 行。

### 3.2 demo 程序 qjs_tina_wasm_demo.c

~200 行。结构：

- 顶部 include + `#define TINA_IMPLEMENTATION`
- thread-local（或全局）的 `current_coro` 指针，binding 用它调 tina_yield
- coro_body 函数：`JS_Eval` JS 字符串
- `js_my_yield` binding
- `js_print` binding（简单 printf 包装）
- `main` 函数 dispatcher

### 3.3 Makefile

```makefile
QJS_DIR = ../../quickjs
QJS_SRCS = $(QJS_DIR)/quickjs.c $(QJS_DIR)/cutils.c \
           $(QJS_DIR)/libregexp.c $(QJS_DIR)/libunicode.c \
           $(QJS_DIR)/dtoa.c

CFLAGS = -std=gnu99 -O2 -g -I$(QJS_DIR) -I..

# native
native: qjs_tina_wasm_demo
	./qjs_tina_wasm_demo

qjs_tina_wasm_demo: qjs_tina_wasm_demo.c $(QJS_SRCS) ../tina.h
	clang $(CFLAGS) -o $@ qjs_tina_wasm_demo.c $(QJS_SRCS) -lm

# wasm
WASM_FLAGS = -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH

wasm: qjs_tina_wasm_demo.html

qjs_tina_wasm_demo.html: qjs_tina_wasm_demo.c $(QJS_SRCS) ../tina.h shell.html
	emcc $(CFLAGS) $(WASM_FLAGS) \
		--shell-file shell.html \
		-o $@ qjs_tina_wasm_demo.c $(QJS_SRCS)
	@echo "=== wasm size:"
	@ls -lh qjs_tina_wasm_demo.wasm

# 启动本地 server
serve: wasm
	python3 -m http.server 8000

clean:
	rm -f qjs_tina_wasm_demo qjs_tina_wasm_demo.html \
	      qjs_tina_wasm_demo.js qjs_tina_wasm_demo.wasm
```

### 3.4 shell.html

把 wasm console 输出显示到页面上的最小 HTML 入口。

### 3.5 README.md

记录：
- 跑通的 emcc 版本
- native + wasm 二进制大小（baseline）
- 如何在 Chrome 跑（python http.server + open localhost:8000）
- 已知问题和处置

---

## 四、不改的部分

- `external/Tina/tina.h` 主体（只新增 wasm 分支）
- `external/quickjs/` 任何文件
- jtask / mapgen / sokol / 业务代码

**Phase 0.5 是孤岛**。跑通后 tina.h 的 wasm 分支直接进 Phase 1 不重写，demo 程序本身是一次性产物。

---

## 五、工作量

| 任务 | 估计 |
|---|---|
| tina.h wasm 分支实现（解决 3 个 TODO） | 1-2 天 |
| demo 程序写 | 1 天 |
| native build 跑通 | 半天 |
| wasm build 跑通 + emcc 配置调试 | 2-3 天 |
| 浏览器测试（Chrome） | 半天 |
| 调试 buffer（unwound stack / 大小爆炸 / 行为不一致） | 1-2 天 |

**Phase 0.5 总计**：5-7 天。

---

## 六、可能踩的坑（重点）

### 6.1 emscripten_fiber_init 语义不对齐 Tina

emcc fiber: "创建 fiber，等 swap 进去时开始跑 entry"
Tina: "创建并立刻跑到第一个 yield"

**预案**：wasm 分支里手动跑一次 fiber_swap 让协程启动并 yield，模拟 Tina 初次 init。

### 6.2 value 中转

`emscripten_fiber_swap(from, to)` 没有 value 参数。

**预案**：tina 结构体加 `_yielded_value` 字段，或用 thread-local 变量中转。

### 6.3 ASYNCIFY_IMPORTS 名单

Demo 简单，可能不需要显式名单。如果遇到 `unwound stack` 错误，从最小集合开始迭代加：

```
-sASYNCIFY_IMPORTS=[]
```

或用 `-sASYNCIFY_ADVISE` 让 emcc 给建议。

### 6.4 asyncify_stack 大小

QuickJS 解释器调用栈深，16KB 可能不够。

**预案**：从 32KB 起，遇到 `RuntimeError: unreachable executed` 类错误就翻倍。

### 6.5 二进制大小爆炸

QuickJS + Asyncify 改写后预计 wasm 文件 1-3MB。**这是 Phase 0.5 重要数据点**，不算问题（Asyncify 已知代价）。

记录在 README，作为 Phase 1 接入 jtask/sokol 时的对照基准。

### 6.6 Asyncify 改写漏过 my_yield

如果 emcc 没识别出 `my_yield` 路径会触发 yield，运行时会崩。需要把 my_yield 加到 ASYNCIFY_IMPORTS 或用 `__attribute__((noinline))` + `EMSCRIPTEN_KEEPALIVE` 标注。

### 6.7 QuickJS 内部 inline 汇编 vs Asyncify

QuickJS 有少量内联汇编（atomic 操作等）。Asyncify 改写时可能出错。

**预案**：编译时关注 emcc 警告，如有问题禁用 QuickJS 的对应优化。

### 6.8 emcc 版本

记录测试通过的 emcc 版本到 README。

---

## 七、Phase 0.5 完成标准

- [ ] tina.h wasm 分支提交（仅新增，不修改现有 ABI 分支）
- [ ] native build 输出预期 JS 序列
- [ ] wasm build 输出预期 JS 序列（Chrome 验证）
- [ ] 输出对照 100% 一致
- [ ] **JS 端 const a 跨 yield 后在 const b 行仍有值**（最关键的核心验证）
- [ ] wasm 二进制大小记录到 README
- [ ] 测试通过的 emcc 版本记录到 README

---

## 八、Phase 0.5 失败的处置

| 失败模式 | 对策 | 后续 |
|---|---|---|
| emscripten_fiber 语义太特殊，Tina 包装很难写 | 评估直接重写 stackful_mini wasm 版（不通过 Tina），加 wasm 专用胶水 | 修订 Phase 1 路线 |
| QuickJS + Asyncify 编译失败 | 调 emcc 配置，禁用某些优化 | 修订 Phase 1 |
| 跑通但 JS 局部变量跨 yield 丢失 | **核心方案不成立**，需要重新评估（甚至放弃 wasm 上 stackful 协程） | 暂停项目重新讨论 |
| 二进制 >10MB | 记录数据但不阻塞 | Phase 1 评估 ASYNCIFY_ONLY 等限制选项 |
| 跑通但行为不一致 | debug | Phase 0.5 内部解决 |

---

## 九、验证后

Phase 0.5 通过 → 立即进 Phase 1：
- tina.h wasm 分支 → 直接成为 Phase 1 成果
- demo 程序扔掉
- 二进制大小 baseline → Phase 1 监控指标

Phase 0.5 卡死 → 暂停 Phase 1，按 §八 处置。

---

## 十、待审批

请确认：

1. 同意 Phase 0.5 范围（QuickJS + Tina + emscripten_fiber 三方在最小场景验证）
2. 同意工作量估计（5-7 天）
3. 同意 demo 跑通后 tina.h wasm 分支直进 Phase 1 不重写
4. 同意失败处置策略（特别是"JS 局部变量跨 yield 丢失"导致的暂停决议）
