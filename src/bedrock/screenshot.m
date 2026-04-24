#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "stb_image_write.h"

#define SOKOL_METAL
#include "../../external/sokol/c/sokol_app.h"

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

int screenshot_capture(void) {
    if (!g_screenshot_pending) {
        return 0;
    }
    g_screenshot_pending = 0;

    sapp_swapchain swapchain = sapp_get_swapchain();
    const void *drawable_ptr = swapchain.metal.current_drawable;
    if (!drawable_ptr) {
        fprintf(stderr, "[screenshot] Failed to get current drawable\n");
        return -1;
    }

    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawable_ptr;
    id<MTLTexture> texture = drawable.texture;

    if (!texture) {
        fprintf(stderr, "[screenshot] Failed to get texture from drawable\n");
        return -1;
    }

    NSUInteger width = texture.width;
    NSUInteger height = texture.height;
    NSUInteger bytesPerPixel = 4;
    NSUInteger bytesPerRow = width * bytesPerPixel;

    uint8_t *pixels = (uint8_t *)malloc(width * height * bytesPerPixel);
    if (!pixels) {
        fprintf(stderr, "[screenshot] Failed to allocate pixel buffer\n");
        return -1;
    }

    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture getBytes:pixels
          bytesPerRow:bytesPerRow
           fromRegion:region
          mipmapLevel:0];

    for (NSUInteger i = 0; i < width * height; i++) {
        uint8_t b = pixels[i * 4 + 0];
        uint8_t r = pixels[i * 4 + 2];
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 2] = b;
    }

    int result = stbi_write_png(g_screenshot_path, (int)width, (int)height, 4, pixels, (int)bytesPerRow);

    free(pixels);

    if (result) {
        fprintf(stderr, "[screenshot] Saved to %s (%lux%lu)\n", g_screenshot_path, width, height);
        return 0;
    } else {
        fprintf(stderr, "[screenshot] Failed to write PNG file\n");
        return -1;
    }
}
