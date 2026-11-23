// Portions are ported from ink-studio by siliconjungle (MIT, Copyright (c) 2026 siliconjungle;
// full notice in src/bedrock/splat/LICENSE.ink-studio): the palette resolve is src/pixel-look.js
// PixelLook.glsl, the projection and falloff follow cli/render.js.
// splat.glsl — Gaussian splat rendering and palette pixel resolve.
//
// Programs:
//   splat    — one instanced quad per splat; the vertex stage projects the splat's 3D
//              covariance to a screen-space ellipse (orthographic view), the fragment stage
//              evaluates the Gaussian. Two colour targets: 0 = colour (blended by alpha),
//              1 = normalised depth in .r (blended by alpha for solid splats only); .g and .b
//              carry the own palette of the splats that drew the pixel (see fs_splat).
//   resolve  — palette resolve of a 2x samples target into the art grid: 2x2 palette
//              vote, ordered dither on the native picture, body-coloured outer contour. A pixel
//              drawn by splats with an own palette is resolved against that palette.
//   blit     — nearest-neighbour copy of a target into a viewport.
//
// Pixel coordinates in every pass are "y-up": row 0 is the lowest screen row. Storage rows
// differ per backend (origin_top_left), so passes convert with u_flip where they address
// texels or compute ordered-dither phases.

@ctype mat4 Splat_Mat4
@ctype vec4 Splat_Vec4

@block local_light_inputs
@image_sample_type lamp_cells unfilterable_float
layout(binding=1) uniform texture2D lamp_cells;
@sampler_type lamp_smp nonfiltering
layout(binding=1) uniform sampler lamp_smp;
@end

@block local_light
vec3 surface_normal(mat3 shape, vec3 towardsEye) {
    // Longest cofactor is perpendicular to the plane of the elliptical splat.
    vec3 a=cross(shape[0],shape[1]), b=cross(shape[1],shape[2]), c=cross(shape[2],shape[0]);
    vec3 n=dot(a,a)>dot(b,b)?a:b;
    if(dot(c,c)>dot(n,n)) n=c;
    return dot(n,towardsEye)<0.0 ? -normalize(n+vec3(0,1e-20,0)) : normalize(n+vec3(0,1e-20,0));
}
vec3 unpack_normal(float packedNormal) {
    int bits=int(packedNormal)-1;
    vec2 p=vec2(bits&255,(bits>>8)&255)/127.5-1.0;
    vec3 n=vec3(p,1.0-abs(p.x)-abs(p.y));
    n.xy+=mix(vec2(1),vec2(-1),greaterThanEqual(n.xy,vec2(0)))*max(0.0,-n.z);
    return normalize(n);
}
void lamp_shade(vec3 pos, mat3 shape, vec3 normal) {
    ivec2 cell=ivec2(floor(pos.xz)-lampOrigin.xy);
    if(any(lessThan(cell,ivec2(0))) || any(greaterThanEqual(cell,ivec2(lampFrame.xy)))) return;
    int width=textureSize(sampler2D(lamp_cells,lamp_smp),0).x;
    int header=1+cell.y*int(lampFrame.x)+cell.x;
    vec2 span=texelFetch(sampler2D(lamp_cells,lamp_smp),ivec2(header%width,header/width),0).xy;
    if(span.y<0.5) return;
    vec3 n=normal;
    if(dot(n,n)<0.1) n=surface_normal(shape,vec3(view[0][2],view[1][2],view[2][2]));
    vec3 energy=vec3(0);
    for(int i=0;i<int(span.y);i++){
        int index=int(span.x)+i;
        vec4 lamp=texelFetch(sampler2D(lamp_cells,lamp_smp),ivec2(index%width,index/width),0);
        vec3 delta=vec3(lamp.x,lampFrame.w,lamp.y)-pos;
        float distanceToLight=length(delta);
        float atten=max(0.0,1.0-distanceToLight/lamp.z);
        float direct=max(0.0,dot(n,delta/max(.001,distanceToLight)));
        int rgb=int(lamp.w);
        vec3 color=vec3(rgb>>16,(rgb>>8)&255,rgb&255)/255.0;
        float phase=lampFrame.z+dot(floor(lamp.xy),vec2(1.7,2.1));
        float flicker=1.0+.025*sin(5.0*phase);
        energy+=color*atten*atten*(.15+1.40*direct)*flicker;
    }
    if(max(energy.r,max(energy.g,energy.b))<.009) return;
    v_col.rgb+=min(energy,vec3(1.6))*(.14+.72*v_col.rgb);
    v_lit=1.0;
}
@end

@block art_projection
void project_surface(vec3 world, out float normalizedDepth) {
    vec3 eye = mat3(view) * world;
    float pz = eye.z + view[3][2];
    vec2 ndc = (dep.z * eye.xy + cam.xy) / cam.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, clamp((-pz - zr.x) * zr.y - zr.w, 0.0, 1.0), 1.0);
    normalizedDepth = clamp((-pz - (dep.x - dep.y)) / (2.0 * dep.y), 0.0, 1.0);
}
@end

@block splat_project
// Projects one splat (world position, density, covariance, packed colour) into the target and
// fills the flat outputs. Uniforms: view, cam, dep, zr (declared by the including stage).
void splat_project(int corner, vec3 pos, float al, float c00, float c01, float c02, float c11, float c12, float c22, float pk, vec3 groundUp) {
    bool ground = al == -5.0;  // SPLAT_GROUND_DAB
    if (ground) al = 4.0;
    v_lit=0.0;
    vec2 sgn = vec2((corner & 1) != 0 ? 1.0 : -1.0, (corner & 2) != 0 ? 1.0 : -1.0);
    vec3 xa = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 ya = vec3(view[0][1], view[1][1], view[2][1]);
    vec3 za = vec3(view[0][2], view[1][2], view[2][2]);
    float pz = dot(za, pos) + view[3][2];
    float focal = dep.z;
    vec3 a = focal * xa;
    vec3 b = focal * ya;
    vec3 u = vec3(c00 * a.x + c01 * a.y + c02 * a.z, c01 * a.x + c11 * a.y + c12 * a.z, c02 * a.x + c12 * a.y + c22 * a.z);
    vec3 w = vec3(c00 * b.x + c01 * b.y + c02 * b.z, c01 * b.x + c11 * b.y + c12 * b.z, c02 * b.x + c12 * b.y + c22 * b.z);
    float A = dot(a, u) + 0.15;
    float B = dot(b, u);
    float C = dot(b, w) + 0.15;
    float det = A * C - B * B;
    if (al <= 0.0 || pz > -dep.w || det <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_rel = vec2(0.0); v_conic = vec3(0.0); v_col = vec4(0.0); v_dn = 1.0;
        return;
    }
    // how many sigmas out the fragment stage can still draw: it drops everything past 3 sigma
    // (pw < -4.5), and a solid splat (al >= 1) wherever al * exp(pw) <= 0.38, where its
    // smoothstep is 0. Solid splats are most of a world and end well inside 3 sigma (2.17 at
    // density 4), so their quads are that much smaller and the fill with them.
    float k = al >= 1.0 ? min(3.0, sqrt(2.0 * log(al / 0.38))) : 3.0;
    vec2 ext = vec2(min(k * sqrt(A), 2048.0), min(k * sqrt(C), 2048.0));
    vec2 rel = focal * vec2(dot(xa, pos), dot(ya, pos));
    // the quad is a little larger than that so its edge never decides a fragment: the cuts in
    // the fragment stage do
    vec2 corner_px = rel + cam.xy + sgn * (ext * 1.02 + 0.5);
    vec2 ndc = corner_px / cam.zw * 2.0 - 1.0;
    // The Gaussian still has its elliptical footprint, but each corner lies on the
    // ground plane. Both the camera and sun pass interpolate the same surface depth.
    if (ground) {
        vec3 n = mat3(view) * groundUp;
        vec2 eyeOffset = sgn * (ext * 1.02 + 0.5) / focal;
        if (abs(n.z) > 0.0001) pz -= dot(n.xy, eyeOffset) / n.z;
    }
    // hardware depth: farther = larger, over the whole scene depth range
    float zd = clamp((-pz - zr.x) * zr.y - zr.w, 0.0, 1.0);
    gl_Position = vec4(ndc, zd, 1.0);
    v_rel = rel;
    v_conic = vec3(C / det, -B / det, A / det);
    v_col = vec4(mod(pk, 256.0) / 255.0, mod(floor(pk / 256.0), 256.0) / 255.0, floor(pk / 65536.0) / 255.0, ground ? -al : al);
    v_dn = clamp((-pz - (dep.x - dep.y)) / (2.0 * dep.y), 0.0, 1.0);
}

// SPLAT_FLAT_PATCH in splat.h. Project actual corners so intersecting faces share
// a straight edge and hardware depth varies continuously across the surface.
bool is_flat_patch(float density) { return density == -4.0; }
void project_patch(int vertex, vec3 center, vec3 halfExtent, mat3 rotation, float pk) {
    vec2 uv = vec2((vertex & 1) == 0 ? -1.0 : 1.0, (vertex & 2) == 0 ? -1.0 : 1.0);
    vec3 axes = halfExtent.x == 0.0 ? vec3(0, uv.y, uv.x) :
                halfExtent.y == 0.0 ? vec3(uv.x, 0, uv.y) : vec3(uv.x, uv.y, 0);
    vec3 world = center + rotation * (axes * halfExtent);
    project_surface(world, v_dn);
    v_rel = vec2(0);
    v_conic = vec3(0);
    vec3 rgb = vec3(mod(pk, 256.0), mod(floor(pk / 256.0), 256.0), floor(pk / 65536.0));
    v_col = vec4(rgb / 255.0, 4.0);
    v_lit = 0.0;
}
@end

@block vertex_splat
layout(binding=0) uniform vs_splat_params {
    mat4 view;      // world -> view (column-major); only its rotation rows are used for x/y
    vec4 cam;       // x = screen offset Cx, y = screen offset Cy (target px), z = target width px, w = target height px
    vec4 dep;       // x = eye distance, y = depth radius, z = focal px, w = near
    vec4 zr;        // x = hardware depth near (view distance), y = 1 / depth range, w = how much nearer these
                    // splats are tested and written than they are (share of the depth range)
    vec4 lampOrigin;
    vec4 lampFrame; // map width, height, animation seconds, flame height
};
@include_block local_light_inputs

in vec4 a_pos;   // x y z density
in vec4 a_cov0;  // c00 c01 c02 packed rgb
in vec4 a_cov1;  // c11 c12 c22 kind

#ifdef ART_POST
in vec4 a_material;
#endif
// Everything the fragment stage needs is constant per splat (flat): the fragment's offset from
// the centre is formed from gl_FragCoord there, so a view moved by whole pixels reproduces
// every fragment bit for bit.
flat out vec2 v_rel;    // splat centre minus the screen offset, target px (y-up)
flat out vec3 v_conic;
flat out vec4 v_col;
flat out float v_lit;
out float v_dn;
#ifdef ART_POST
flat out vec4 v_receiver;
#endif

@include_block art_projection
@include_block splat_project
@include_block local_light

void main() {
    vec3 pos=a_pos.xyz;
    float flame=max(0.0,-a_cov1.w);
    if(flame>0.0) {
        float phase=lampFrame.z+dot(floor(pos.xz),vec2(1.7,2.1));
        pos.x+=flame*.035*sin(phase*5.0);
        pos.y+=flame*.045*sin(phase*7.0);
    }
    if (is_flat_patch(a_pos.w))
        project_patch(gl_VertexIndex, pos, sqrt(vec3(a_cov0.x, a_cov1.x, a_cov1.z)), mat3(1.0), a_cov0.w);
    else splat_project(gl_VertexIndex, pos, a_pos.w, a_cov0.x, a_cov0.y, a_cov0.z, a_cov1.x, a_cov1.y, a_cov1.z, a_cov0.w, vec3(0, 1, 0));
    if(flame==0.0 && lampFrame.x>0.0) {
        mat3 shape=mat3(a_cov0.xyz,vec3(a_cov0.y,a_cov1.xy),vec3(a_cov0.z,a_cov1.yz));
        lamp_shade(pos,shape,vec3(0));
    }
#ifdef ART_POST
    mat3 cov=mat3(a_cov0.xyz,vec3(a_cov0.y,a_cov1.xy),vec3(a_cov0.z,a_cov1.yz));
    vec3 normal=a_material.w>0.0?unpack_normal(a_material.w):surface_normal(cov,vec3(view[0][2],view[1][2],view[2][2]));
    vec3 vn=mat3(view)*normal;
    v_receiver=vec4(vn.z<0.0?-vn.xy:vn.xy,0,flame>0.0 ? 127.0 : 1.0+a_material.x+(a_material.x>0.0 ? .125*(1.0+a_material.y) : 0.0));
#endif
}
@end
@vs vs_splat
@include_block vertex_splat
@end
@vs vs_splat_post
#define ART_POST 1
@include_block vertex_splat
@end

@block vertex_splat_skin
layout(binding=0) uniform vs_splat_params {
    mat4 view;
    vec4 cam;
    vec4 dep;
    vec4 zr;        // z = bone texture row of this instance
    vec4 lampOrigin;
    vec4 lampFrame;
};
@include_block local_light_inputs
@image_sample_type bone_tex unfilterable_float
layout(binding=0) uniform texture2D bone_tex;
@sampler_type bone_smp nonfiltering
layout(binding=0) uniform sampler bone_smp;

in vec4 a_pos;   // x y z density (rest pose)
in vec4 a_cov0;  // c00 c01 c02 packed rgb
in vec4 a_cov1;  // c11 c12 c22 kind
in vec4 a_bone;  // bone, second bone, second bone share, unused

flat out vec2 v_rel;
flat out vec3 v_conic;
flat out vec4 v_col;
flat out float v_lit;
out float v_dn;
#ifdef ART_POST
flat out vec4 v_receiver;
#endif

@include_block art_projection
@include_block splat_project
@include_block local_light

// bone b of this instance: rows of [R | t]
void bone_rows(int b, out vec4 r0, out vec4 r1, out vec4 r2) {
    int row = int(zr.z);
    r0 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3, row), 0);
    r1 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3 + 1, row), 0);
    r2 = texelFetch(sampler2D(bone_tex, bone_smp), ivec2(b * 3 + 2, row), 0);
}

void main() {
    vec4 r0, r1, r2;
    bone_rows(int(a_bone.x), r0, r1, r2);
    vec4 p = vec4(a_pos.xyz, 1.0);
    vec3 x = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    if (a_bone.z > 0.0) {
        vec4 s0, s1, s2;
        bone_rows(int(a_bone.y), s0, s1, s2);
        x += (vec3(dot(s0, p), dot(s1, p), dot(s2, p)) - x) * a_bone.z;
    }
    // shape follows the first bone: R S R^T (as K.bones.posed)
    mat3 S = mat3(a_cov0.x, a_cov0.y, a_cov0.z, a_cov0.y, a_cov1.x, a_cov1.y, a_cov0.z, a_cov1.y, a_cov1.z);
    mat3 R = transpose(mat3(r0.xyz, r1.xyz, r2.xyz));
    mat3 Sp = R * S * transpose(R);
    if (is_flat_patch(a_pos.w))
        project_patch(gl_VertexIndex, x, sqrt(vec3(a_cov0.x, a_cov1.x, a_cov1.z)), R, a_cov0.w);
    else splat_project(gl_VertexIndex, x, a_pos.w, Sp[0][0], Sp[0][1], Sp[0][2], Sp[1][1], Sp[1][2], Sp[2][2], a_cov0.w, R[1]);
    if(lampFrame.x>0.0) {
        vec3 n=a_bone.w>0.0 ? normalize(R*unpack_normal(a_bone.w)) : vec3(0);
        lamp_shade(x,Sp,n);
    }
#ifdef ART_POST
    vec3 normal=a_bone.w>0.0?normalize(R*unpack_normal(a_bone.w)):surface_normal(Sp,vec3(view[0][2],view[1][2],view[2][2]));
    vec3 vn=mat3(view)*normal;
    v_receiver=vec4(vn.z<0.0?-vn.xy:vn.xy,0,1.0+floor(a_cov1.w/16.0));
#endif
}
@end
@vs vs_splat_skin
@include_block vertex_splat_skin
@end
@vs vs_splat_skin_post
#define ART_POST 1
@include_block vertex_splat_skin
@end

@block body_splat
layout(binding=1) uniform fs_splat_params {
    vec4 frag;      // x = screen offset Cx, y = Cy, z = target height px, w = 1 if storage origin is top-left
    vec4 mode;      // x = 1: opaque cut (alpha >= 0.5 drawn whole, the rest dropped), for splats drawn out of order;
                    // y = own palette of these splats (0 = the scene's)
    vec4 tint;      // rgb = colour the splats are pulled towards, a = how far (0 = their own colour)
};
flat in vec2 v_rel;
flat in vec3 v_conic;
flat in vec4 v_col;
flat in float v_lit;
in float v_dn;
#ifdef ART_POST
flat in vec4 v_receiver;
#endif
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_depth;
#ifdef ART_POST
layout(location=2) out vec4 frag_geometry;
layout(location=3) out vec4 frag_surface;
#endif

void main() {
    vec2 fc = vec2(gl_FragCoord.x, frag.w > 0.5 ? frag.z - gl_FragCoord.y : gl_FragCoord.y);
    vec2 d = (fc - frag.xy) - v_rel;
    float dx = d.x, dy = d.y;
    float pw = -0.5 * (v_conic.x * dx * dx + v_conic.z * dy * dy) - v_conic.y * dx * dy;
    if (pw > 0.0 || pw < -4.5) discard;
    bool ground = v_col.a < 0.0;
    float al = abs(v_col.a);
    float gg = al * exp(pw);
    float a = min(0.99, gg);
    bool solid = al >= 1.0;
    if (solid) a = 0.99 * smoothstep(0.38, 0.62, gg);
    if (a < 0.004) discard;
    // Adjacent ground footprints overlap on the same plane. A translucent rim
    // can otherwise write depth before the neighbour fills in the background.
    if (ground || mode.x > 0.5) {
        if (a < 0.5) discard;
        a = 1.0;
    }
    #ifdef ART_POST
    frag_geometry=vec4(0);
    frag_surface=vec4(v_receiver.xy,2.0*v_dn-1.0,v_receiver.w);
    #endif
    frag_color = vec4(mix(min(v_col.rgb, vec3(1.0)), tint.rgb, tint.a), a);
    // .g = own palette number in steps of 8/255, .b = 1 for an own palette: blended like the depth, so
    // .b is the share of the pixel that splats with an own palette drew and .g / .b their palette
    frag_depth = vec4(v_dn, (mode.y*8.0+(mode.y>0.0 ? 3.0*v_lit : 0.0))/255.0, mode.y > 0.0 ? 1.0 : 0.0, solid ? a : 0.0);
}
@end
@fs fs_splat

@include_block body_splat
@end
@fs fs_splat_post
#define ART_POST 1

@include_block body_splat
@end

@program splat vs_splat fs_splat
@program splat_post vs_splat_post fs_splat_post
@program splat_skin vs_splat_skin fs_splat
@program splat_skin_post vs_splat_skin_post fs_splat_post

@vs vs_quad
layout(binding=0) uniform vs_quad_params {
    vec4 quad;   // x = 1 if texture storage origin is top-left
};
out vec2 v_uv;
void main() {
    int i = gl_VertexIndex;
    vec2 ndc = vec2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
    gl_Position = vec4(ndc, 0.5, 1.0);
    vec2 uv = ndc * 0.5 + 0.5;
    v_uv = vec2(uv.x, quad.x > 0.5 ? 1.0 - uv.y : uv.y);
}
@end

@fs fs_blit
layout(binding=0) uniform texture2D blit_tex;
layout(binding=0) uniform sampler blit_smp;
@image_sample_type blit_depth unfilterable_float
layout(binding=1) uniform texture2D blit_depth;
@sampler_type blit_dsmp nonfiltering
layout(binding=1) uniform sampler blit_dsmp;
// occluders: things the host drew before the composite (e.g. 2D characters). Inside an
// occluder's rectangle (art pixels, y-up), splat pixels farther than the occluder's depth are
// dropped so the host's drawing shows through; nearer ones cover it.
layout(binding=1) uniform fs_blit_params {
    vec4 occ_rect[32];    // x0, y0, x1, y1 in art pixels (y-up)
    vec4 occ_depth[32];   // x = normalised depth of the occluder's foot
    vec4 occ_info;        // x = count, y = art height, z = 1 if storage origin is top-left, w = samples height
    vec4 blit_opts;       // x = 1: the copy covers what is behind it whatever the target's alpha
};
in vec2 v_uv;
out vec4 frag_color;
void main() {
    vec4 c = texture(sampler2D(blit_tex, blit_smp), v_uv);
    int n = int(occ_info.x);
    if (n > 0 && c.a > 0.0) {
        vec2 art = vec2(textureSize(sampler2D(blit_tex, blit_smp), 0));
        // art pixel under this fragment, y-up
        vec2 a = vec2(v_uv.x, occ_info.z > 0.5 ? 1.0 - v_uv.y : v_uv.y) * art;
        ivec2 ap = ivec2(floor(a));
        float d = 1.0;
        for (int j = 0; j < 4; j++) {
            ivec2 q = ap * 2 + ivec2(j & 1, j >> 1);
            q.y = occ_info.z > 0.5 ? int(occ_info.w) - 1 - q.y : q.y;
            d = min(d, texelFetch(sampler2D(blit_depth, blit_dsmp), q, 0).r);
        }
        for (int i = 0; i < 32; i++) {
            if (i >= n) break;
            vec4 r = occ_rect[i];
            if (a.x >= r.x && a.x < r.z && a.y >= r.y && a.y < r.w) {
                float od = occ_depth[i].x;
                if (d > od) discard;
            }
        }
    }
    frag_color = blit_opts.x > 0.5 ? vec4(c.rgb, 1.0) : c;
}
@end

@program blit vs_quad fs_blit

@block warm_palette
// Keep the native luminance choices; add the chroma of a lit sample in small discrete steps.
vec3 lit_palette(vec3 entry,vec3 sampleColor) {
    vec3 d=sampleColor-entry;
    vec3 chroma=d-vec3(dot(d,vec3(.30,.55,.15)));
    return clamp(entry+round(64.0*chroma)/64.0,0.0,1.0);
}
@end
// Surface-buffer values 2..65 identify palette ramps and 127 identifies emissive
// pixels. Natural ground keeps its base colour through palette resolve, then lights normally.
@block natural_ground_tag
const float NATURAL_GROUND = 126.0;
@end

@block body_resolve
#ifdef ART_POST
@include_block natural_ground_tag
#endif
layout(binding=1) uniform fs_resolve_params {
    vec4 pal[64];
    vec4 opts;   // x = palette size, y = dither, z = flip (storage origin top-left), w = outline darkening (0 = body-coloured contour)
    vec4 size;   // x, y = art grid size, z, w = samples size
    vec4 phase;  // x, y = ordered-dither phase of art pixel (0,0); z = contour depth step (normalised); w = 1: transparent background
    mat4 camera;
    mat4 lightCamera;
    vec4 reconstruct; // world width per native pixel, camera eye distance, depth radius, live-sun flag
    vec4 shadowGrid;
    vec4 sunVector;
    vec4 skyTint;
    vec4 weather; // rain, cloud opacity, animation seconds, lamp height
    vec4 lightRect;
    vec4 ramps[64];
    vec4 effect; // depth outline, convex ridge, bloom, grade
    vec4 depth_scale; // normalised depth per unit
    vec4 bloom_grid;  // x, y = offset (0..3) of the world-anchored bloom blocks in native pixels
    vec4 own[16]; // own palette k (1..15): x = size, y = dither
};
@image_sample_type u_scene unfilterable_float
layout(binding=0) uniform texture2D u_scene;
@image_sample_type u_depth unfilterable_float
layout(binding=1) uniform texture2D u_depth;
@image_sample_type u_native unfilterable_float
layout(binding=2) uniform texture2D u_native;
// own palettes: row k holds palette k (row 0 unused), one colour a texel
@image_sample_type u_pal unfilterable_float
layout(binding=3) uniform texture2D u_pal;
#ifdef ART_POST
layout(binding=4) uniform texture2D u_geometry;
layout(binding=5) uniform texture2D u_half;
layout(binding=6) uniform texture2D u_quarter;
@image_sample_type u_lamps unfilterable_float
layout(binding=7) uniform texture2D u_lamps;
@image_sample_type u_surface unfilterable_float
layout(binding=8) uniform texture2D u_surface;
@image_sample_type u_shadow unfilterable_float
layout(binding=9) uniform texture2D u_shadow;
layout(binding=10) uniform texture2D u_snow;
#endif
@sampler_type u_smp nonfiltering
layout(binding=0) uniform sampler u_smp;
in vec2 v_uv;
out vec4 frag_color;

@include_block warm_palette
// colour i of palette k (0 = the scene's)
vec3 pixelMapped(int k, int i) {
    if (k == 0) return pal[i].rgb;
    return texelFetch(sampler2D(u_pal, u_smp), ivec2(i, k), 0).rgb;
}
int pixelNearest(vec3 c, bool solid, int k) {
    int n = int(k == 0 ? opts.x : own[k].x);
    int best = solid && n > 1 ? 1 : 0;
    float bd = 1e9;
    for (int i = 0; i < 64; i++) {
        if (i >= n) break;
        if (solid && i == 0 && n > 1) continue;
        vec3 df = c - pixelMapped(k, i);
        float d = dot(df * df, vec3(0.30, 0.55, 0.15));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
#ifdef ART_POST
// A host port of KIT.light.glsl (Ink Studio, MIT). The ramp contains actual
// world palette indices. Receivers and casters use the same light-space view.
float sunLevel(vec3 n,float visibility){
    float daylight=sunVector.w;
    return .13+.23*(.5+.5*n.y)+.69*daylight*visibility*max(dot(n,sunVector.xyz),0.0);
}
vec3 fragmentLamps(vec3 world,vec3 normal){
    ivec2 cell=ivec2(floor(world.xz))-ivec2(lightRect.xy);
    if(any(lessThan(cell,ivec2(0))) || any(greaterThanEqual(cell,ivec2(lightRect.zw)))) return vec3(0);
    int width=textureSize(sampler2D(u_lamps,u_smp),0).x;
    int header=1+cell.y*int(lightRect.z)+cell.x;
    vec2 span=texelFetch(sampler2D(u_lamps,u_smp),ivec2(header%width,header/width),0).xy;
    vec3 energy=vec3(0);
    for(int i=0;i<int(span.y);i++){
        int id=int(span.x)+i;
        vec4 lamp=texelFetch(sampler2D(u_lamps,u_smp),ivec2(id%width,id/width),0);
        vec3 delta=vec3(lamp.x,weather.w,lamp.y)-world;
        float dist=length(delta);
        float fade=max(0.0,1.0-dist/lamp.z);
        float flicker=1.0+.025*sin(5.0*(weather.z+dot(floor(lamp.xy),vec2(1.7,2.1))));
        int rgb=int(lamp.w);
        vec3 lampColor=vec3(rgb>>16,(rgb>>8)&255,rgb&255)/255.0;
        energy+=lampColor*fade*fade*(.20+1.15*max(dot(normal,delta/max(.01,dist)),0.0))*flicker;
    }
    return energy;
}
vec3 daylightColor(vec3 base,int ownPalette,ivec2 pixel,vec4 surface){
    if(surface.w==NATURAL_GROUND) surface.w=1.0;
    if(surface.w<.5 || surface.w>126.0) return base;
    vec3 eye=vec3((vec2(pixel)+.5-size.xy*.5)*reconstruct.x-camera[3].xy,
                   -reconstruct.y+-surface.z*reconstruct.z-camera[3].z);
    mat3 toWorld=transpose(mat3(camera));
    vec3 world=toWorld*eye;
    vec3 n=normalize(toWorld*vec3(surface.xy,sqrt(max(0.0,1.0-dot(surface.xy,surface.xy)))));
    float shadow=1.0;
    if(sunVector.w>.01){
        vec3 l=(lightCamera*vec4(world,1)).xyz;
        vec2 uv=.5+l.xy*shadowGrid.x;
        if(opts.z>.5) uv.y=1.0-uv.y;
        if(all(greaterThan(uv,vec2(0))) && all(lessThan(uv,vec2(1)))){
            float receiver=(-l.z-shadowGrid.y)*shadowGrid.z;
            // One shadow texel covers a longer stretch of surface at grazing incidence.
            float cosine=clamp(dot(n,sunVector.xyz),.12,1.0);
            float slope=sqrt(1.0-cosine*cosine)/cosine;
            float bias=.00018+(.045+.0006*reconstruct.z+1.4*slope*shadowGrid.w/shadowGrid.x)*shadowGrid.z;
            for(int j=0;j<4;j++){
                vec2 d=vec2(float(j&1)-.5,float(j>>1)-.5)*shadowGrid.w;
                vec4 sampleData=textureLod(sampler2D(u_shadow,u_smp),uv+d,0.0);
                if(sampleData.w>.5 && receiver>.5+.5*sampleData.z+bias) shadow-=.25;
            }
        }
    }
    float cloud=.5+.5*sin(world.x*.12+world.z*.06+weather.z*.07+.8*sin(world.z*.11-weather.z*.04));
    float sunShare=shadow*(1.0-.52*weather.y*smoothstep(.33,.79,cloud));
    vec3 lamps=fragmentLamps(world,n);
    float lampLevel=max(lamps.r,max(lamps.g,lamps.b));
    float level=sunLevel(n,sunShare)+lampLevel*.63;
    // Authored materials name their ramp; older geometry starts at its nearest base colour.
    int mat=surface.w>=2.0 ? int(floor(surface.w))-2 : pixelNearest(base,false,0);
    vec4 row=ramps[clamp(mat,0,63)];
    vec3 ramp=row.xyz;
    // Captured delta mode: the reference sun is calibrated to the middle stop.
    // Static v2 dabs carry their reference stop in the fractional part of the tag.
    float packedStop=fract(surface.w)*8.0;
    float stop=packedStop>.5 ? packedStop-1.0 : row.w;
    if(surface.w>=2.0) level+=(stop-1.0)/3.0;
    int stepIndex=int(clamp(floor(level*3.0),0.0,2.0));
    vec3 col;
    if(ownPalette==0) col=pixelMapped(0,int(ramp[stepIndex]+.5));
    else col=pixelMapped(ownPalette,pixelNearest(base*(.67+.53*level),false,ownPalette));
    ivec2 snowCell=ivec2(floor(world.xz)),extent=textureSize(sampler2D(u_snow,u_smp),0);
    if(all(greaterThanEqual(snowCell,ivec2(0)))&&all(lessThan(snowCell,extent))){
        float depth=texelFetch(sampler2D(u_snow,u_smp),snowCell,0).r;
        col=mix(col,pixelMapped(0,51),smoothstep(.30,1.0,n.y)*depth);
    }
    float wet=weather.x*(.35+.65*max(n.y,0.0));
    col*=1.0-.21*wet;
    col=mix(col,skyTint.rgb,skyTint.a*(1.0-min(lampLevel,1.0)));
    return col+min(lamps,vec3(1.4))*(.13+.38*col);
}
#endif
// own palette of a samples texel of the depth target (0 = the scene's): see fs_splat
int ownOf(vec4 d) { return d.b > 0.5 ? int(d.g / d.b * (255.0 / 8.0) + 0.5) : 0; }
// y-up pixel coordinate -> storage texel coordinate
ivec2 stor(ivec2 p, float h) { return ivec2(p.x, opts.z > 0.5 ? int(h) - 1 - p.y : p.y); }
ivec2 clampS(ivec2 p) { return clamp(p, ivec2(0), ivec2(size.zw) - 1); }
float pixelDepth(ivec2 p) {
    ivec2 q = p * 2; float d = 1.0;
    for (int y = 0; y < 2; y++) for (int x = 0; x < 2; x++)
        d = min(d, texelFetch(sampler2D(u_depth, u_smp), stor(clampS(q + ivec2(x, y)), size.w), 0).r);
    return d;
}
float bayer(int i) {
    // 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
    int t[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return float(t[i]);
}
void main() {
    ivec2 fp = ivec2(gl_FragCoord.xy);
    ivec2 p = ivec2(fp.x, opts.z > 0.5 ? int(size.y) - 1 - fp.y : fp.y);
    // labels number the colours of every palette apart: palette * 64 + colour
    vec3 samples[4]; int labels[4]; int owns[4]; bool solid[4]; float coverage = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 q = stor(clampS(p * 2 + ivec2(i & 1, i >> 1)), size.w);
        samples[i] = texelFetch(sampler2D(u_scene, u_smp), q, 0).rgb;
        vec4 dq = texelFetch(sampler2D(u_depth, u_smp), q, 0);
        solid[i] = dq.r < 0.995;
        owns[i] = ownOf(dq);
        labels[i] = owns[i] * 64 + pixelNearest(samples[i], solid[i], owns[i]);
        if (solid[i]) coverage += 0.25;
    }
    // the pixel takes an own palette, and its dither, when at least two of its four samples do
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (owns[i] == 0) continue;
        int n = 0;
        for (int j = 0; j < 4; j++) if (owns[j] == owns[i]) n++;
        if (n >= 2) k = owns[i];
    }
    vec3 native = texelFetch(sampler2D(u_native, u_smp), stor(p, size.y), 0).rgb;
    ivec2 dp = p + ivec2(phase.xy);
    float noise = ((bayer((dp.x & 3) + ((dp.y & 3) << 2)) + 0.5) / 16.0 - 0.5) * (k == 0 ? opts.y : own[k].y);
#ifdef ART_POST
    vec4 surface=texelFetch(sampler2D(u_surface,u_smp),fp,0);
    if(surface.w==NATURAL_GROUND) noise=0.0;
#endif
    vec3 c = pixelMapped(k, pixelNearest(clamp(native + noise, 0.0, 1.0), false, k));
    int best = -1, score = 0;
    for (int i = 0; i < 4; i++) {
        if (!solid[i]) continue;
        int n = 0;
        for (int j = 0; j < 4; j++) if (solid[j] && labels[j] == labels[i]) n++;
        if (n > score) { best = labels[i]; score = n; }
    }
    bool clear = phase.w > 0.5 && best < 0;
    if (best >= 0) {
        bool outer = coverage < 1.0 || max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
            max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1)))) >= 0.995;
        if (outer) {
            vec3 body = pixelMapped(best / 64, best % 64);
            vec3 tone = body * 0.64;
            tone = min(vec3(1.0), tone * max(1.0, 0.12 / max(dot(tone, vec3(0.30, 0.55, 0.15)), 0.001)));
            vec3 edge = mix(body, tone, 0.65);
            edge = mix(edge, pixelMapped(0, 0), opts.w);
            // over a transparent background the contour keeps its own colour instead of fading to palette 0
            c = phase.w > 0.5 ? edge : mix(pixelMapped(0, 0), edge, coverage);
        } else if (opts.w > 0.0) {
            // dark contour where this pixel stands in front of a sharp step back
            float d0 = pixelDepth(p);
            float dn = max(max(pixelDepth(p - ivec2(1, 0)), pixelDepth(p + ivec2(1, 0))),
                           max(pixelDepth(p - ivec2(0, 1)), pixelDepth(p + ivec2(0, 1))));
            if (dn - d0 > phase.z) c = mix(c, pixelMapped(0, 0), opts.w);
        }
    }
    if(k>0) {
        vec4 meta=texelFetch(sampler2D(u_depth,u_smp),stor(clampS(p*2),size.w),0);
        if(meta.b>.5 && meta.g*255.0/meta.b-float(k*8)>1.5) c=lit_palette(c,native);
    }
#ifdef ART_POST
    if(reconstruct.w>.5 && !clear) c=daylightColor(c,k,p,surface);
    vec4 center=texelFetch(sampler2D(u_geometry,u_smp),fp,0);
    if((effect.x>0.0 || effect.y>0.0) && center.b+center.a>0.0) {
        vec2 n=center.rg*2.0-1.0;
        float z=dot(center.ba,vec2(256.0,1.0))*(255.0/65535.0);
        float ridge=0.0, edge=0.0;
        for(int i=0;i<4;i++){
            ivec2 delta=ivec2(i==0?1:(i==1?-1:0), i==2?1:(i==3?-1:0));
            ivec2 q=clamp(fp+delta,ivec2(0),ivec2(size.xy)-1);
            vec4 other=texelFetch(sampler2D(u_geometry,u_smp),q,0);
            // Splats clear the surface tag. Their contours continue to use the palette resolve.
            if(other.b+other.a==0.0) continue;
            float dz=(dot(other.ba,vec2(256.0,1.0))*(255.0/65535.0)-z)/depth_scale.x;
            edge=max(edge,smoothstep(.25,.65,dz));
            vec2 diff=other.rg*2.0-1.0-n;
            vec2 direction=vec2(float(delta.x),float(delta.y)*(opts.z>.5?-1.0:1.0));
            if(abs(dz)<.33) ridge=max(ridge,smoothstep(.20,.72,dot(diff,direction)));
        }
        c=mix(c,c*.45,edge*effect.x);
        c+=vec3(.15,.14,.11)*ridge*effect.y*(1.0-edge);
    }
    if(effect.z>0.0){
        vec2 half_at=(vec2(fp)+.5+mod(bloom_grid.xy,2.0))*.5;
        vec2 quarter_at=(half_at+floor(bloom_grid.xy*.5))*.5;
        vec3 h=textureLod(sampler2D(u_half,u_smp),half_at/vec2(textureSize(sampler2D(u_half,u_smp),0)),0.0).rgb;
        vec3 q=textureLod(sampler2D(u_quarter,u_smp),quarter_at/vec2(textureSize(sampler2D(u_quarter,u_smp),0)),0.0).rgb;
        c+=effect.z*(.35*h+.65*q);
    }
    c=mix(c,c*vec3(1.035,1.005,.975)+vec3(.006,.001,.003),effect.w);
#endif
    frag_color = vec4(c, clear ? 0.0 : 1.0);
}
@end
@fs fs_resolve
@include_block body_resolve
@end
@fs fs_resolve_post
#define ART_POST 1
@include_block body_resolve
@end

@program resolve vs_quad fs_resolve
@program resolve_post vs_quad fs_resolve_post

// The crop carries the visible contribution of just one own-palette model in
// depth.b. All other geometry participates in depth and colour blending.
@fs fs_detail
layout(binding=0) uniform texture2D detail_color;
layout(binding=1) uniform texture2D detail_depth;
layout(binding=2) uniform texture2D detail_palette;
@sampler_type detail_smp nonfiltering
layout(binding=0) uniform sampler detail_smp;
layout(binding=1) uniform fs_detail_params { vec4 palette; };
in vec2 v_uv;
out vec4 frag_color;
@include_block warm_palette
void main() {
    vec4 meta=texture(sampler2D(detail_depth, detail_smp), v_uv);
    float coverage=meta.b;
    if (coverage == 0.0) discard;
    vec3 source = texture(sampler2D(detail_color, detail_smp), v_uv).rgb;
    const int ranks[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    ivec2 q = ivec2(v_uv * vec2(textureSize(sampler2D(detail_color, detail_smp), 0)));
    source += ((float(ranks[(q.x & 3) + 4*(q.y & 3)]) + 0.5)/16.0 - 0.5) * palette.z;
    vec3 color = vec3(0.0);
    float best = 1e9;
    for (int i=0; i<int(palette.y); i++) {
        vec3 entry = texelFetch(sampler2D(detail_palette, detail_smp), ivec2(i,int(palette.x)),0).rgb;
        vec3 delta = source-entry;
        float error = dot(delta*delta,vec3(0.30,0.55,0.15));
        if (error < best) { best=error; color=entry; }
    }
    vec3 background = texelFetch(sampler2D(detail_palette, detail_smp), ivec2(0, int(palette.x)), 0).rgb;
    if (coverage < 1.0 && all(equal(color, background))) discard;
    if(meta.g*255.0/coverage-palette.x*8.0>1.5) color=lit_palette(color,source);
    frag_color = vec4(color, 1.0);
}
@end

@program detail vs_quad fs_detail

@block pack_normal
vec4 pack_surface(vec3 view_position,float depth){
    vec3 normal=normalize(cross(dFdx(view_position),dFdy(view_position)));
    if(normal.z<0.0) normal=-normal;
    // Reserve depth code zero for surfaces without a geometric normal.
    float code=1.0+round(clamp(depth,0.0,1.0)*65534.0);
    return vec4(normal.xy*.5+.5, floor(code/256.0)/255.0,mod(code,256.0)/255.0);
}
@end

@block vertex_artmesh
layout(binding=0) uniform vs_artmesh_params {
    mat4 view;
    vec4 cam;
    vec4 dep;
    vec4 zr;
    vec4 backdrop;
};
in vec3 position;
in vec4 color;
in vec4 row0;
in vec4 row1;
in vec4 row2;
in vec4 top;
in vec4 lit;
in vec4 shade;
#ifdef ART_POST
out vec3 mesh_view_position;
flat out float mesh_material;
#endif
out vec3 mesh_color;
out float mesh_depth;
@include_block art_projection
void main() {
    vec4 local = vec4(position, 1);
    vec3 world = vec3(dot(row0,local),dot(row1,local),dot(row2,local));
    vec3 face = color.a < 0.5/255.0 ? top.rgb : color.a < 1.5/255.0 ? lit.rgb : shade.rgb;
    mesh_color = mix(color.rgb * face, backdrop.rgb, backdrop.a);
#ifdef ART_POST
    mesh_view_position=mat3(view)*world;
    mesh_material=color.a>.99 ? 127.0 : (color.a*255.0>=3.0 ? round(color.a*255.0)-1.0 : 1.0);
#endif
    project_surface(world, mesh_depth);
}
@end
@vs vs_artmesh
@include_block vertex_artmesh
@end
@vs vs_artmesh_post
#define ART_POST 1
@include_block vertex_artmesh
@end

@block body_artmesh
#ifdef ART_POST
in vec3 mesh_view_position;
flat in float mesh_material;
#endif
in vec3 mesh_color;
in float mesh_depth;
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_depth;
#ifdef ART_POST
layout(location=2) out vec4 frag_geometry;
layout(location=3) out vec4 frag_surface;
#endif
void main() {
#ifdef ART_POST
    frag_geometry=pack_surface(mesh_view_position,mesh_depth);
    frag_surface=vec4(frag_geometry.rg*2.0-1.0,2.0*mesh_depth-1.0,mesh_material);
#endif
    frag_color = vec4(mesh_color,1);
    frag_depth = vec4(mesh_depth,0,0,1);
}
@end
@fs fs_artmesh

@include_block body_artmesh
@end
@fs fs_artmesh_post
#define ART_POST 1

@include_block pack_normal
@include_block body_artmesh
@end
@program artmesh vs_artmesh fs_artmesh
@program artmesh_post vs_artmesh_post fs_artmesh_post

@block vertex_artsurface
#ifdef ART_POST
@include_block natural_ground_tag
#endif
layout(binding=0) uniform vs_artsurface_params { mat4 view; vec4 cam; vec4 dep; vec4 zr; vec4 model_pose; };
in vec3 position;
in vec4 dark;
in vec4 base;
in vec4 light;
in vec4 accent;
in vec4 material;
#ifdef ART_POST
out vec3 surface_view_position;
flat out float surface_material;
#endif
out vec3 world;
out vec3 c0;
out vec3 c1;
out vec3 c2;
out vec3 c3;
out vec3 info;
out float surface_depth;
@include_block art_projection
void main() {
    world=position;
    c0=dark.rgb; c1=base.rgb; c2=light.rgb; c3=accent.rgb;
    info=material.rgb*255.0;
    vec2 p=model_pose.xy+vec2(position.x*model_pose.z-position.z*model_pose.w,position.x*model_pose.w+position.z*model_pose.z);
    vec3 placed=vec3(p.x,position.y,p.y);
#ifdef ART_POST
    surface_view_position=mat3(view)*placed;
    surface_material=info.x<.5 ? NATURAL_GROUND : material.a>.99 ? 127.0 : (material.a*255.0>=3.0 ? round(material.a*255.0)-1.0 : 1.0);
#endif
    project_surface(placed,surface_depth);
}
@end
@vs vs_artsurface
@include_block vertex_artsurface
@end
@vs vs_artsurface_post
#define ART_POST 1
@include_block vertex_artsurface
@end

@block body_artsurface
#ifdef ART_POST
in vec3 surface_view_position;
flat in float surface_material;
#endif
layout(binding=1) uniform fs_artsurface_params { vec4 surface_backdrop; };
in vec3 world;
in vec3 c0;
in vec3 c1;
in vec3 c2;
in vec3 c3;
in vec3 info;
in float surface_depth;
layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_depth;
#ifdef ART_POST
layout(location=2) out vec4 frag_geometry;
layout(location=3) out vec4 frag_surface;
#endif
float roll(vec2 xy) { return fract(sin(dot(xy,vec2(12.9898,78.233)))*4375.545); }
// A seam every `period` along `coord`, `width` wide; `span` is how far coord moves across one
// pixel. A seam thinner than a pixel would be drawn only where a pixel centre happens to land
// in it, so it is widened to one pixel. Where neighbouring seams come closer than
// SEAM_MIN_GAP pixels the whole family is left out instead of turning the face into stripes.
#define SEAM_MIN_GAP 5.0
bool seam(float coord, float span, float period, float width) {
    return period >= SEAM_MIN_GAP*span && mod(coord,period) < max(width,span);
}
void main() {
#ifdef ART_POST
    frag_geometry=pack_surface(surface_view_position,surface_depth);
    frag_surface=vec4(frag_geometry.rg*2.0-1.0,2.0*surface_depth-1.0,surface_material);
#endif
    vec2 p=world.xz;
    int kind=int(info.x+0.5);
    vec3 col=c1;
    float grain=roll(floor(p*12.0))-.5;
    float field=sin(dot(p,vec2(.19,.08)))+.7*cos(dot(p,vec2(-.11,.22)))+
        .32*sin(dot(p,vec2(.43,.70)))+.18*cos(dot(p,vec2(1.02,-.52)))+.08*grain;
    if(kind==1){
        int mask=int(info.z+.5);
        vec2 uv=fract(p);
        float bank=2.0;
        if((mask&1)!=0) bank=min(bank,uv.x);
        if((mask&2)!=0) bank=min(bank,1.0-uv.x);
        if((mask&4)!=0) bank=min(bank,uv.y);
        if((mask&8)!=0) bank=min(bank,1.0-uv.y);
        float crest=sin(p.x*1.62+p.y*.45+.65*sin(p.y*.45));
        float swell=sin(p.x*.67+p.y*.16+.73*sin(p.y*.21))+.42*sin(p.x*.24-p.y*.22);
        col=swell < -1.18 ? c0 : c1;
        if(crest>.973 && cos(p.y*1.19-p.x*.44)+.65*sin(p.x*.83+p.y*.34)>.63) col=c2;
        if(bank<.32 && sin(p.x*1.6+p.y*.85)>.3) col=c3;
        else if(bank<.55 && swell>.85) col=c2;
    } else if(kind==2){
        float row=floor(p.y), along=p.x+floor(roll(vec2(row,7))*16.0)/8.0;
        float wear=roll(vec2(row,floor(along/7.5)));
        col=wear<.045?c0:wear>.95?c2:c1;
        if(seam(p.y,fwidth(p.y),1.0,.063) || seam(along,fwidth(p.x),7.5,.068)) col=mix(col,c3,.36);
    } else if(kind>=6){
        float side=info.z>.5?world.z:world.x;
        float side_span=fwidth(side);
        if(kind==7){
            float course=floor(world.y/.575);
            if(seam(world.y,fwidth(world.y),.575,.01955) || seam(side+course,side_span,2.0,.018)) col*=.89;
        }
        if(kind==8 && seam(side,side_span,.25,.015)) col*=.82;
    } else if(kind==5){
        col=mix(c0,c2,clamp(1.0+world.y,0.0,1.0));
    } else if(kind!=0){
        col=field<-.82?c0:field>.97?c2:c1;
        if(kind==3){
            float course=floor(p.y*2.0);
            if(seam(p.y,fwidth(p.y),.5,.0325) || seam(p.x+course*.5,fwidth(p.x),1.0,.034)) col=mix(col,c0,.55);
        }
    }
    if(info.y>0.0 && kind!=1 && kind!=5){
        float snow=info.y/255.0;
        float cover=clamp(snow*1.6-.25+(.55*field+.27*grain)*(1.0-snow),0.0,1.0);
        col=mix(col,vec3(.89,.91,.94)+.025*field,cover);
    }
    frag_color=vec4(mix(col,surface_backdrop.rgb,surface_backdrop.a),1);
    frag_depth=vec4(surface_depth,0,0,1);
}
@end
@fs fs_artsurface

@include_block body_artsurface
@end
@fs fs_artsurface_post
#define ART_POST 1

@include_block pack_normal
@include_block body_artsurface
@end
@program artsurface vs_artsurface fs_artsurface
@program artsurface_post vs_artsurface_post fs_artsurface_post

@fs fs_bloom
layout(binding=1) uniform fs_bloom_params {
    vec4 blur; // threshold, knee, source inverse width and height
    vec4 grid; // x, y = offset of the world-anchored 2x2 blocks in source pixels; z, w = target size
};
layout(binding=0) uniform texture2D u_bright;
layout(binding=0) uniform sampler bloom_smp;
in vec2 v_uv;
out vec4 frag_color;
void main(){
    vec3 sum=vec3(0);
    for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){
        // Each target texel gathers the same world pixels wherever the camera stands.
        vec2 centre=(2.0*v_uv*grid.zw-grid.xy)*blur.zw;
        vec3 sample_color=textureLod(sampler2D(u_bright,bloom_smp),centre+vec2(float(x),float(y))*blur.zw*1.5,0.0).rgb;
        float lum=dot(sample_color,vec3(.30,.55,.15));
        float weight=float((2-abs(x))*(2-abs(y)))/16.0;
        sum+=weight*sample_color*(blur.y>0.0?smoothstep(blur.x,blur.x+blur.y,lum):1.0);
    }
    frag_color=vec4(sum,1);
}
@end
@program bloom vs_quad fs_bloom
