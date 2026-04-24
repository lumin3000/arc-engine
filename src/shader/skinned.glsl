// skinned.glsl - Skeletal animation shader for ozz-animation
// With diffuse texture support
// Compile: ./sokol-shdc --input src/shader/skinned.glsl --output src/bedrock/ozz/skinned_shader.h --slang metal_macos --format sokol

@ctype mat4 Matrix4
@ctype vec4 Vec4

@block skin_utils
void skinned_pos_nrm(in vec4 pos, in vec4 nrm, in vec4 skin_weights, in uvec4 skin_indices, out vec4 skin_pos, out vec4 skin_nrm) {
    skin_pos = vec4(0.0, 0.0, 0.0, 1.0);
    skin_nrm = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 weights = skin_weights / dot(skin_weights, vec4(1.0));
    ivec2 uv;
    vec4 xxxx, yyyy, zzzz;
    if (weights.x > 0.0) {
        uv = ivec2(3 * skin_indices.x, gl_InstanceIndex);
        xxxx = texelFetch(sampler2D(joint_tex, joint_smp), uv, 0);
        yyyy = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(1,0), 0);
        zzzz = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(2,0), 0);
        skin_pos.xyz += vec3(dot(pos,xxxx), dot(pos,yyyy), dot(pos,zzzz)) * weights.x;
        skin_nrm.xyz += vec3(dot(nrm,xxxx), dot(nrm,yyyy), dot(nrm,zzzz)) * weights.x;
    }
    if (weights.y > 0.0) {
        uv = ivec2(3 * skin_indices.y, gl_InstanceIndex);
        xxxx = texelFetch(sampler2D(joint_tex, joint_smp), uv, 0);
        yyyy = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(1,0), 0);
        zzzz = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(2,0), 0);
        skin_pos.xyz += vec3(dot(pos,xxxx), dot(pos,yyyy), dot(pos,zzzz)) * weights.y;
        skin_nrm.xyz += vec3(dot(nrm,xxxx), dot(nrm,yyyy), dot(nrm,zzzz)) * weights.y;
    }
    if (weights.z > 0.0) {
        uv = ivec2(3 * skin_indices.z, gl_InstanceIndex);
        xxxx = texelFetch(sampler2D(joint_tex, joint_smp), uv, 0);
        yyyy = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(1,0), 0);
        zzzz = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(2,0), 0);
        skin_pos.xyz += vec3(dot(pos,xxxx), dot(pos,yyyy), dot(pos,zzzz)) * weights.z;
        skin_nrm.xyz += vec3(dot(nrm,xxxx), dot(nrm,yyyy), dot(nrm,zzzz)) * weights.z;
    }
    if (weights.w > 0.0) {
        uv = ivec2(3 * skin_indices.w, gl_InstanceIndex);
        xxxx = texelFetch(sampler2D(joint_tex, joint_smp), uv, 0);
        yyyy = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(1,0), 0);
        zzzz = texelFetch(sampler2D(joint_tex, joint_smp), uv + ivec2(2,0), 0);
        skin_pos.xyz += vec3(dot(pos,xxxx), dot(pos,yyyy), dot(pos,zzzz)) * weights.w;
        skin_nrm.xyz += vec3(dot(nrm,xxxx), dot(nrm,yyyy), dot(nrm,zzzz)) * weights.w;
    }
}
@end

@vs vs_skinned
layout(binding=0) uniform vs_skinned_params {
    mat4 view_proj;
};

@image_sample_type joint_tex unfilterable_float
layout(binding=0) uniform texture2D joint_tex;
@sampler_type joint_smp nonfiltering
layout(binding=0) uniform sampler joint_smp;

in vec4 position;
in vec4 normal;
in uvec4 jindices;
in vec4 jweights;
in vec2 texcoord0;
in vec4 inst_xxxx;
in vec4 inst_yyyy;
in vec4 inst_zzzz;

out vec2 fs_uv;
out vec3 fs_nrm;

@include_block skin_utils

void main() {
    vec4 pos, nrm;
    skinned_pos_nrm(position, normal, jweights, jindices, pos, nrm);

    // transform to world space using per-instance model matrix (transposed 4x3)
    pos = vec4(dot(pos,inst_xxxx), dot(pos,inst_yyyy), dot(pos,inst_zzzz), 1.0);
    nrm = vec4(dot(nrm,inst_xxxx), dot(nrm,inst_yyyy), dot(nrm,inst_zzzz), 0.0);

    gl_Position = view_proj * pos;
    fs_uv = texcoord0;
    fs_nrm = nrm.xyz;
}
@end

@fs fs_skinned
layout(binding=1) uniform texture2D diffuse_tex;
layout(binding=1) uniform sampler diffuse_smp;

in vec2 fs_uv;
in vec3 fs_nrm;
out vec4 frag_color;

void main() {
    vec4 tex_color = texture(sampler2D(diffuse_tex, diffuse_smp), fs_uv);
    vec3 nrm = normalize(fs_nrm);

    // ---- Step 1: Half-Lambert toon shading (UTS2 style) ----
    // Light from upper-left-front, adapted for Arc top-down view
    vec3 light_dir = normalize(vec3(-0.3, 0.8, 0.5));

    // Half-Lambert: remap -1~1 to 0~1 for softer shading
    float halfLambert = 0.5 * dot(nrm, light_dir) + 0.5;

    // Feathered step: smooth transition between lit and shade
    float shade_threshold = 0.5;
    float shade_feather = 0.1;
    float lit = smoothstep(shade_threshold - shade_feather,
                           shade_threshold + shade_feather,
                           halfLambert);

    // Shade color: tint shadows slightly cool (not pure darken)
    vec3 shade_tint = vec3(0.6, 0.55, 0.7);
    vec3 shaded = tex_color.rgb * mix(shade_tint, vec3(1.0), lit);

    // ---- Outline ----
    float edge = abs(dot(nrm, vec3(0.0, -1.0, 0.0)));
    vec3 outline_color = vec3(0.08, 0.06, 0.1);
    float outline_blend = smoothstep(0.40, 0.50, edge);
    vec3 final_rgb = mix(outline_color, shaded, outline_blend);

    frag_color = vec4(final_rgb, tex_color.a);
}
@end

@program skinned vs_skinned fs_skinned
