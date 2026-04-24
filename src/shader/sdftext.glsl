@vs vs
layout(binding=0) uniform vs_params {
    vec2 framesize;
    float texsize;
    float glyphsize;
};

in vec2 position;
in vec4 glyph_rect;
in vec4 glyph_offset;

out vec2 uv;

void main() {
    float base_u = glyph_rect.x;
    float base_v = glyph_rect.y;
    float glyph_w = glyph_rect.z;
    float glyph_h = glyph_rect.w;
    float offset_x = glyph_offset.x;
    float offset_y = glyph_offset.y;
    float tex_w = glyph_offset.z;
    float tex_h = glyph_offset.w;

    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));

    vec2 local_pos = corner * vec2(glyph_w, glyph_h);

    vec2 world_pos = position.xy + local_pos + vec2(offset_x, offset_y);
    vec2 clip_pos = world_pos * framesize;
    gl_Position = vec4(clip_pos.x - 1.0, 1.0 - clip_pos.y, 0.0, 1.0);

    vec2 tex_uv = corner * vec2(tex_w, tex_h);
    uv = (vec2(base_u, base_v) + tex_uv) * texsize;
}

@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

layout(binding=1) uniform fs_params {
    float edge_mask;
    float dist_multiplier;
    vec4 color;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    float dis = texture(sampler2D(tex, smp), uv).r;
    float smoothing = length(fwidth(uv)) * 128.0 * dist_multiplier;
    float alpha = smoothstep(edge_mask - smoothing, edge_mask + smoothing, dis);
    frag_color = vec4(color.rgb, color.a * alpha);
}
@end

@program sdftext vs fs
