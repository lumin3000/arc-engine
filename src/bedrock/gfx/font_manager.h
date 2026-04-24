
#ifndef ARC_FONT_MANAGER_H
#define ARC_FONT_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#define FONT_MANAGER_TEXSIZE    2048
#define FONT_MANAGER_GLYPHSIZE  64
#define MAX_FONT_NUM            64

#define FONT_MANAGER_SLOTLINE   (FONT_MANAGER_TEXSIZE / FONT_MANAGER_GLYPHSIZE)
#define FONT_MANAGER_SLOTS      (FONT_MANAGER_SLOTLINE * FONT_MANAGER_SLOTLINE)
#define FONT_MANAGER_HASHSLOTS  (FONT_MANAGER_SLOTS * 2)

typedef struct {
    int16_t offset_x;
    int16_t offset_y;
    int16_t advance_x;
    int16_t advance_y;
    uint16_t w;
    uint16_t h;
    uint16_t u;
    uint16_t v;
} FontGlyph;

struct font_manager;

size_t font_manager_sizeof(void);

void font_manager_init(struct font_manager *F);

void font_manager_shutdown(struct font_manager *F);

int font_manager_load_font(struct font_manager *F, const char *path, int font_id);

const char *font_manager_glyph(struct font_manager *F, int font_id, int codepoint, int size, FontGlyph *g);

const char *font_manager_glyph_ex(struct font_manager *F, int font_id, int codepoint, int size, FontGlyph *g, FontGlyph *og);

void font_manager_scale(struct font_manager *F, FontGlyph *glyph, int size);

void font_manager_metrics(struct font_manager *F, int font_id, int size,
                          int *ascent, int *descent, int *line_gap);

int font_manager_pixel_size(struct font_manager *F, int font_id, int point_size);

int font_manager_underline(struct font_manager *F, int font_id, int size,
                           float *position, float *thickness);

int font_manager_flush(struct font_manager *F);

const void *font_manager_texture(struct font_manager *F, int *sz);

float font_manager_sdf_mask(struct font_manager *F);

float font_manager_sdf_distance(struct font_manager *F, uint8_t num_pixel);

int font_manager_has_codepoint(struct font_manager *F, int font_id, int codepoint);

static inline uint32_t codepoint_key(int font, int codepoint) {
    return (uint32_t)((font << 24) | codepoint);
}

static inline int font_index(int font_id) {
    return font_id & 0x3F;
}

#endif
