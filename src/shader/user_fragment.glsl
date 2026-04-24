#define FLAG_background_pixels (1<<0)
#define FLAG_2 (1<<1)
#define FLAG_3 (1<<2)
bool has_flag(int flags, int flag) { return (flags & flag) != 0; }

layout(binding=0) uniform Shader_Data {
	mat4 ndc_to_world_xform;

	vec4 bg_repeat_tex0_atlas_uv;
    vec4 batch_props;
    vec4 col_override;
    vec4 col_override_2;
    vec4 params;
};

const float TERRAIN_TILE_SIZE = 8.0;

vec2 get_terrain_uv(vec2 wp) {
	return wp / TERRAIN_TILE_SIZE;
}

vec2 get_tiled_atlas_uv(vec2 uv) {
    vec4 rect = (length(uv_rect) < 0.001) ? vec4(0.0, 0.0, 1.0, 1.0) : uv_rect;

    vec2 fract_uv = fract(uv);

    return rect.xy + fract_uv * (rect.zw - rect.xy);
}

vec4 shader_terrain_hard(vec2 wp) {
	vec2 terrain_uv = get_terrain_uv(wp);
    vec2 atlas_uv = get_tiled_atlas_uv(terrain_uv);
	vec4 tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
	return tex_col * vec4(color.rgb, 1.0);
}

vec4 shader_terrain_fade(vec2 wp) {
	vec2 terrain_uv = get_terrain_uv(wp);
    vec2 atlas_uv = get_tiled_atlas_uv(terrain_uv);
	vec4 tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
	return vec4(tex_col.rgb * color.rgb, tex_col.a * color.a);
}

vec4 shader_terrain_fade_rough(vec2 wp) {
	vec2 terrain_uv = get_terrain_uv(wp);
    vec2 atlas_uv = get_tiled_atlas_uv(terrain_uv);
	vec4 tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
	float noise = fract(sin(dot(terrain_uv, vec2(12.9898, 78.233))) * 43758.5453);
	float rough_alpha = color.a * (0.8 + 0.4 * noise);
	rough_alpha = clamp(rough_alpha, 0.0, 1.0);
	return tex_col * vec4(color.rgb, rough_alpha);
}

vec4 shader_terrain_water(vec2 wp) {
	float time = params.x;

	vec2 terrain_uv = get_terrain_uv(wp);
	vec2 atlas_uv = get_tiled_atlas_uv(terrain_uv);
	vec3 base_color = texture(sampler2D(tex0, default_sampler), atlas_uv).rgb;

	vec2 water_uv = wp / TERRAIN_TILE_SIZE;

	float map_inv_size = params.y;
	vec2 flow_sample_uv = wp * map_inv_size;
	vec2 flow_raw = texture(sampler2D(flow_map, default_sampler), flow_sample_uv).rg;
	vec2 flow = flow_raw * 2.0 - 1.0;

	float flow_strength = length(flow);

	float phase0 = fract(time * 0.1);
	float phase1 = fract(time * 0.1 + 0.5);

	vec2 flow_uv0 = water_uv + flow * phase0 * 0.5;
	vec2 flow_uv1 = water_uv + flow * phase1 * 0.5;

	vec4 flow_ripple0 = texture(sampler2D(ripple_tex, default_sampler), flow_uv0);
	vec4 flow_ripple1 = texture(sampler2D(ripple_tex, default_sampler), flow_uv1);

	float flow_blend = abs(2.0 * phase0 - 1.0);
	vec4 flow_col = mix(flow_ripple0, flow_ripple1, flow_blend);

	vec2 still_uv1 = water_uv + vec2(time * 0.02, time * 0.01);
	vec4 still_ripple = texture(sampler2D(ripple_tex, default_sampler), still_uv1);

	vec2 still_uv2 = water_uv * 0.6 + vec2(-time * 0.015, time * 0.008);
	vec4 still_noise = texture(sampler2D(noise_tex, default_sampler), still_uv2);

	vec4 still_col = mix(still_ripple, still_noise, 0.3);

	float flow_factor = smoothstep(0.0, 0.1, flow_strength);
	vec4 tex_col = mix(still_col, flow_col, flow_factor);

	float ripple_luma = tex_col.r;
	float enhanced = smoothstep(0.3, 0.7, ripple_luma);
	vec3 final_col = base_color * (0.7 + 0.3 * enhanced);
	return vec4(final_col, color.a);
}

vec4 shader_terrain_hard_polluted(vec2 wp) {
	vec4 base = shader_terrain_hard(wp);
	vec4 pollution_tint = vec4(0.8, 1.0, 0.7, 1.0);
	return base * pollution_tint;
}

vec4 shader_terrain_fade_polluted(vec2 wp) {
	vec4 base = shader_terrain_fade(wp);
	vec4 pollution_tint = vec4(0.8, 1.0, 0.7, 1.0);
	return base * pollution_tint;
}

vec4 shader_terrain_fade_rough_polluted(vec2 wp) {
	vec4 base = shader_terrain_fade_rough(wp);
	vec4 pollution_tint = vec4(0.8, 1.0, 0.7, 1.0);
	return base * pollution_tint;
}

void main() {
	int tex_index = int(batch_props.x);
	int flags = int(extras.y * 255.0);

	if (tex_index == 255) {
		col_out = color * col_override * col_override_2;
		return;
	}

	if (tex_index == 254) {
		col_out = shader_terrain_fade(world_pos.xz);
		return;
	}

	if (tex_index == 253) {
		col_out = shader_terrain_hard(world_pos.xz);
		return;
	}

	if (tex_index == 252) {
		col_out = shader_terrain_fade_rough(world_pos.xz);
		return;
	}

	if (tex_index == 251) {
		col_out = shader_terrain_water(world_pos.xz);
		return;
	}

	if (tex_index == 250) {
		col_out = shader_terrain_hard_polluted(world_pos.xz);
		return;
	}

	if (tex_index == 249) {
		col_out = shader_terrain_fade_polluted(world_pos.xz);
		return;
	}

	if (tex_index == 248) {
		col_out = shader_terrain_fade_rough_polluted(world_pos.xz);
		return;
	}

	if (tex_index == 247) {
		col_out = shader_terrain_water(world_pos.xz);
		return;
	}

	if (tex_index == 5) {
		vec2 atlas_uv_ed = get_tiled_atlas_uv(uv);
		vec2 texel = vec2(params.z, params.w);

		float a_c = texture(sampler2D(tex0, default_sampler), atlas_uv_ed).a;
		float a_l = texture(sampler2D(tex0, default_sampler), atlas_uv_ed + vec2(-texel.x, 0.0)).a;
		float a_r = texture(sampler2D(tex0, default_sampler), atlas_uv_ed + vec2( texel.x, 0.0)).a;
		float a_u = texture(sampler2D(tex0, default_sampler), atlas_uv_ed + vec2(0.0, -texel.y)).a;
		float a_d = texture(sampler2D(tex0, default_sampler), atlas_uv_ed + vec2(0.0,  texel.y)).a;

		float edge = abs(4.0 * a_c - a_l - a_r - a_u - a_d);

		if (edge < 0.1) discard;

		float edge_alpha = clamp(edge * 2.0, 0.0, 1.0);
		col_out = color * col_override * col_override_2;
		col_out.a *= edge_alpha;
		return;
	}

	vec2 world_pixel = world_pos.xz;

    vec2 atlas_uv = get_tiled_atlas_uv(uv);

	vec4 tex_col = vec4(1.0);
	if (tex_index == 0) {
		tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
	} else if (tex_index == 1) {
		tex_col.a = texture(sampler2D(font_tex, default_sampler), uv).r;
	} else if (tex_index == 2) {
		tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
		if (tex_col.a < 0.5) discard;
	} else if (tex_index == 3) {
		tex_col = texture(sampler2D(tex0, default_sampler), atlas_uv);
	}

	col_out = tex_col;

	if (has_flag(flags, FLAG_background_pixels)) {
		float wrap_length = 128.0;
		vec2 wrapped_uv = world_pixel / wrap_length;
		wrapped_uv = local_uv_to_atlas_uv(wrapped_uv, bg_repeat_tex0_atlas_uv);
		vec4 img = texture(sampler2D(tex0, default_sampler), wrapped_uv);
		col_out.rgb = img.rgb;
	}

	col_out *= color;

	col_out *= col_override;

	col_out *= col_override_2;

}
