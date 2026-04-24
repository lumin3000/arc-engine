
#include "font_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "../../external/stb/stb_truetype.h"

#define COLLISION_STEP      7
#define DISTANCE_OFFSET     8
#define ORIGINAL_SIZE       (FONT_MANAGER_GLYPHSIZE - DISTANCE_OFFSET * 2)
#define ONEDGE_VALUE        180
#define PIXEL_DIST_SCALE    (ONEDGE_VALUE / (float)(DISTANCE_OFFSET))

struct font_slot {
    uint32_t codepoint_key;
    int16_t offset_x;
    int16_t offset_y;
    int16_t advance_x;
    int16_t advance_y;
    uint16_t w;
    uint16_t h;
};

struct priority_list {
    int version;
    int16_t prev;
    int16_t next;
};

struct loaded_font {
    stbtt_fontinfo info;
    unsigned char *data;
    int loaded;
};

struct font_manager {
    int version;
    int count;
    int16_t list_head;
    struct font_slot slots[FONT_MANAGER_SLOTS];
    struct priority_list priority[FONT_MANAGER_SLOTS];
    int16_t hash[FONT_MANAGER_HASHSLOTS];
    struct loaded_font fonts[MAX_FONT_NUM];
    int dpi_perinch;
    int dirty;
    uint8_t texture_buffer[FONT_MANAGER_TEXSIZE * FONT_MANAGER_TEXSIZE];
};

static const int SPACE_CODEPOINTS[] = {' ', '\t', '\n', '\r'};

static inline int is_space_codepoint(int codepoint) {
    for (size_t i = 0; i < sizeof(SPACE_CODEPOINTS) / sizeof(SPACE_CODEPOINTS[0]); i++) {
        if (codepoint == SPACE_CODEPOINTS[i]) {
            return 1;
        }
    }
    return 0;
}

static inline int hash(int value) {
    return (value * 0xdeece66d + 0xb) % FONT_MANAGER_HASHSLOTS;
}

static int hash_lookup(struct font_manager *F, int cp) {
    int slot;
    int position = hash(cp);
    while ((slot = F->hash[position]) >= 0) {
        struct font_slot *s = &F->slots[slot];
        if (s->codepoint_key == (uint32_t)cp) {
            return slot;
        }
        position = (position + COLLISION_STEP) % FONT_MANAGER_HASHSLOTS;
    }
    return -1;
}

static void rehash(struct font_manager *F);

static void hash_insert(struct font_manager *F, int cp, int slotid) {
    ++F->count;
    if (F->count > FONT_MANAGER_SLOTS + FONT_MANAGER_SLOTS / 2) {
        rehash(F);
    }
    int position = hash(cp);
    int slot;
    while ((slot = F->hash[position]) >= 0) {
        struct font_slot *s = &F->slots[slot];
        if ((int32_t)s->codepoint_key < 0) {
            break;
        }
        assert(s->codepoint_key != (uint32_t)cp);
        position = (position + COLLISION_STEP) % FONT_MANAGER_HASHSLOTS;
    }
    F->hash[position] = slotid;
    F->slots[slotid].codepoint_key = cp;
}

static void rehash(struct font_manager *F) {

    for (int i = 0; i < FONT_MANAGER_HASHSLOTS; i++) {
        F->hash[i] = -1;
    }
    F->count = 0;

    for (int i = 0; i < FONT_MANAGER_SLOTS; i++) {
        int cp = F->slots[i].codepoint_key;
        if ((int32_t)cp >= 0) {
            hash_insert(F, cp, i);
        }
    }
}

static void remove_node(struct font_manager *F, struct priority_list *node) {
    struct priority_list *prev_node = &F->priority[node->prev];
    struct priority_list *next_node = &F->priority[node->next];
    prev_node->next = node->next;
    next_node->prev = node->prev;
}

static void touch_slot(struct font_manager *F, int slotid) {
    struct priority_list *node = &F->priority[slotid];
    node->version = F->version;

    if (slotid == F->list_head) {
        return;
    }

    remove_node(F, node);

    int head = F->list_head;
    int tail = F->priority[head].prev;
    node->prev = tail;
    node->next = head;

    struct priority_list *head_node = &F->priority[head];
    struct priority_list *tail_node = &F->priority[tail];
    head_node->prev = slotid;
    tail_node->next = slotid;

    F->list_head = slotid;
}

static inline const stbtt_fontinfo *get_ttf(struct font_manager *F, int font_id) {
    int idx = font_index(font_id);
    if (idx <= 0 || idx >= MAX_FONT_NUM) {
        return NULL;
    }
    if (!F->fonts[idx].loaded) {
        return NULL;
    }
    return &F->fonts[idx].info;
}

static int font_manager_touch(struct font_manager *F, int font_id, int codepoint, FontGlyph *glyph) {
    int cp = codepoint_key(font_id, codepoint);
    int slot = hash_lookup(F, cp);

    if (slot >= 0) {

        touch_slot(F, slot);
        struct font_slot *s = &F->slots[slot];
        glyph->offset_x = s->offset_x;
        glyph->offset_y = s->offset_y;
        glyph->advance_x = s->advance_x;
        glyph->advance_y = s->advance_y;
        glyph->w = s->w;
        glyph->h = s->h;
        glyph->u = (slot % FONT_MANAGER_SLOTLINE) * FONT_MANAGER_GLYPHSIZE;
        glyph->v = (slot / FONT_MANAGER_SLOTLINE) * FONT_MANAGER_GLYPHSIZE;
        return 1;
    }

    int idx = font_index(font_id);
    if (idx <= 0 || idx >= MAX_FONT_NUM || !F->fonts[idx].loaded) {
        memset(glyph, 0, sizeof(*glyph));
        return -1;
    }

    const stbtt_fontinfo *fi = &F->fonts[idx].info;

    float scale = stbtt_ScaleForMappingEmToPixels(fi, ORIGINAL_SIZE);
    int ascent, descent, lineGap;
    int advance, lsb;
    int ix0, iy0, ix1, iy1;

    if (!stbtt_GetFontVMetricsOS2(fi, &ascent, &descent, &lineGap)) {
        stbtt_GetFontVMetrics(fi, &ascent, &descent, &lineGap);
    }
    stbtt_GetCodepointHMetrics(fi, codepoint, &advance, &lsb);
    stbtt_GetCodepointBitmapBox(fi, codepoint, scale, scale, &ix0, &iy0, &ix1, &iy1);

    glyph->w = ix1 - ix0 + DISTANCE_OFFSET * 2;
    glyph->h = iy1 - iy0 + DISTANCE_OFFSET * 2;
    glyph->offset_x = (int16_t)(lsb * scale) - DISTANCE_OFFSET;
    glyph->offset_y = iy0 - DISTANCE_OFFSET;
    glyph->advance_x = (int16_t)(((float)advance) * scale + 0.5f);
    glyph->advance_y = (int16_t)((ascent - descent) * scale + 0.5f);
    glyph->u = 0;
    glyph->v = 0;

    int last_slot = F->priority[F->list_head].prev;
    struct priority_list *last_node = &F->priority[last_slot];
    if (last_node->version == F->version) {

        return -1;
    }

    F->dirty = 1;
    return 0;
}

static const char *font_manager_update(struct font_manager *F, int font_id, int codepoint,
                                        FontGlyph *glyph, uint8_t *buffer, int stride) {
    int idx = font_index(font_id);
    if (idx <= 0 || idx >= MAX_FONT_NUM || !F->fonts[idx].loaded) {
        return "Invalid font";
    }

    int cp = codepoint_key(font_id, codepoint);
    int slot = hash_lookup(F, cp);

    if (slot < 0) {

        slot = F->priority[F->list_head].prev;
        struct priority_list *last_node = &F->priority[slot];
        if (last_node->version == F->version) {
            return "Too many glyphs";
        }
        last_node->version = F->version;
        F->list_head = slot;
        F->slots[slot].codepoint_key = -1;
        hash_insert(F, cp, slot);
    }

    glyph->u = (slot % FONT_MANAGER_SLOTLINE) * FONT_MANAGER_GLYPHSIZE;
    glyph->v = (slot / FONT_MANAGER_SLOTLINE) * FONT_MANAGER_GLYPHSIZE;

    struct font_slot *s = &F->slots[slot];
    s->codepoint_key = cp;
    s->offset_x = glyph->offset_x;
    s->offset_y = glyph->offset_y;
    s->advance_x = glyph->advance_x;
    s->advance_y = glyph->advance_y;
    s->w = glyph->w;
    s->h = glyph->h;

    const stbtt_fontinfo *fi = &F->fonts[idx].info;
    float scale = stbtt_ScaleForMappingEmToPixels(fi, ORIGINAL_SIZE);

    int width, height, xoff, yoff;
    unsigned char *tmp = stbtt_GetCodepointSDF(fi, scale, codepoint,
                                                DISTANCE_OFFSET, ONEDGE_VALUE, PIXEL_DIST_SCALE,
                                                &width, &height, &xoff, &yoff);

    if (tmp == NULL) {

        return NULL;
    }

    const uint8_t *src = tmp;
    buffer += stride * glyph->v + glyph->u;

    int src_stride = width;
    if (width > FONT_MANAGER_GLYPHSIZE) width = FONT_MANAGER_GLYPHSIZE;
    if (height > FONT_MANAGER_GLYPHSIZE) height = FONT_MANAGER_GLYPHSIZE;
    if (width > glyph->w) width = glyph->w;
    if (height > glyph->h) height = glyph->h;

    int i;
    for (i = 0; i < height; i++) {
        memcpy(buffer, src, width);
        memset(buffer + width, 0, FONT_MANAGER_GLYPHSIZE - width);
        src += src_stride;
        buffer += stride;
    }
    for (; i < FONT_MANAGER_GLYPHSIZE; i++) {
        memset(buffer, 0, FONT_MANAGER_GLYPHSIZE);
        buffer += stride;
    }

    stbtt_FreeSDF(tmp, fi->userdata);

    return NULL;
}

static inline void scale_value(int16_t *v, int size) {
    *v = (*v * size + ORIGINAL_SIZE / 2) / ORIGINAL_SIZE;
}

static inline void uscale_value(uint16_t *v, int size) {
    *v = (*v * size + ORIGINAL_SIZE / 2) / ORIGINAL_SIZE;
}

void font_manager_scale(struct font_manager *F, FontGlyph *glyph, int size) {
    (void)F;
    scale_value(&glyph->offset_x, size);
    scale_value(&glyph->offset_y, size);
    scale_value(&glyph->advance_x, size);
    scale_value(&glyph->advance_y, size);
    uscale_value(&glyph->w, size);
    uscale_value(&glyph->h, size);
}

size_t font_manager_sizeof(void) {
    return sizeof(struct font_manager);
}

void font_manager_init(struct font_manager *F) {
    memset(F, 0, sizeof(*F));

    F->version = 1;
    F->count = 0;
    F->dpi_perinch = 96;
    F->dirty = 0;

    for (int i = 0; i < FONT_MANAGER_SLOTS; i++) {
        F->priority[i].prev = i + 1;
        F->priority[i].next = i - 1;
    }
    int lastslot = FONT_MANAGER_SLOTS - 1;
    F->priority[0].next = lastslot;
    F->priority[lastslot].prev = 0;
    F->list_head = lastslot;

    for (int i = 0; i < FONT_MANAGER_SLOTS; i++) {
        F->slots[i].codepoint_key = -1;
    }
    for (int i = 0; i < FONT_MANAGER_HASHSLOTS; i++) {
        F->hash[i] = -1;
    }

    for (int i = 0; i < MAX_FONT_NUM; i++) {
        F->fonts[i].loaded = 0;
        F->fonts[i].data = NULL;
    }
}

void font_manager_shutdown(struct font_manager *F) {

    for (int i = 0; i < MAX_FONT_NUM; i++) {
        if (F->fonts[i].data) {
            free(F->fonts[i].data);
            F->fonts[i].data = NULL;
        }
        F->fonts[i].loaded = 0;
    }
}

int font_manager_load_font(struct font_manager *F, const char *path, int font_id) {
    int idx = font_index(font_id);
    if (idx <= 0 || idx >= MAX_FONT_NUM) {
        fprintf(stderr, "[font_manager] Invalid font_id %d (must be 1-%d)\n", font_id, MAX_FONT_NUM - 1);
        return -1;
    }

    if (F->fonts[idx].data) {
        free(F->fonts[idx].data);
        F->fonts[idx].data = NULL;
        F->fonts[idx].loaded = 0;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[font_manager] Failed to open font: %s\n", path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        fclose(fp);
        fprintf(stderr, "[font_manager] Failed to allocate memory for font: %s\n", path);
        return -1;
    }

    if (fread(data, 1, size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        fprintf(stderr, "[font_manager] Failed to read font: %s\n", path);
        return -1;
    }
    fclose(fp);

    if (!stbtt_InitFont(&F->fonts[idx].info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(data);
        fprintf(stderr, "[font_manager] Failed to init font: %s\n", path);
        return -1;
    }

    F->fonts[idx].data = data;
    F->fonts[idx].loaded = 1;

    fprintf(stderr, "[font_manager] Loaded font[%d]: %s\n", font_id, path);
    return 0;
}

int font_manager_has_codepoint(struct font_manager *F, int font_id, int codepoint) {
    int idx = font_index(font_id);
    if (idx <= 0 || idx >= MAX_FONT_NUM || !F->fonts[idx].loaded) {
        return 0;
    }
    return stbtt_FindGlyphIndex(&F->fonts[idx].info, codepoint) != 0;
}

const char *font_manager_glyph_ex(struct font_manager *F, int font_id, int codepoint, int size, FontGlyph *g, FontGlyph *og_out) {
    FontGlyph og;

    int updated = font_manager_touch(F, font_id, codepoint, &og);

    *g = og;

    if (is_space_codepoint(codepoint)) {
        updated = 1;
        g->w = g->h = 0;
        og.w = og.h = 0;
    }

    font_manager_scale(F, g, size);

    if (updated == 0) {
        const char *err = font_manager_update(F, font_id, codepoint, &og,
                                               F->texture_buffer, FONT_MANAGER_TEXSIZE);
        if (err) {
            return err;
        }

        g->u = og.u;
        g->v = og.v;
    }

    if (og_out) {
        *og_out = og;
    }

    return NULL;
}

const char *font_manager_glyph(struct font_manager *F, int font_id, int codepoint, int size, FontGlyph *g) {
    return font_manager_glyph_ex(F, font_id, codepoint, size, g, NULL);
}

void font_manager_metrics(struct font_manager *F, int font_id, int size,
                          int *ascent, int *descent, int *line_gap) {
    const stbtt_fontinfo *fi = get_ttf(F, font_id);
    if (!fi) {
        *ascent = 0;
        *descent = 0;
        *line_gap = 0;
        return;
    }

    int a, d, g;
    if (!stbtt_GetFontVMetricsOS2(fi, &a, &d, &g)) {
        stbtt_GetFontVMetrics(fi, &a, &d, &g);
    }

    float scale = stbtt_ScaleForMappingEmToPixels(fi, ORIGINAL_SIZE);
    *ascent = (int)(a * scale * size / ORIGINAL_SIZE + 0.5f);
    *descent = (int)(d * scale * size / ORIGINAL_SIZE + 0.5f);
    *line_gap = (int)(g * scale * size / ORIGINAL_SIZE + 0.5f);
}

int font_manager_pixel_size(struct font_manager *F, int font_id, int point_size) {
    (void)font_id;
    const int default_dpi = 96;
    int dpi = F->dpi_perinch == 0 ? default_dpi : F->dpi_perinch;
    return (int)((point_size / 72.0f) * dpi + 0.5f);
}

int font_manager_underline(struct font_manager *F, int font_id, int size,
                           float *position, float *thickness) {
    const stbtt_fontinfo *fi = get_ttf(F, font_id);
    if (!fi) {
        return -1;
    }

    float scale = stbtt_ScaleForMappingEmToPixels(fi, (float)size);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(fi, &ascent, &descent, &line_gap);

    *position = descent * scale * 0.25f;
    *thickness = (float)size / 16.0f;

    return 0;
}

int font_manager_flush(struct font_manager *F) {
    int dirty = F->dirty;
    ++F->version;
    F->dirty = 0;
    return dirty;
}

const void *font_manager_texture(struct font_manager *F, int *sz) {
    *sz = FONT_MANAGER_TEXSIZE;
    return F->texture_buffer;
}

float font_manager_sdf_mask(struct font_manager *F) {
    (void)F;
    return ONEDGE_VALUE / 255.0f;
}

float font_manager_sdf_distance(struct font_manager *F, uint8_t num_pixel) {
    (void)F;
    return (num_pixel * PIXEL_DIST_SCALE) / 255.0f;
}
