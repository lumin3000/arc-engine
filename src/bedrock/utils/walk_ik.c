// Adapted from js/rig.js, Copyright (c) 2026 loktar00, MIT (LICENSE.rig).
#include "walk_ik.h"
#include <math.h>

void walk_place(WalkState *s, const WalkDimensions *d, float x, float z, float fx, float fz, float phase) {
    *s=(WalkState){.x=x,.z=z,.phase=phase,.hip=d->standing_hip};
    for(int i=0;i<2;i++){
        const float side=i?1:-1;
        s->foot[i]=(WalkFoot){.x=x+side*fz*d->hip_width,.z=z-side*fx*d->hip_width,
                              .planted=true,.facing_x=fx,.facing_z=fz};
    }
}
void walk_step(WalkState *s, const WalkDimensions *d, float x, float z, float fx, float fz, float dt) {
    const float mx=x-s->x,mz=z-s->z,distance=hypotf(mx,mz);
    s->phase+=distance/d->stride;s->phase-=floorf(s->phase);
    s->x=x;s->z=z;
    const float dx=distance>1.e-6f?mx/distance:fx,dz=distance>1.e-6f?mz/distance:fz;
    float far=0;
    for(int i=0;i<2;i++){
        WalkFoot *f=&s->foot[i];
        const float side=i?1:-1,hx=x+side*fz*d->hip_width,hz=z-side*fx*d->hip_width;
        const float t=s->phase+.5f*i-floorf(s->phase+.5f*i);
        if(t<d->duty){
            if(!f->planted){
                const float lead=(.5f*d->duty-t)*d->stride;
                f->x=hx+dx*lead;f->z=hz+dz*lead;
                f->facing_x=fx;f->facing_z=fz;f->planted=true;
            }
            f->height=0;
            far=fmaxf(far,hypotf(f->x-hx,f->z-hz));
        }else{
            if(f->planted){f->from_x=f->x;f->from_z=f->z;f->planted=false;}
            const float u=(t-d->duty)/(1-d->duty),ease=u*u*(3-2*u);
            const float lead=(1-t+.5f*d->duty)*d->stride;
            f->x=f->from_x+(hx+dx*lead-f->from_x)*ease;
            f->z=f->from_z+(hz+dz*lead-f->from_z)*ease;
            f->height=d->lift*sinf(3.14159265f*u);
            f->facing_x=fx;f->facing_z=fz;
        }
    }
    const float reach=d->thigh+d->shin-.003f;
    const float most=d->ankle+sqrtf(fmaxf(0,reach*reach-far*far));
    s->hip=fminf(most,s->hip+(fminf(d->standing_hip,most)-s->hip)*fminf(1,dt*14));
}

WalkPoint walk_joint(WalkPoint root, WalkPoint tip, float a, float b, WalkPoint hint) {
    WalkPoint axis={tip.x-root.x,tip.y-root.y,tip.z-root.z};
    const float length=sqrtf(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
    if(length<1.e-7f)axis=(WalkPoint){0,-1,0};
    else{axis.x/=length;axis.y/=length;axis.z/=length;}
    const float d=fminf(a+b-.00001f,fmaxf(fabsf(a-b)+.00001f,length));
    const float along=fminf(a,fmaxf(-a,(a*a+d*d-b*b)/(2*d)));
    const float project=hint.x*axis.x+hint.y*axis.y+hint.z*axis.z;
    WalkPoint bend={hint.x-project*axis.x,hint.y-project*axis.y,hint.z-project*axis.z};
    float n=sqrtf(bend.x*bend.x+bend.y*bend.y+bend.z*bend.z);
    if(n<1.e-6f){
        bend=fabsf(axis.y)<.9f?(WalkPoint){-axis.z,0,axis.x}:(WalkPoint){0,-axis.z,axis.y};
        n=sqrtf(bend.x*bend.x+bend.y*bend.y+bend.z*bend.z);
    }
    const float perp=sqrtf(fmaxf(0,a*a-along*along))/n;
    return (WalkPoint){root.x+axis.x*along+bend.x*perp,root.y+axis.y*along+bend.y*perp,
                       root.z+axis.z*along+bend.z*perp};
}
