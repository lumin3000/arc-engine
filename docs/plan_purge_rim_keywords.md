# 计划：清除 arc-engine 中所有 RimWorld 相关关键字

日期：2026-04-27
范围：`/Volumes/thunderbolt/works/11/arc-engine` 全仓 + 联动 mapgen / gunslinger 不让其挂掉
原则：A/B/C 三类全清。引擎内不得残留任何 RW 出处的命名、术语、注释。

---

## 0. 命名学边界（先达成共识，再动手）

判定一个符号是不是"RW 关键字"的标准：去 `/Volumes/thunderbolt/works/11/RimWorldPrototypePackcode/rim.latesd/Verse/` 里 `ls`，能找到同名 `.cs` 文件、且 arc-engine 用法/字段/方法名一一对应 → 算 RW 借名。

按这个标准，arc-engine 的命中分三类：
- **A 类（RW Verse 引擎层借名）**：TickList/TickManager/TickerType/TimeSpeed/GenTicks/LongEventHandler/CameraDriver/Find/Current/Game/Root/Prefs/GameInit + 鸭子接口字段（doTick/tickerType/isMonoHolder/preMapPostTick/MaxScaffoldTimeMs/WorstAllowedFPS/tickRateMultiplier/ScaffoldPriority）
- **B 类（RW 业务专名残留）**：material.h 的 `SHADER_TYPE_TERRAIN_*` 系列、00_0_types.js 的 `TerrainHard`、render_service.js 的 `query_damage(weapon, armor)`、user_fragment.glsl 注释里的 `terrain variants` / `gunslinger-style` / `s16_review.md`
- **C 类（文档/注释/历史档案里的 RW/消费者名）**：CLAUDE.md / README.md 反例符号、docs/singleton_lock.md 的描述、scripts 注释里 "Games that need..." 这种泛用表述里夹带的 terrain/thing 等词

三类全清。

---

## 1. 引用量调研（已完成）

`grep -E "\b<symbol>\b"` 三仓统计（排除 external/）：

| A 类符号 | engine | mapgen | gunslinger |
|---|---|---|---|
| TickList | 7 | 5 | 0 |
| TickerType | 18 | 35 | 0 |
| TickManager | 12 | 17 | 0 |
| TimeSpeed | 18 | 25 | 0 |
| GenTicks | 9 | 32 | 0 |
| LongEventHandler | 23 | 10 | 0 |
| CameraDriver | 3 | 7 | 0 |
| Find. | 2 | 248 | 0 |
| Current. | 22 | 50 | 0 |
| ScaffoldPriority | 8 | 51 | 22 |
| GameInit | 15 | 60 | 2 |
| tickerType (字段) | 6 | 29 | 0 |
| 其他（doTick/isMonoHolder/...）| 个位 | 个位/十位 | 0 |

**总计**：mapgen 侧 ~600 处引用，gunslinger 侧 ~25 处引用。

这意味着 A 类清理 ≠ 引擎内单仓改名，**消费者 600+ 引用必须同步**，否则消费者 import arc-engine 后 `globalThis.Find` 不存在直接崩。

---

## 2. 重命名映射表（A 类）

引擎层骨架名 → 中性引擎语义。原则：去掉"RimWorld 出处"痕迹，但保留语义可读性，不退化成 User_1/User_2 占位符。

| RW 借名 | 新名 | 理由 |
|---|---|---|
| TickList | TickBucket | 按 ticker 分桶的列表，bucket 是通用术语 |
| TickerType (Never/Normal/Rare/Long) | TickRate (None/Every/Sparse/Slow) | enum 改义，避免 RW 专用词 |
| TickManager | TickScheduler | scheduler 是引擎通用词 |
| TimeSpeed (Paused/Normal/Fast/...) | SimSpeed | sim 是仿真引擎通用词 |
| GenTicks | TickClock | 时钟语义比 GenTicks 直白 |
| LongEventHandler | BlockingTaskQueue | 直译用途："会阻塞主循环的任务队列" |
| CameraDriver | CameraController | controller 是引擎通用词 |
| Find (静态聚合器) | EngineRefs | "引擎单例引用聚合"，不再叫 Find |
| Current (静态全局) | EngineState | 引擎状态根 |
| Game (聚合 tickManager/cameraDriver/currentMap) | EngineSession | 一次"游戏会话"的根 |
| Root (启动根) | EngineRoot | 保留 Root，加 Engine 前缀防混淆 |
| Prefs | EnginePrefs | 同上 |
| GameInit | EngineBootstrap | 启动初始化阶段 |
| 字段 .doTick() | .tick() | RW 用 doTick，引擎用更短的 tick |
| 字段 .tickerType | .tickRate | 跟 enum 改名一致 |
| 字段 .isMonoHolder | .holdsChildren | 描述行为，不绑定 RW Mono 概念 |
| 字段 .preMapPostTick | .preSessionPostTick | 跟 Game→EngineSession 一致 |
| MaxScaffoldTimeMs / WorstAllowedFPS | MaxFrameBudgetMs / MinAllowedFps | 帧预算语义 |
| tickRateMultiplier | tickSpeedFactor | 跟 SimSpeed 一致 |
| ScaffoldPriority | FrameStagePriority | scaffold 是 RW 内部词，frame stage 通用 |

**所有重命名必须三仓同步，不允许引擎留新名 + 消费者留旧名**。

---

## 3. B 类清理（业务专名残留）

| 文件 | 原 | 新 | 备注 |
|---|---|---|---|
| `src/bedrock/gfx/material.h` | `SHADER_TYPE_TERRAIN_HARD` 等 8 个 | `SHADER_TYPE_USER_1` … `SHADER_TYPE_USER_8` | 引擎只提供 8 个 user shader 槽，业务名由消费者侧 typedef 别名 |
| `src/bedrock/gfx/material.c` | 对应 switch case | 跟改 | |
| `scripts/render/00_0_types.js` | `TerrainHard:1` 等 | `User1:1` … `User8:8` | 同上 |
| `scripts/rt/render_service.js:123` | `S.query_damage = function (weapon, armor) {}` | 整行删除 | 引擎不该认识 damage/weapon/armor，由消费者自挂 |
| `src/shader/user_fragment.glsl:3-7` | "terrain variants, edge detection, procedural effects"、"gunslinger-style build rules"、"docs/s16_review.md" | 改成"e.g. extra texture branches, post effects"、"games place their own user_fragment.glsl into the shader build directory before sokol-shdc"、删除 s16_review 引用 | 注释纯净化 |
| `scripts/bedrock/03_frame_callbacks.js:20` | `TERRAIN: 400` | 整行删除 | mapgen 未引用，gunslinger 未引用 |

---

## 4. C 类清理（文档/注释 RW & 消费者名）

| 文件 | 处理 |
|---|---|
| `arc-engine/CLAUDE.md` | 反例清单里 `<游戏专名清单>` 占位符保留；其余 RW/消费者具名删 |
| `arc-engine/README.md` | 通读，凡提"对齐 RimWorld"、"参考 RW Verse"、"gunslinger/mapgen"具名 → 改占位符 |
| `arc-engine/docs/singleton_lock.md` | 例子里若有 gunslinger/mapgen 具名 → `<consumer>` |
| `scripts/bedrock/08_find.js:67-71` 注释 | 删 "Games extend by..." 里的 RW 概念引用 |
| `scripts/rt/render_service.js:14,21-22` | 删具体游戏名 |
| `src/bedrock/noise/{noise.c,noise.h}:4` | 删 "scripts/mapgen/" 路径引用 |
| `src/bedrock/engine_asset.h:7-8,30,37` | `player.png`/`terrain flow map` → 通用例（如 `sprite.png`/`flow texture`） |
| `src/bedrock/engine_main.c:160-161` | 删 mapgen/gunslinger 具名 |
| `src/bedrock/engine_main.h:45-46,75` | 同上 |
| `src/bedrock/gfx/render.h:99` / `src/bindings/batch.c:211` | `blood canvas` → `auxiliary user texture` |
| `src/bindings/graphics_bindings.c:124` | `Thing` → `dynamic mesh` |
| `src/bindings/common_bindings.c:25` | 删 `03_player.js` 引用 |
| `src/bedrock/gfx/graphics.c:281` | "terrain" → 通用表述 |

---

## 5. 消费者侧策略（mapgen 600+ / gunslinger 25+ 引用）

**不在引擎仓搞兼容 shim**（违背"清干净"目标）。改用机械化全仓重命名：

1. 在每个消费者仓写一个 `tools/rename_engine_symbols.sh`，内容是 `git ls-files -z | xargs -0 perl -pi -e 's/\bTickList\b/TickBucket/g; s/\bTickerType\b/TickRate.../g; ...'` 这种确定性替换
2. 替换前后的 git diff 必须完整 review，**不允许整批 commit 不看**
3. 替换后跑：
   - `cd arc-mapgen && make arc 2>&1 | tee /tmp/rename_arc.log` 必须无 undefined 错
   - `cd arc-mapgen && make test-auto 2>&1 | tee /tmp/rename_test.log` 必须能生成地图
   - `cd sokol-javascript-game-gunslinger && make 2>&1 | tee /tmp/rename_gun.log` 必须 build 通过
4. mapgen 侧仍需手挂 `TickClock.getCameraUpdateRate`（原 GenTicks.getCameraUpdateRate 的逻辑），引擎不再持有

风险：rename 脚本误伤同名局部变量（如某处 `let scaffold = ...`）。缓解：每条规则用 `\b` 严格词界，且只匹配命名空间限定的全名（如 `globalThis.Find` → `globalThis.EngineRefs`，而不是裸 `Find`）。裸名引用必须人工逐处确认。

---

## 6. enum 数值/接口形状保留

- 所有 enum 的数值 ID 不变（TickerType.Normal=0 改名 TickRate.Every 仍=0），消费者持有的数值常量无需改
- TickList → TickBucket 的 public 方法签名不变（register/deregister/Tick），只是类名和形参名改
- 鸭子接口字段 .doTick → .tick：消费者侧 600+ 处会被脚本扫到，无遗漏

---

## 7. 阶段拆分

### 阶段 1：本计划文档（已完成）

### 阶段 2：A 类引擎侧改名

1. 改 `scripts/bedrock/00_time.js`（GenTicks → TickClock）
2. 改 `scripts/bedrock/01_long_event_handler.js`（LongEventHandler → BlockingTaskQueue）
3. 改 `scripts/bedrock/02_game_init.js`（GameInit → EngineBootstrap）
4. 改 `scripts/bedrock/03_frame_callbacks.js`（ScaffoldPriority → FrameStagePriority）
5. 改 `scripts/bedrock/05_current.js`（Current → EngineState）
6. 改 `scripts/bedrock/06_root.js`（Root → EngineRoot）
7. 改 `scripts/bedrock/07_game.js`（Game → EngineSession）
8. 改 `scripts/bedrock/08_find.js`（Find → EngineRefs）
9. 改 `scripts/bedrock/09_prefs.js`（Prefs → EnginePrefs）
10. 改 `scripts/render/13_camera_driver.js`（CameraDriver → CameraController）
11. 改 `scripts/tick/01_tick_list.js`（TickList → TickBucket，TickerType → TickRate，TimeSpeed → SimSpeed）
12. 改 `scripts/tick/02_tick_manager.js`（TickManager → TickScheduler，所有内部字段同步）
13. 改 `scripts/rt/{start,loader,render}_service.js` 引用
14. 改 `scripts/bedrock/99_startup.js` 引用
15. 改 `scripts/bedrock/03_loader.js` 引用

### 阶段 3：B 类引擎侧改造

16. `src/bedrock/gfx/material.{h,c}` SHADER_TYPE_TERRAIN_* → SHADER_TYPE_USER_*
17. `scripts/render/00_0_types.js` TerrainHard 等 → User1 等
18. `scripts/rt/render_service.js:123` 删 query_damage
19. `src/shader/user_fragment.glsl` 注释纯净化
20. `scripts/bedrock/03_frame_callbacks.js:20` 删 TERRAIN

### 阶段 4：C 类文档/注释

21. CLAUDE.md / README.md / docs/singleton_lock.md 占位符化
22. scripts/ 注释清理（08_find.js / render_service.js）
23. src/ 注释清理（noise / engine_asset / engine_main / render / batch / graphics_bindings / common_bindings / graphics）

### 阶段 5：消费者侧机械化重命名

24. 写 `arc-mapgen/tools/rename_engine_symbols.sh`，跑、review diff、commit
25. 写 `sokol-javascript-game-gunslinger/tools/rename_engine_symbols.sh`，跑、review diff、commit
26. mapgen 侧补挂 `TickClock.getCameraUpdateRate`

### 阶段 6：验证（必须三仓全过）

27. `cd arc-mapgen && make arc 2>&1 | tee /tmp/test_arc.log` — 视觉确认地图渲染正常
28. `cd arc-mapgen && make test-auto 2>&1 | tee /tmp/test_auto.log`
29. `cd sokol-javascript-game-gunslinger && make 2>&1 | tee /tmp/test_gun.log`
30. 完成判定 grep（见 §9）全部返回空

### 阶段 7：commit

31. arc-engine commit（计划 + ABC 三类清扫）
32. arc-mapgen commit（机械化重命名 + 补挂）
33. gunslinger commit（机械化重命名）

---

## 8. 风险与回退

| 风险 | 缓解 |
|---|---|
| 消费者 600+ 引用机械替换误伤 | 严格 `\b` 词界 + 命名空间限定全名 + git diff 全量人工 review |
| enum 数值若不慎被改 | 改名只改 identifier，不改 `=数字` 部分；改完后 grep 确认数值未动 |
| 鸭子接口字段 .doTick → .tick 漏改某处 | 三仓 grep `\.doTick\b` 必须全空才算完成 |
| RW 接口形状泄漏到外部 API（消费者依赖某个引擎暴露的 RW 字段）| 改名前先 grep 消费者代码用了哪些公开字段，全部纳入映射表 |
| 改完三仓 build 通过但运行时崩 | make arc 必须视觉确认地图实际渲染，不能只看 build 退出码 |
| 改一半 token 不够 | 每完成一个阶段立即 commit，工作树干净后再进下一阶段 |

回退：每阶段独立 commit，出错 `git revert <hash>` 回到上一阶段干净状态。

---

## 9. 完成判定

```bash
# 引擎内 RW 关键字（全空）
grep -rnE "\b(TickList|TickerType|TickManager|TimeSpeed|GenTicks|LongEventHandler|CameraDriver|ScaffoldPriority|GameInit|MaxScaffoldTimeMs|WorstAllowedFPS|tickRateMultiplier|preMapPostTick|isMonoHolder|doTick|tickerType)\b" /Volumes/thunderbolt/works/11/arc-engine/src /Volumes/thunderbolt/works/11/arc-engine/scripts

# 引擎内 RW 业务词（全空）
grep -rniE "\b(rim|rimworld|verse|pawn|colonist|terrain|thing|blood|grime|filth|food|rest|player|enemy|wave|bullet|gunslinger|mapgen)\b" /Volumes/thunderbolt/works/11/arc-engine/src /Volumes/thunderbolt/works/11/arc-engine/scripts /Volumes/thunderbolt/works/11/arc-engine/docs /Volumes/thunderbolt/works/11/arc-engine/CLAUDE.md /Volumes/thunderbolt/works/11/arc-engine/README.md | grep -v plan_purge_rim_keywords.md

# 三仓 build
cd /Volumes/thunderbolt/works/11/arc-mapgen && make arc && make test-auto
cd /Volumes/thunderbolt/works/11/sokol-javascript-game-gunslinger && make
```

三个 grep 全空 + 三个 build 全过 + make arc 视觉确认地图渲染 = 完成。

---

## 10. 待用户确认

1. 命名映射表（§2）的新名是否接受？
2. 消费者侧机械化重命名策略（§5）是否接受？还是另有偏好（如保留兼容 shim）？
3. enum 数值不变（§6）是否接受？
4. 阶段顺序（§7）是否调整？
5. 是否同意先动 arc-engine、再动 mapgen、最后 gunslinger 的顺序？

**计划写完，等待审批后再动手。**
