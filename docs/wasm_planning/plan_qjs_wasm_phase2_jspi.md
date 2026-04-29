# Phase 2: JSPI 接入 — wasm 协程性能优化

**目标**：在 Phase 1（Asyncify）跑通之后，给现代浏览器用户切换到 JSPI 路径，获得接近原生的性能。

**前置**：Phase 1 完成。

---

## 零、范围说明

Phase 2 **只针对协程层后端切换**（Asyncify → JSPI）。

**Phase 1 沉淀下来的所有东西不变**：
- sokol_gfx WebGPU 后端（`SOKOL_WGPU` + `--use-port=emdawnwebgpu`）
- jtask worker 的 wasm pthread 模型
- 主线程 cond_wait（emcc 自动用 Atomics.wait）
- mapgen WGSL shader 编译路径
- 业务 / jtask binding / QuickJS / Tina 主体 全部 0 改动

Phase 2 **唯一的事**：让协程那一层在支持 JSPI 的浏览器上走更快的路径。

---

## 一、JSPI 是什么

JSPI = **JavaScript Promise Integration**，wasm 标准提案。

机制：让 wasm 函数能直接和 JS Promise 互通。wasm 函数挂起时返回 Promise，JS 端 await 它，resolve 时 wasm 从挂起点继续。

**这是 wasm runtime 原生支持的协程能力**（不是编译器改写代码），所以代价比 Asyncify 小得多。

---

## 二、JSPI vs Asyncify 对比

| 维度 | Asyncify (Phase 1) | JSPI (Phase 2) |
|---|---|---|
| 二进制大小 | +30%-80% | 几乎无增加 |
| 运行时性能 | -30%-50% 跨 yield 路径 | 接近原生 |
| 浏览器兼容 | 所有 wasm 浏览器 | Chrome/Edge 132+ 默认开启<br>Firefox 实验性<br>Safari 未支持 |
| emcc 选项 | `-sASYNCIFY=1` | `-sJSPI=1` |
| 我们的 tina.h 改动 | 已在 Phase 1 完成 | **0 改动**（同一份代码） |

**关键事实**：emcc 的 `emscripten_fiber_*` API 在 Asyncify 和 JSPI 后端下接口完全一致。我们 Phase 1 写的 tina.h wasm 分支**直接复用**，不需要改。

---

## 三、改动清单

### 3.1 tina.h

**0 改动**。Phase 1 写的 wasm 分支同时支持两种后端。

### 3.2 jtask Makefile

加一个 wasm-jspi target：

```makefile
ifeq ($(PLATFORM),wasm)
    ifeq ($(WASM_BACKEND),jspi)
        CFLAGS += -sJSPI=1
        LDFLAGS += -sJSPI=1
    else
        CFLAGS += -sASYNCIFY=1
        LDFLAGS += -sASYNCIFY=1
        LDFLAGS += -sASYNCIFY_IMPORTS=...  # Phase 1 维护的名单
    endif
    # 以下为两种后端共用(都从 Phase 1 沉淀):
    CFLAGS += -pthread
    LDFLAGS += -pthread -sUSE_PTHREADS=1
    LDFLAGS += -sPTHREAD_POOL_SIZE='Math.max(2,navigator.hardwareConcurrency)'
    LDFLAGS += -sALLOW_MEMORY_GROWTH
endif
```

### 3.3 mapgen Makefile

类似 3.2。两个 target 都用同样的 sokol/WebGPU/pthread 配置（Phase 1 沉淀），只是加 `WASM_BACKEND=jspi` 时换协程后端。

### 3.4 双产物构建脚本

```bash
make wasm WASM_BACKEND=asyncify  # 输出 mapgen.asyncify.wasm + .js
make wasm WASM_BACKEND=jspi      # 输出 mapgen.jspi.wasm + .js
```

### 3.5 HTML 入口：浏览器特性检测

```html
<script>
async function pickWasmBackend() {
    // JSPI 特性检测
    if (typeof WebAssembly.Suspending === 'function') {
        return 'jspi';
    }
    return 'asyncify';
}

pickWasmBackend().then(backend => {
    const script = document.createElement('script');
    script.src = `mapgen.${backend}.js`;
    document.head.appendChild(script);
});
</script>
```

### 3.6 ASYNCIFY_IMPORTS

JSPI target 不需要这个名单。Phase 2 下 jspi target 的编译路径不传 `-sASYNCIFY_IMPORTS`，asyncify target 继续用 Phase 1 维护的名单。

---

## 四、不改的部分

| 项 | 状态 |
|---|---|
| `tina.h` wasm 分支 | 0 改动（Phase 1 的代码直接用） |
| `quickjs.c` | 0 改动 |
| `quickjs_stackful_mini.c` | 0 改动 |
| jtask src 主体 | 0 改动 |
| 业务 JS | 0 改动 |
| sokol/WebGPU 集成 | 0 改动（Phase 1 沉淀） |
| jtask worker pthread 模型 | 0 改动（Phase 1 沉淀） |
| WGSL shader 编译 | 0 改动（Phase 1 沉淀） |

---

## 五、工作量

| 任务 | 估计 |
|---|---|
| jtask Makefile 加 wasm-jspi target | 1-2 天 |
| mapgen Makefile 同步 | 1 天 |
| 双产物构建脚本 | 1 天 |
| HTML 入口浏览器特性检测 | 1 天 |
| Chrome 上跑 JSPI 版本验证 | 2-3 天 |
| Firefox/Safari 上跑 Asyncify 版本验证 | 2-3 天 |
| 性能对比 benchmark（JSPI vs Asyncify vs native） | 2-3 天 |

**Phase 2 总计**：1-2 周。

---

## 六、风险与代价

### 6.1 浏览器兼容性

| 浏览器 | JSPI 支持 |
|---|---|
| Chrome 132+ / Edge 132+ | ✅ 默认开启 |
| Firefox | 实验性（`javascript.options.wasm_js_promise_integration`） |
| Safari | ❌ 未支持 |

**应对**：双轨编译，浏览器特性检测自动 fallback 到 Asyncify 版本。

### 6.2 emcc JSPI 后端成熟度

emcc 对 JSPI 的支持还在迭代。已知问题：
- 某些组合（如 pthread + JSPI）兼容性需要验证
- ASYNCIFY 转 JSPI 时少量 API 行为差异

**应对**：跑全套 jtask 业务测试，对比 Asyncify 路径的行为。Phase 2 验证阶段重点关注 pthread + JSPI 的稳定性。

### 6.3 标准变化

JSPI 仍在 wasm CG 推进中。emcc 的 API 可能跟标准变化调整。生产部署时锁定 emcc 版本。

---

## 七、验证标准

### 7.1 Chrome 跑 JSPI 路径

最小 demo + jtask 全业务，行为和 Asyncify 路径一致。

### 7.2 Firefox/Safari 跑 Asyncify 路径

特性检测正确 fallback。Asyncify 路径行为不变。

### 7.3 性能 benchmark

| 指标 | 期望 |
|---|---|
| 协程切换 RTT (JSPI) | 接近 native (5-10x 优于 Asyncify) |
| 二进制大小 (JSPI) | 接近不开协程的 wasm |
| 帧渲染耗时 (JSPI) | 接近 native |

---

## 八、Phase 2 完成标准

- [ ] jtask Makefile 双 target 跑通
- [ ] mapgen Makefile 双 target 跑通
- [ ] HTML 入口特性检测正确
- [ ] Chrome JSPI 路径业务回归通过
- [ ] Firefox/Safari Asyncify fallback 验证
- [ ] 性能数据收集完毕
- [ ] 文档：`双轨构建说明`、`浏览器兼容矩阵`

---

## 九、如果 JSPI 后续标准化

Phase 2 后期（半年到一年），如果 JSPI 进入 wasm 标准（Phase 4/5），Safari 也支持后：

- 逐步淘汰 Asyncify 路径
- 单一 wasm 产物，所有浏览器跑 JSPI
- ASYNCIFY_IMPORTS 维护成本归零

---

## 十、待审批

请确认：

1. 同意 Phase 2 范围（双轨编译，特性检测自动 fallback）
2. 接受 Safari 用户暂时拿到 Asyncify 性能（直到 Safari 支持 JSPI）
3. 性能 benchmark 通过后再清理 Asyncify 路径（可能在数月后）
