
#ifndef ARC_SDF_TEXT_PIPELINE_H
#define ARC_SDF_TEXT_PIPELINE_H

#include <stdint.h>

struct font_manager;

void sdf_text_pipeline_init(void);

void sdf_text_pipeline_shutdown(void);

void sdf_text_begin_frame(void);

void sdf_text_add_glyph_ex(float x, float y,
                           int glyph_u, int glyph_v,
                           int glyph_w, int glyph_h,
                           int tex_w, int tex_h,
                           int offset_x, int offset_y,
                           uint32_t color);

void sdf_text_add_glyph(float x, float y,
                        int glyph_u, int glyph_v,
                        int glyph_w, int glyph_h,
                        int offset_x, int offset_y,
                        uint32_t color);

void sdf_text_flush(int screen_width, int screen_height, struct font_manager *font_mgr);

int sdf_text_flush_count(int screen_width, int screen_height, struct font_manager *font_mgr);

#include "font_manager.h"

void sdf_text_add_font_glyph(float x, float y, const FontGlyph *g, uint32_t color);

#endif
