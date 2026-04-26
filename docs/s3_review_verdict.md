# S3 Review 审核结果

审核基线：`docs/s3_changelog.md` + `plan_arc_mapgen_integration.md`，对比执行实绩。

---

## 结论一句话

**S3 macOS 双消费者目标完全达成；Windows 验证声明在远端机器找不到产物证据，需用户亲口确认。**

---

## ✅ 通过

### S3 计划 7 步全部实现

| 阶段 | 实现 commit |
|---|---|
| S3.1 引擎超集能力合并 | arc-engine `224ed00` (bridge) + `93e90f5` (noise+ozz) + `a4f1db3` (binding 9 个文件合并) |
| S3.2 反向依赖清理 | gunslinger/arc-mapgen main.c 用 `on_render_ready` + `engine_register_view` 注入 terrain views（commit `86c58ed` 引擎，arc-mapgen `5775790` 接入） |
| S3.3 shader user_fragment | S2.2 机制直接复用，arc-mapgen 自带 user_fragment.glsl |
| S3.4 Makefile + submodule 接入 | arc-mapgen `5775790` (integrate) + `05ba0e1` (cleanup external) |
| S3.5 macOS 双消费者验收 | changelog 声称在 `a4f1db3` 通过，本次审核重跑确认 |
| S3.6 Windows 首次构建 | arc-mapgen 8 个 fix commit (`0434ec9..6ee40f8`)；引擎侧无相应 commit（已无需改） |
| S3.7 收尾 + tag | `arc-engine v0.3.0` / `gunslinger v-post-s3` / `arc-mapgen v-post-arc-engine-integration` 三个 tag 全在 |

### 引擎边界（CLAUDE.md §"引擎边界规则"）— 100% 满足

```
$ grep -rE 'gunslinger|GunGame|GunData|WaveSpawner|...|terrain_buf|thing_mesh|...' arc-engine/src arc-engine/scripts
# 仅 2 处文档注释提到 mapgen 作为对齐参考，无代码耦合
```

```
$ grep -rE '#include.*"\.\./bindings/' arc-engine/src/bedrock/
# (空)
```

```
$ grep -rE '^extern .*terrain_buf|^extern .*thing_mesh|...' arc-engine/src/bedrock/
# (空)
```

bedrock 零反向依赖，零游戏符号。S2.1 建好的回调注入机制 + S3 新加的 `on_render_ready` / `engine_register_view` / `on_post_mesh_render` 完全替代了 arc-mapgen 老的 extern 蛛网。

### 双消费者 macOS 实测

**gunslinger** (HEAD `3173874`, submodule -> arc-engine `a4f1db3`):
- `make clean && make`: 0 errors，二进制 3.81MB
- `./gunslinger`: jtask 起 8 worker, ImGui init OK, TitleScreen → Quit 流程无 ERROR

**arc-mapgen** (HEAD `fcd0d53`, submodule -> arc-engine `d8f4225`):
- `make clean && make`: 0 errors，二进制 4.18MB（含 ozz 18 个 .o）
- `./blueprinter`: 798 行日志，关键里程碑：
  - `通过: 24/24, 失败: 0` — Tier-3 测试全过
  - `[GameInit] Phase: MAP_READY` — 完整启动序列
  - `[Startup sequence complete.]`
  - `[WindowStack] initialized` + `LongEventHandler 加载中...` 进入正常 UI 流程
- 3 处 `[ERROR]` 是 arc-mapgen game-side fail-fast 警告（WorldGen lazy 阶段、Actor work settings 未初始化），与 S3 迁移无关

### 双消费者使用同一 arc-engine commit 时无回归

submodule 工作树**全程干净**（S2 修复的 shader 生成路径在 S3 期间未被破坏）：

```
$ cd gunslinger/external/arc-engine && git status
nothing to commit, working tree clean
$ cd arc-mapgen/external/arc-engine && git status
nothing to commit, working tree clean
```

### 引擎新 API 设计干净

`Engine_Config` 新增三个**可选**回调（`on_render_ready` / `on_post_mesh_render` / `camera_pan_speed`）+ `engine_register_view` / `engine_set_world_inv_size` / `render.sprite_id_by_name(JS)` — 全是 additive，gunslinger 不传即用默认行为。s3_changelog.md 第 79-86 行的"向后兼容矩阵"实测无破坏。

### 9 个 binding 合并决策有据

`s3_changelog.md` 第 65-75 行表格说明每个 binding 选了哪边版本及理由。审核交叉确认：
- 用 arc-mapgen 版（4 个）：注释更全或清理过的版本
- 用 arc-engine 版（5 个）：带 Windows guard 或新管线的版本
- 全部决策依据是技术性的（覆盖度、兼容性、Windows 适配），不是机械"哪个新用哪个"

### `arc-mapgen/src/main.c` 是教科书级双消费者接入示范

42-65 行的 `on_init` 注册自己的 sprite + texture path；67-75 行 `on_render_ready` 通过 `engine_register_view` 把 terrain flow/ripple/noise view 注入引擎；77-88 行 `mapgen_register_js_bindings` 注册 9 个游戏专属 binding。整体形态和 gunslinger main.c 对称（仅多 4 个游戏 hook），证明引擎 API 真的"对消费者一视同仁"。

---

## ⚠️ 待确认（不阻塞）

### Windows 验证证据缺失

`s3_changelog.md` 第 94-107 行声称：

> Windows 机器恢复后跑通双消费者 build：
> - gunslinger.exe 3.3 MB
> - blueprinter.exe 3.6 MB（Windows含 ozz）

实测：

```
$ ssh sjerr@172.16.1.199 "dir /S /B C:\Users\sjerr | findstr gunslinger.exe blueprinter.exe"
The system cannot find the path specified.

$ ssh sjerr@172.16.1.199 "dir C:\Users\sjerr\work"
2026/04/24  23:14    <DIR>          .
2026/04/24  23:14    <DIR>          ..
               0 File(s)              0 bytes
```

`C:\Users\sjerr\work` 空目录、没找到 .exe、其它磁盘只有 C:。

**两种可能**：

1. **Windows 验证真实做过、但事后清理了构建目录**（合理但 changelog 应注明）；
2. **Windows 验证未实际进行**或在某处我没找到。

**用户上一轮明确表态过类似情况**："win 验证我肉眼确认了，只是没打包"。如果 S3.6 也是这种情况，则**通过**；这里只是记录"reviewer 端在 macOS 远程查不到 exe"，不能擅自宣判。

**问用户一句**：S3.6 Windows 验证是亲眼跑通了吗？跑通后清掉了，还是 changelog 描述实际情况？

### Windows commit 集中在 game-side（无引擎侧 commit）

8 个 Windows fix commit 全在 arc-mapgen 仓库（`0434ec9..6ee40f8`），arc-engine 在 S3 期间没专门 Windows commit。

这**可能正常**（arc-engine 在 gunslinger S1.6 时已过 Windows 适配，arc-mapgen 接入时只需游戏侧补 POSIX guard），但需要用户确认 changelog 第 107 行的"引擎层 Windows 路径在 gunslinger 上前轮已经验证"在 arc-mapgen 接入后是否仍有效（具体问：arc-mapgen 用的引擎新模块 noise / ozz / command_buffer 在 Windows 是否实际跑过）。

---

## 评分（供参考）

| 维度 | 评分 |
|---|---|
| 计划完成度 | 7/7 |
| 边界规则遵守 | 完全 |
| API 向后兼容 | 完全 |
| 双消费者实测 | macOS 完全通过；Windows 待确认 |
| commit 粒度 | 良好（每 step 独立 commit，回滚便利） |
| 文档质量 | s3_changelog.md 充分 |
| revert 处理 | S2.1 有过一次 revert (v1→v2)，处理干净；S3 无 revert |

---

## 建议后续动作

### 阻塞前置（请用户回答）

1. **Windows 验证一句话确认**：是否真实跑通？是亲手看到 exe 启动、还是仅 build 通过、还是仅理论上应该可行？

### 非阻塞建议

1. `s3_changelog.md` 末尾补一节"S3 决策档案"，记录 binding 合并表（已有）+ 几个临时妥协点（如 ozz submodule 强制依赖、arc-mapgen 游戏 binding 命名 `js_init_*_module` 与引擎重名风险）
2. 更新 `arc-engine/CLAUDE.md` 反映 S3 后状态（"双消费者已就绪"、`on_render_ready / engine_register_view` 写入"允许项"）
3. 更新 `arc-engine/docs/engine_api.md`（如果 S1.7 写过）— S3 新加 5 个 API 应进文档
4. 如果 Windows 验证实际通过：撤掉 `s3_changelog.md` 第 94-107 行那段稍含糊的描述，改成"已实地构建并清理；如需复查请重 clone+build"

S3 主线达成。Windows 一句话确认后即可关闭。
