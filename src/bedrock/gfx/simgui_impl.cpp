#include "../../../external/cimgui/imgui/imgui.h"
#include <cstdio>

#include "../../../external/sokol/c/sokol_app.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "../../../external/sokol/c/sokol_log.h"

#define SOKOL_IMGUI_IMPL
#include "../../../external/sokol/sokol_imgui.h"

static bool s_simgui_initialized = false;

extern "C" {

void simgui_setup_wrapper(void) {
    simgui_desc_t desc = {};
    desc.no_default_font = true;
    desc.logger.func = slog_func;
    simgui_setup(&desc);

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "res/fonts/SourceHanSansSC-Regular.ttf",
        16.0f,
        NULL,
        NULL
    );
    if (font) {
        io.FontDefault = font;
    } else {
        io.Fonts->AddFontDefault();
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

void simgui_shutdown_wrapper(void) {
    simgui_shutdown();
    s_simgui_initialized = false;
}

bool simgui_handle_event_wrapper(const sapp_event *event) {
    if (!s_simgui_initialized) return false;
    return simgui_handle_event(event);
}

}
