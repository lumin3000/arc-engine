@header package core_user
@header import sg "bald:sokol/gfx"

@ctype vec4 Vec4
@ctype mat4 Matrix4

@vs vs
in vec3 position;
in vec4 color0;
in vec2 uv0;
in vec2 local_uv0;
in vec2 size0;
in vec4 extras0;
in vec4 uv_rect0;

out vec4 color;
out vec2 uv;
out vec2 local_uv;
out vec2 size;
out vec4 extras;
out vec4 uv_rect;

out vec3 pos;
out vec3 world_pos;

layout(binding=1) uniform VS_MVP {
	mat4 mvp;
	float use_mvp;
	float sway_head;
	float _pad2;
	float _pad3;
};

void main() {
	vec3 pos_sway = position;

	float sway_amount = extras0.z;
	if (sway_amount > 0.01 && sway_head > 0.0) {
		float phase = sway_head + position.x * 0.5;
		float sway_offset = sin(phase) * sway_amount * 0.08;
		pos_sway.x += sway_offset;
	}

	if (use_mvp > 0.5) {
		gl_Position = mvp * vec4(pos_sway, 1.0);
	} else {
		gl_Position = vec4(pos_sway, 1.0);
	}

	color = color0;
	uv = uv0;
	local_uv = local_uv0;
	size = size0;
	extras = extras0;
	uv_rect = uv_rect0;

	pos = gl_Position.xyz;

	if (use_mvp > 0.5) {
		world_pos = position;
	} else {
		world_pos = position;
	}
}
@end

@fs fs

layout(binding=0) uniform texture2D tex0;
layout(binding=1) uniform texture2D font_tex;
layout(binding=2) uniform texture2D flow_map;
layout(binding=3) uniform texture2D ripple_tex;
layout(binding=4) uniform texture2D noise_tex;

layout(binding=0) uniform sampler default_sampler;

in vec4 color;
in vec2 uv;
in vec2 local_uv;
in vec2 size;
in vec4 extras;
in vec4 uv_rect;

in vec3 pos;
in vec3 world_pos;

out vec4 col_out;

@include shader_utils.glsl

@include user_fragment.glsl

@end

@program quad vs fs

@vs vs_inst
in vec3 position;
in vec4 color0;
in vec2 uv0;
in vec2 local_uv0;
in vec2 size0;
in vec4 extras0;
in vec4 uv_rect0;

in vec4 inst_m0;
in vec4 inst_m1;
in vec4 inst_m2;
in vec4 inst_m3;
in vec4 inst_color;

out vec4 color;
out vec2 uv;
out vec2 local_uv;
out vec2 size;
out vec4 extras;
out vec4 uv_rect;

out vec3 pos;
out vec3 world_pos;

layout(binding=1) uniform VS_MVP {
	mat4 mvp;
	float use_mvp;
	float sway_head;
	float _pad2;
	float _pad3;
};

void main() {
    mat4 inst_m = mat4(inst_m0, inst_m1, inst_m2, inst_m3);
    vec3 pos_sway = position;

    float sway_amount = extras0.z;
    if (sway_amount > 0.01 && sway_head > 0.0) {
        vec4 world_p = inst_m * vec4(position, 1.0);
        float phase = sway_head + world_p.x * 0.5;
        float sway_offset = sin(phase) * sway_amount * 0.08;
        pos_sway.x += sway_offset;
    }

    if (use_mvp > 0.5) {
        gl_Position = mvp * inst_m * vec4(pos_sway, 1.0);
    } else {
        gl_Position = inst_m * vec4(pos_sway, 1.0);
    }

    color = color0 * inst_color;

    uv = uv0;
    local_uv = local_uv0;
    size = size0;
    extras = extras0;
    uv_rect = uv_rect0;

    pos = gl_Position.xyz;

    vec4 world_p = inst_m * vec4(position, 1.0);
    world_pos = world_p.xyz;
}
@end

@program quad_inst vs_inst fs
