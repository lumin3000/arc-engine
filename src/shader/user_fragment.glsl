// arc-engine default fragment extension — provides only generic
// pixel handling (tex_index 0..3, FLAG_background_pixels, col_override
// multiplies). Games that need extra branches (e.g. extra texture
// branches, post effects) supply their own user_fragment.glsl via
// consumer-side build rules that place the game copy into the shader
// build directory before invoking sokol-shdc. See arc-engine/CLAUDE.md §3.
//
// Contract inherited from shader_core.glsl:
//   - uniforms:  Shader_Data (bg_repeat_tex0_atlas_uv, batch_props,
//                col_override, col_override_2, params, ...)
//   - samplers:  tex0, font_tex (plus engine-provided: flow_map,
//                ripple_tex, noise_tex)
//   - varyings:  color, uv, uv_rect, extras, world_pos, ...
//   - output:    col_out

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

vec2 get_tiled_atlas_uv(vec2 uv_in) {
	vec4 rect = (length(uv_rect) < 0.001) ? vec4(0.0, 0.0, 1.0, 1.0) : uv_rect;
	vec2 fract_uv = fract(uv_in);
	return rect.xy + fract_uv * (rect.zw - rect.xy);
}

void main() {
	int tex_index = int(batch_props.x);
	int flags = int(extras.y * 255.0);

	if (tex_index == 255) {
		col_out = color * col_override * col_override_2;
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
