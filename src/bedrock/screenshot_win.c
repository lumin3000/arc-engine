// Windows D3D11 截图（W1 um560，2026-09-12）。语义对齐 screenshot.m：
// render.c 在 sg_commit() 之后、Present 之前调 screenshot_capture()，此时
// 交换链 backbuffer 已含本帧完整画面；D3D11 的 Map(STAGING) 会阻塞到 GPU
// 拷贝完成，天然等价于 Metal 版的 fence commandBuffer waitUntilCompleted。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform_path.h"
#include "screenshot.h"

#if defined(_WIN32)
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "stb_image_write.h"
#include "../../external/sokol/c/sokol_app.h"
#include "../../external/sokol/c/sokol_gfx.h"  // sg_d3d11_device*()：本仓 sokol 版本 sapp 侧只有 swapchain getter
#endif

static int g_screenshot_pending = 0;
static char g_screenshot_path[1024] = {0};

int screenshot_request(const char *path) {
    if (!path || strlen(path) >= sizeof(g_screenshot_path)) {
        return -1;
    }
    strncpy(g_screenshot_path, path, sizeof(g_screenshot_path) - 1);
    g_screenshot_path[sizeof(g_screenshot_path) - 1] = '\0';
    g_screenshot_pending = 1;
    return 0;
}

int screenshot_is_pending(void) {
    return g_screenshot_pending;
}

#if defined(_WIN32)
// sokol_app.h 自带同名 GUID 为 private；这里独立定义，避免依赖 dxguid.lib。
static const GUID s_iid_ID3D11Texture2D = {
    0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};

int screenshot_capture(void) {
    if (!g_screenshot_pending) {
        return 0;
    }
    g_screenshot_pending = 0;

    ID3D11Device *dev = (ID3D11Device *)sg_d3d11_device();
    ID3D11DeviceContext *dc = (ID3D11DeviceContext *)sg_d3d11_device_context();
    IDXGISwapChain *sc = (IDXGISwapChain *)sapp_d3d11_get_swap_chain();
    if (!dev || !dc || !sc) {
        fprintf(stderr, "[screenshot] D3D11 device/context/swapchain unavailable\n");
        return -1;
    }

    ID3D11Texture2D *back = NULL;
    HRESULT hr = IDXGISwapChain_GetBuffer(sc, 0, &s_iid_ID3D11Texture2D, (void **)&back);
    if (FAILED(hr) || !back) {
        fprintf(stderr, "[screenshot] GetBuffer(0) failed hr=0x%08lx\n", (unsigned long)hr);
        return -1;
    }

    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(back, &desc);
    const UINT w = desc.Width, h = desc.Height;
    int rc = -1;
    ID3D11Texture2D *resolved = NULL;
    ID3D11Texture2D *staging = NULL;
    ID3D11Resource *src = (ID3D11Resource *)back;

    // 多重采样 backbuffer（sokol 当前 sample_count=1 不会走到）：先 resolve 成单采样。
    if (desc.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC rd = desc;
        rd.SampleDesc.Count = 1;
        rd.SampleDesc.Quality = 0;
        rd.BindFlags = D3D11_BIND_RENDER_TARGET;
        rd.MiscFlags = 0;
        hr = ID3D11Device_CreateTexture2D(dev, &rd, NULL, &resolved);
        if (FAILED(hr) || !resolved) {
            fprintf(stderr, "[screenshot] resolve texture create failed hr=0x%08lx\n", (unsigned long)hr);
            goto done;
        }
        ID3D11DeviceContext_ResolveSubresource(dc, (ID3D11Resource *)resolved, 0, src, 0, desc.Format);
        src = (ID3D11Resource *)resolved;
    }

    D3D11_TEXTURE2D_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.Width = w;
    sd.Height = h;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = desc.Format;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(dev, &sd, NULL, &staging);
    if (FAILED(hr) || !staging) {
        fprintf(stderr, "[screenshot] staging texture create failed hr=0x%08lx\n", (unsigned long)hr);
        goto done;
    }
    ID3D11DeviceContext_CopyResource(dc, (ID3D11Resource *)staging, src);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ID3D11DeviceContext_Map(dc, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        fprintf(stderr, "[screenshot] Map failed hr=0x%08lx\n", (unsigned long)hr);
        goto done;
    }

    const size_t row_bytes = (size_t)w * 4;
    uint8_t *pixels = (uint8_t *)malloc(row_bytes * h);
    if (!pixels) {
        ID3D11DeviceContext_Unmap(dc, (ID3D11Resource *)staging, 0);
        fprintf(stderr, "[screenshot] Failed to allocate pixel buffer\n");
        goto done;
    }
    // RowPitch 可能大于 w*4，逐行拷。
    for (UINT y = 0; y < h; y++) {
        memcpy(pixels + y * row_bytes, (const uint8_t *)mapped.pData + y * mapped.RowPitch, row_bytes);
    }
    ID3D11DeviceContext_Unmap(dc, (ID3D11Resource *)staging, 0);

    const int is_bgra = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                         desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    const int is_rgba = (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                         desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    if (!is_bgra && !is_rgba) {
        free(pixels);
        fprintf(stderr, "[screenshot] unsupported backbuffer format %d\n", (int)desc.Format);
        goto done;
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        if (is_bgra) {
            uint8_t b = pixels[i * 4 + 0];
            pixels[i * 4 + 0] = pixels[i * 4 + 2];
            pixels[i * 4 + 2] = b;
        }
        pixels[i * 4 + 3] = 255;  // 与 Metal 版一致：截图不带透明
    }

    char path_buf[1024];
    const char *out = platform_path_resolve(g_screenshot_path, path_buf, sizeof(path_buf));
    int result = stbi_write_png(out, (int)w, (int)h, 4, pixels, (int)row_bytes);
    free(pixels);
    if (result) {
        fprintf(stderr, "[screenshot] Saved to %s (%ux%u)\n", out, w, h);
        rc = 0;
    } else {
        fprintf(stderr, "[screenshot] Failed to write PNG file %s\n", out);
    }

done:
    if (staging) ID3D11Texture2D_Release(staging);
    if (resolved) ID3D11Texture2D_Release(resolved);
    ID3D11Texture2D_Release(back);
    return rc;
}
#else
int screenshot_capture(void) {
    if (!g_screenshot_pending) {
        return 0;
    }
    g_screenshot_pending = 0;
    fprintf(stderr, "[screenshot] Capture not implemented on this platform\n");
    return -1;
}
#endif
