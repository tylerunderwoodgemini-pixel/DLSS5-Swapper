#pragma once
#include <d3d11.h>
#include <cstdint>
struct DepthRect { unsigned x=0,y=0,width=0,height=0,layer=0; bool operator==(const DepthRect &) const = default; };
struct DepthLayout { DepthRect eye[2]; bool valid=false; const char *method="unresolved"; };
inline bool VisibleExtent(unsigned allocation,unsigned visible){
    return allocation==visible || (visible>=2 && allocation==((visible+7u)&~7u));
}
inline bool StereoShape(const D3D11_TEXTURE2D_DESC &d,unsigned w,unsigned h){
    return w>=2 && !(w&1) && VisibleExtent(d.Height,h) && d.MipLevels==1 &&
        ((d.ArraySize==2 && VisibleExtent(d.Width,w/2)) || (d.ArraySize==1 && VisibleExtent(d.Width,w)));
}
inline bool RectInside(const DepthRect &r,const D3D11_TEXTURE2D_DESC &d){
    return r.width && r.height && r.layer<d.ArraySize && uint64_t(r.x)+r.width<=d.Width && uint64_t(r.y)+r.height<=d.Height;
}
inline DepthLayout ResolveDepthLayout(const D3D11_TEXTURE2D_DESC &d,unsigned w,unsigned h,const DepthRect *observed=nullptr,unsigned count=0){
    DepthLayout result; if(w<2 || (w&1) || !h || d.MipLevels!=1 || (d.ArraySize!=1&&d.ArraySize!=2))return result;
    const unsigned half=w/2;
    // Actual per-eye draw viewports take precedence over allocation heuristics.
    // They authorize arbitrary padding/offsets without treating unused pixels
    // as visible depth, and establish the two independent eye rectangles.
    if(count==2 && observed){
        auto a=observed[0],b=observed[1];
        if(a.width==half&&b.width==half&&a.height==h&&b.height==h&&RectInside(a,d)&&RectInside(b,d)){
            const bool separate=d.ArraySize==2&&a.layer==0&&b.layer==1;
            const bool horizontal=d.ArraySize==1&&a.y==b.y&&uint64_t(a.x)+half<=b.x;
            if(separate||horizontal){result.eye[0]=a;result.eye[1]=b;result.valid=true;result.method="observed-eye-viewports";return result;}
        }
    }
    if(StereoShape(d,w,h)){
        result.eye[0]={0,0,half,h,0};result.eye[1]={d.ArraySize==2?0:half,0,half,h,d.ArraySize==2?1u:0u};
        result.valid=true;result.method=d.Width==(d.ArraySize==2?half:w)&&d.Height==h?"exact-stereo":"aligned-visible-crop";
    }
    return result;
}
