#include "../../../external/cimgui/imgui/imgui.h"
#include <cstdio>

#include "../../../external/sokol/c/sokol_app.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../../external/sokol/c/sokol_log.h"

#define SOKOL_IMGUI_IMPL
#include "../../../external/sokol/sokol_imgui.h"

extern "C" {
#include "../../../external/stb/stb_truetype.h"
}
#include <cstdlib>

static bool s_simgui_initialized = false;
static float s_font_em_scale = 0.0f;  // 0 = setup 未跑, 消费者读到应 fail-fast

// stb_truetype 把字号解释为 (ascent-descent) 像素行高, 浏览器/CSS 把 font-size
// 解释为 em 大小。补偿系数 = (asc-desc)/unitsPerEm, 从加载的字体度量派生
// (思源黑体为 1.448)。引擎只计算并暴露, 是否启用由消费者决定 (默认行为不变)。
static float compute_font_em_scale(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1.0f;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return 1.0f; }
    unsigned char *buf = (unsigned char *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        fprintf(stderr, "[imgui] WARNING: font em-scale read failed: %s\n", path);
        return 1.0f;
    }
    fclose(f);
    stbtt_fontinfo info;
    float scale = 1.0f;
    if (stbtt_InitFont(&info, buf, stbtt_GetFontOffsetForIndex(buf, 0))) {
        // ScaleForMappingEmToPixels(1) = 1/unitsPerEm, ScaleForPixelHeight(1) = 1/(asc-desc)
        scale = stbtt_ScaleForMappingEmToPixels(&info, 1.0f)
              / stbtt_ScaleForPixelHeight(&info, 1.0f);
    } else {
        fprintf(stderr, "[imgui] WARNING: font em-scale stbtt_InitFont failed: %s\n", path);
    }
    free(buf);
    return scale;
}

extern "C" {

void simgui_setup_wrapper(void) {
    simgui_desc_t desc = {};
    desc.no_default_font = true;
    desc.logger.func = slog_func;
    simgui_setup(&desc);

    ImGuiIO& io = ImGui::GetIO();
    // 优先全量字库(otf); 旧 1.8M 裁剪子集(ttf)作为消费者未放置全量文件时的回退
    const char* font_path = "res/fonts/SourceHanSansSC-Regular.otf";
    ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 16.0f, NULL, NULL);
    if (!font) {
        font_path = "res/fonts/SourceHanSansSC-Regular.ttf";
        font = io.Fonts->AddFontFromFileTTF(font_path, 16.0f, NULL, NULL);
    }
    if (font) {
        io.FontDefault = font;
        s_font_em_scale = compute_font_em_scale(font_path);
    } else {
        io.Fonts->AddFontDefault();
        s_font_em_scale = 1.0f;  // ImGui 内置 ProggyClean 无该口径问题
        fprintf(stderr, "[imgui] WARNING: Failed to load SourceHanSansSC, using default font\n");
    }

    s_simgui_initialized = true;
}

void simgui_new_frame_wrapper(int width, int height, double delta_time) {
    if (!s_simgui_initialized) return;
    simgui_frame_desc_t desc = {};
    desc.width = width;
    desc.height = height;
    desc.delta_time = delta_time;
    simgui_new_frame(&desc);
}

static int s_last_draw_calls = 0;
static int s_last_effective_draws = 0;    // ElemCount>0 的命令 = simgui 实际发出的 sg_draw
static int s_last_texture_switches = 0;   // 有效命令序列中纹理变化次数

// 按 draw list (= ImGui 窗口) 拆分的上一帧统计, 供消费者区分 UI 本体与调试叠层
#define SIMGUI_MAX_LIST_STATS 32
typedef struct {
    char owner[48];
    int cmds;
    int effective;
    int tex_switches;
} SimguiListStat;
static SimguiListStat s_list_stats[SIMGUI_MAX_LIST_STATS];
static int s_list_stat_count = 0;
static int s_list_stat_total = 0;         // 真实 list 数 (可能超过数组上限)

void simgui_render_wrapper(void) {
    if (!s_simgui_initialized) return;
    simgui_render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        int dc = 0, ed = 0, ts = 0;
        ImTextureID last_tex = 0;
        bool have_tex = false;
        s_list_stat_count = 0;
        s_list_stat_total = draw_data->CmdListsCount;
        for (int i = 0; i < draw_data->CmdListsCount; i++) {
            const ImDrawList* dl = draw_data->CmdLists[i];
            dc += dl->CmdBuffer.Size;
            int l_ed = 0, l_ts = 0;
            ImTextureID l_last = 0;
            bool l_have = false;
            for (int c = 0; c < dl->CmdBuffer.Size; c++) {
                const ImDrawCmd* cmd = &dl->CmdBuffer[c];
                if (cmd->UserCallback || cmd->ElemCount == 0) continue;
                ed++;
                l_ed++;
                ImTextureID tex = cmd->GetTexID();
                if (!have_tex) { last_tex = tex; have_tex = true; }
                else if (tex != last_tex) { ts++; last_tex = tex; }
                if (!l_have) { l_last = tex; l_have = true; }
                else if (tex != l_last) { l_ts++; l_last = tex; }
            }
            if (s_list_stat_count < SIMGUI_MAX_LIST_STATS) {
                SimguiListStat* st = &s_list_stats[s_list_stat_count++];
                const char* owner = dl->_OwnerName ? dl->_OwnerName : "";
                snprintf(st->owner, sizeof(st->owner), "%s", owner);
                st->cmds = dl->CmdBuffer.Size;
                st->effective = l_ed;
                st->tex_switches = l_ts;
            }
        }
        s_last_draw_calls = dc;
        s_last_effective_draws = ed;
        s_last_texture_switches = ts;
    } else {
        s_last_draw_calls = 0;
        s_last_effective_draws = 0;
        s_last_texture_switches = 0;
        s_list_stat_count = 0;
        s_list_stat_total = 0;
    }
}

int simgui_get_draw_call_count(void) {
    return s_last_draw_calls;
}

int simgui_get_effective_draw_count(void) {
    return s_last_effective_draws;
}

int simgui_get_texture_switch_count(void) {
    return s_last_texture_switches;
}

// 上一帧 draw list 统计: 返回实际填充条数 (≤SIMGUI_MAX_LIST_STATS), *out_total = 真实 list 数
int simgui_get_list_stats(int index, const char** owner, int* cmds, int* effective,
                          int* tex_switches, int* out_total) {
    if (out_total) *out_total = s_list_stat_total;
    if (index < 0 || index >= s_list_stat_count) return 0;
    const SimguiListStat* st = &s_list_stats[index];
    *owner = st->owner;
    *cmds = st->cmds;
    *effective = st->effective;
    *tex_switches = st->tex_switches;
    return 1;
}

uint64_t simgui_texture_id_wrapper(sg_view view) {
    return (uint64_t)simgui_imtextureid(view);
}

float simgui_get_font_em_scale(void) {
    return s_font_em_scale;
}

void simgui_shutdown_wrapper(void) {
    simgui_shutdown();
    s_simgui_initialized = false;
}

bool simgui_handle_event_wrapper(const sapp_event *event) {
    if (!s_simgui_initialized) return false;
    return simgui_handle_event(event);
}

// 注入指针 (自动化测试) 走与 OS 事件相同的 ImGui 输入通道: 坐标换算同 simgui_handle_event
// (sokol_imgui.h MOUSE_MOVE/DOWN/UP 分支按 cur_dpi_scale 缩放), 否则 WantCaptureMouse 恒假
void simgui_inject_mouse_pos_wrapper(float x, float y) {
    if (!s_simgui_initialized) return;
    const float dpi_scale = _simgui.cur_dpi_scale;
    simgui_add_mouse_pos_event(x / dpi_scale, y / dpi_scale);
}

void simgui_inject_mouse_button_wrapper(int mouse_button, bool down) {
    if (!s_simgui_initialized) return;
    simgui_add_mouse_button_event(mouse_button, down);
}

}
