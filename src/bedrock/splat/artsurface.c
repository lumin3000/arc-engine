#include "artsurface.h"
#include "../../../external/sokol/c/sokol_gfx.h"
#include "splat_shader.h"
#include "../../log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct ArtSurface {
    sg_buffer geometry;
    int count;
    float center[3], half[3];
    Splat_Vec4 pose;
    bool shown;
    struct ArtSurface *next, *previous;
};
static ArtSurface *surfaces;
static size_t bytes;
static sg_pipeline pipeline;
static sg_shader shader;
static void check(bool ok, const char *operation) {
    if(!ok) { LOG_ERROR("[artsurface] %s failed\n",operation); exit(1); }
}
static void setup(void) {
    shader=sg_make_shader(splat_post_enabled() ? artsurface_post_shader_desc(sg_query_backend()) : artsurface_shader_desc(sg_query_backend()));
    pipeline=sg_make_pipeline(&(sg_pipeline_desc){
        .shader=shader,
        .layout={.buffers={{.stride=sizeof(ArtSurfaceVertex)}},.attrs={
            [ATTR_artsurface_position]={.format=SG_VERTEXFORMAT_FLOAT3},
            [ATTR_artsurface_dark]={.format=SG_VERTEXFORMAT_UBYTE4N},
            [ATTR_artsurface_base]={.format=SG_VERTEXFORMAT_UBYTE4N},
            [ATTR_artsurface_light]={.format=SG_VERTEXFORMAT_UBYTE4N},
            [ATTR_artsurface_accent]={.format=SG_VERTEXFORMAT_UBYTE4N},
            [ATTR_artsurface_material]={.format=SG_VERTEXFORMAT_UBYTE4N}}},
        .depth={.pixel_format=SG_PIXELFORMAT_DEPTH,.compare=SG_COMPAREFUNC_LESS_EQUAL,.write_enabled=true},
        .color_count = splat_post_enabled() ? 3 : 2,.colors={{.pixel_format=SG_PIXELFORMAT_RGBA8},{.pixel_format=SG_PIXELFORMAT_RGBA8},{.pixel_format=SG_PIXELFORMAT_RGBA8}},
        .sample_count=1,.label="artsurface-pipeline"});
    check(sg_query_pipeline_state(pipeline)==SG_RESOURCESTATE_VALID,"pipeline creation");
}
ArtSurface *artsurface_create(const ArtSurfaceVertex *v, int n) {
    if(!n) return NULL;
    if(!pipeline.id) setup();
    ArtSurface *s=calloc(1,sizeof *s);
    check(s!=NULL,"surface allocation");
    const size_t size=(size_t)n*sizeof *v;
    s->geometry=sg_make_buffer(&(sg_buffer_desc){.data={v,size},.label="art-world-chunk"});
    check(sg_query_buffer_state(s->geometry)==SG_RESOURCESTATE_VALID,"geometry upload");
    bytes+=size;
    s->count=n;
    s->pose.v[2]=1;
    s->shown=true;
    for(int k=0;k<3;k++){
        float lo=v[0].position[k], hi=lo;
        for(int i=1;i<n;i++) { lo=fminf(lo,v[i].position[k]); hi=fmaxf(hi,v[i].position[k]); }
        s->center[k]=(lo+hi)*.5f; s->half[k]=(hi-lo)*.5f;
    }
    s->next=surfaces;
    if(surfaces) surfaces->previous=s;
    surfaces=s;
    return s;
}
void artsurface_pose(ArtSurface *s, float x, float z, float heading, bool shown) {
    if(!s) return;
    s->pose=(Splat_Vec4){{x,z,cosf(heading),sinf(heading)}};
    s->shown=shown;
}
void artsurface_destroy(ArtSurface *s) {
    if(!s) return;
    if(s->previous) s->previous->next=s->next; else surfaces=s->next;
    if(s->next) s->next->previous=s->previous;
    sg_destroy_buffer(s->geometry);
    bytes-=(size_t)s->count*sizeof(ArtSurfaceVertex);
    free(s);
}
bool artsurface_visible(void) { return surfaces!=NULL; }
size_t artsurface_bytes(void) { return bytes; }
static bool in_view(const ArtSurface *s, const Splat_Mat4 *v, Splat_Vec4 cam, float focal) {
    for(int axis=0;axis<2;axis++){
        const float *p=s->pose.v;
        float center=cam.v[axis]+focal*(v->v[axis]*p[0]+v->v[8+axis]*p[1]), radius=1;
        const float coeff[]={v->v[axis]*p[2]+v->v[8+axis]*p[3],v->v[4+axis],
                             -v->v[axis]*p[3]+v->v[8+axis]*p[2]};
        for(int k=0;k<3;k++){
            float a=coeff[k]*focal;
            center+=s->center[k]*a;
            radius+=s->half[k]*fabsf(a);
        }
        if(center+radius<0 || center-radius>cam.v[axis+2]) return false;
    }
    return true;
}
void artsurface_draw(const Splat_Mat4 *v, Splat_Vec4 cam, Splat_Vec4 dep, Splat_Vec4 zr, Splat_Vec4 tint) {
    if(!surfaces) return;
    vs_artsurface_params_t params={.view=*v,.cam=cam,.dep=dep,.zr=zr};
    const fs_artsurface_params_t colors={.surface_backdrop=tint};
    sg_apply_pipeline(pipeline);
    params.model_pose.v[2]=1;
    sg_apply_uniforms(UB_vs_artsurface_params,&SG_RANGE(params));
    sg_apply_uniforms(UB_fs_artsurface_params,&SG_RANGE(colors));
    for(ArtSurface *s=surfaces;s;s=s->next) {
        if(!s->shown || !in_view(s,v,cam,dep.v[2])) continue;
        if(memcmp(&params.model_pose,&s->pose,sizeof s->pose)){
            params.model_pose=s->pose;
            sg_apply_uniforms(UB_vs_artsurface_params,&SG_RANGE(params));
        }
        sg_apply_bindings(&(sg_bindings){.vertex_buffers={s->geometry}});
        sg_draw(0,s->count,1);
    }
}
