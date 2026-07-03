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

void simgui_render_wrapper(void) {
    if (!s_simgui_initialized) return;
    simgui_render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        int dc = 0;
        for (int i = 0; i < draw_data->CmdListsCount; i++) {
            dc += draw_data->CmdLists[i]->CmdBuffer.Size;
        }
        s_last_draw_calls = dc;
    } else {
        s_last_draw_calls = 0;
    }
}

int simgui_get_draw_call_count(void) {
    return s_last_draw_calls;
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

}
