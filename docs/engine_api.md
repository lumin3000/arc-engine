# 渲染扩展 API

本文记录当前新增的渲染接口，不是所有绑定的完整清单。

## 帧末资源提交

`RenderFrameCallbacks.registerAfter(callback, name)` 注册物理绘制帧的收尾函数。`render_frame` 在常规绘制回调、分阶段回调之后，且提交到 C 绘制端之前调用；分阶段因阻塞提前结束时也调用。参数为当前帧的 `dt`。

适用于多个视图共用的暂存纹理：各视图先完成修改和绘制命令提交，最后一次性上传每个脏页，遵守同一 GPU 图像每帧最多更新一次的约束。收尾函数不应再次生成需要更早阶段处理的绘制请求。异常使用既有回调日志机制记录。

## ImGui 图集图片

`imgui.draw_image(textureId, x, y, w, h, u0, v0, u1, v1, [r, g, b, a])` 向当前 ImGui 窗口的 draw list 添加图片。矩形使用 UI 坐标；UV 使用图集归一化坐标。旧九参数调用使用不透明白色，保留图片自身 alpha。可选 tint 为四个独立参数，必须全部提供、均为有限的 0–255 数值，四舍五入为 RGBA 字节并乘到源像素；部分 tint 或越界数值抛错。原纹理/UV/所有权不变。

`textureId` 必须是已经加载的有效纹理 ID。绑定先查图集页（10000+），再查 graphics 纹理池（含 staged 合成页），取 `sg_view` 并转换为 ImGui 纹理引用。尺寸必须大于零，坐标必须有限；两池均无该纹理或参数无效时抛错。接口不加载文件、不转移纹理所有权，资源仍由其来源管理。

## 业务 shader 依赖

引擎核心只包含通用 `shader_utils.glsl` 和消费者提供的 `user_fragment.glsl`。消费者的专用函数库应由自己的片段 `@include`，并在自己的构建规则中列出依赖、复制到 shader staging 目录；不得让引擎核心无条件引用某个消费者才有的文件。

## 相机缩放范围（2026-11-12）

`coord` 提供运行时缩放范围接口。`Game_State.zoom_min/zoom_max` 初值 `MIN_ZOOM_LEVEL`/`MAX_ZOOM_LEVEL`（0.03/200），`engine_main.c` 全部缩放夹取读运行时值，默认行为不变。

- `coord.set_camera_zoom_range(min, max)`：两参须为有限数且 `0 < min <= max`，否则 `JS_ThrowRangeError`；`argc < 2` 为 TypeError。设定后立即把 `zoom_level` 与 `desired_zoom_level` 各自夹进新范围（越界取边界值）。
- `coord.get_camera_zoom_range()`：返回 `{min, max}`；`ctx.gs` 未就绪抛 InternalError。
- `coord.set_camera_zoom(z)`：`z` 非有限数或越出当前 `[zoom_min, zoom_max]` → `JS_ThrowRangeError`（Fail-Fast）；调用方负责先夹。游戏侧 `game_bindings.c` 的同名实现注册在后覆盖引擎版，两处校验一致。
- 缩放平滑改 60Hz 固定步（RW `Verse/Gen.cs` `FixedTimeStepUpdate` 同法）：帧 dt（已被 `MIN_FRAME_TIME` 封顶）累加进 buffer，每整步执行一次 `zoom += (desired-zoom)*ZOOM_TIGHTNESS`，相机控制被屏蔽时不累加不步进；到位时间与帧率无关。键盘 `-`/`=` 缩放改按 `60*dt` 折算倍率，按住时长决定缩放量。

### 朝鼠标缩放（2026-11-12，frame_cap_camera_zoom 役3）

- `coord.set_zoom_to_cursor(on)`：参数须为严格布尔，否则 `JS_ThrowTypeError`；写 `Game_State.zoom_to_cursor`（初值 0=关），`ctx.gs` 未就绪时静默不写。对齐 RW `Verse/CameraDriver.cs` `ZoomToMouse`。
- 开启且本帧 `zoom_level` 实际变化、且 `camera_wheel_blocked` 未置位时（相机 controls 被屏蔽时整块本就不执行），引擎在固定步/夹取之后做锚定：用当前（旧）ortho 经 `camera_screen_to_world_ground` 求光标处地面世界点 P0，临时 `camera_set_ortho_size` 为新 ortho（置 `matrices_dirty`，取 VP 即重算）求 P1，`cam_pos.x/z += P0-P1`（只动 x/z，y 不动），随后恢复旧 ortho——下一帧开头按新 zoom 重设，本帧渲染取景一致；`cam_pos` 经 `ctx.gs` 下一帧 `camera_set_position` 生效，消费者侧 JS 平移/界内夹取照常在其后执行。
- `camera_screen_to_world_ground(cam, sx, sy, win_w, win_h, *out_x, *out_z)`：屏幕像素→y=0 地面世界点的内部 helper，`coord.screen_to_world` 绑定与锚定路径共用同一反投影数学（NDC→VP 逆矩阵→近/远射线→y=0 平面交点）。

## 帧率上限（2026-11-12，frame_cap_camera_zoom 役5）

- `Engine_Config.frame_cap_fps`（int，默认 0=不限）：JS 渲染帧签发限速——`js_runtime_render` 每引擎帧至多向 render service 签发一个 `frame` 请求，签发间隔不少于 `1/cap`（`engine_frame_rate_allow`，时基 `clock_gettime(CLOCK_MONOTONIC)`）。引擎帧回调本身不降速：输入/相机/音频照常跑满，cap 只压呈现帧率；签发被跳过的引擎帧下一帧重试，自然量化到引擎帧周期。0 保持改动前行为；`__EMSCRIPTEN__` 同样生效（rAF 只管唤醒）。实测语义：帧工作已超 `1/cap` 时不追帧，实际帧率=工作帧率；帧工作低于 cap 时呈现帧率被钉在 cap。
- 环境变量 `ARC_FRAME_CAP=N`：启动时解析，存在即覆盖 config；须为非负整数（`strtol` 全串校验，0=不限），非法值 `fprintf(stderr)+exit(1)` fatal。cap 低于 sim 所需帧率时 tick 照常按真实 dt 跑（`MIN_FRAME_TIME` 封顶），只是帧率被压低。
- `game.set_frame_cap(fps)`：整数 ≥0（`isfinite && v==floor(v) && 0<=v<=1000000`），否则 `JS_ThrowRangeError`；`argc<1` TypeError。只改 setting。
- `game.get_frame_cap()` → `{setting, effective, env_override}`：setting=配置+运行时值；env_override=ARC_FRAME_CAP 值，未设置为 `null`；effective=env 在场时跟随 env 否则 setting。
- 启动日志 `[engine] frame_cap: effective=N source=config|env ARC_FRAME_CAP|none`。

## 验证记录（2026-09-10）

两个现有原生消费者使用同一引擎工作目录，clean 后 make 均退出0。主消费者8项金图图差通过；多视图冷读档、编成预览和实际图集物品图标均运行验证。旧诊断场景既存的素材缺失不属于本接口的通过范围，具体记录在消费者计划中。

## 高 DPI 渲染（2026-10-01）

消费者可在 `Engine_Config.high_dpi` 显式启用全分辨率帧缓冲，默认 false。布局、世界屏幕投影、SDF 文本位置、OS 与注入鼠标均使用逻辑窗口单位；Sokol 帧缓冲和 ImGui 字体图集使用物理像素。现有 `game.get_state()` 增加数值 `dpi_scale`，图片预缩小目标像素可用逻辑尺寸乘该倍率。

`ARC_WINDOW_MAIN_DISPLAY` 延续既有固定物理尺寸截图约定，默认关闭高 DPI；`ARC_HIGH_DPI=0|1` 可显式覆盖，用于同屏 1x／2x 验收。其他值拒绝启动。不得把物理截图像素直接作为注入鼠标坐标。

主消费者 Mac Retina 实测逻辑 1147×720／物理 2294×1440；五档字号截图、实际进图与逻辑坐标设置按钮点击、2x UI 探针通过。1x 全金图 9/10，旧侧栏差异仍为 3.547，未重立基线。另一个消费者以此引擎构建失败：其 Makefile 缺 jtask/compat 头路径，补 CLI 路径后其 main.c 仍使用本次改动前已不存在的 startup_ambiance_event 字段；未修改该消费者。Windows／WASM 未实测。
