#pragma once
#include <cstdint>
#include <cmath>
// In-process POD ABI shared by the OpenXR layer and ReShade add-on.
struct PoseEye { float q[4], position[3], projection[4]; };
struct PoseFrame {
    uint32_t abi=2, valid=0;
    uint64_t session=0, space=0, tick=0, epoch=0;
    int64_t display_time=0;
    PoseEye eye[2]={};
};
using ReadPoseFrame = bool (__cdecl *)(PoseFrame *, uint32_t);
// Submission metadata only: presence does not prove pixels are readable/usable.
struct DepthEyeInfo {
    uint64_t swapchain=0;
    uint32_t array_index=0;
    int32_t x=0,y=0,width=0,height=0;
    float min_depth=0,max_depth=0,near_z=0,far_z=0;
};
struct DepthSubmissionInfo {
    uint32_t abi=1,present_mask=0;
    uint64_t session=0,tick=0;
    int64_t display_time=0;
    DepthEyeInfo eye[2]={};
};
using ReadDepthSubmissionInfo = bool (__cdecl *)(DepthSubmissionInfo *, uint32_t);
inline bool ValidEye(const PoseEye &e) {
    float norm=0;
    for(float v:e.q){if(!std::isfinite(v))return false;norm+=v*v;}
    if(norm<0.9f || norm>1.1f)return false;
    for(float v:e.position)if(!std::isfinite(v))return false;
    for(float v:e.projection)if(!std::isfinite(v))return false;
    return e.projection[0]<e.projection[1] && e.projection[2]<e.projection[3];
}
inline void Rotation(const PoseEye &e,float *r) {
    float n=0;for(float q:e.q)n+=q*q;
    const float s=2/n,x=e.q[0],y=e.q[1],z=e.q[2],w=e.q[3];
    r[0]=1-s*(y*y+z*z);r[1]=s*(x*y-z*w);r[2]=s*(x*z+y*w);
    r[3]=s*(x*y+z*w);r[4]=1-s*(x*x+z*z);r[5]=s*(y*z-x*w);
    r[6]=s*(x*z-y*w);r[7]=s*(y*z+x*w);r[8]=1-s*(x*x+y*y);
}
inline void RelativeRotation(const PoseEye &previous,const PoseEye &current,float *r) {
    float a[9],b[9];Rotation(previous,a);Rotation(current,b);
    for(int y=0;y<3;++y)for(int x=0;x<3;++x){r[y*3+x]=0;for(int k=0;k<3;++k)r[y*3+x]+=a[k*3+y]*b[k*3+x];}
}
inline void RelativeTranslation(const PoseEye &previous,const PoseEye &current,float *t) {
    float r[9];Rotation(previous,r);
    for(int row=0;row<3;++row){t[row]=0;for(int k=0;k<3;++k)
        t[row]+=r[k*3+row]*(current.position[k]-previous.position[k]);}
}
inline bool ContinuousPose(const PoseFrame &previous,const PoseFrame &current) {
    if(!previous.valid || !current.valid || previous.session!=current.session || previous.space!=current.space || previous.epoch!=current.epoch ||
       current.display_time<previous.display_time || current.display_time-previous.display_time>250000000)return false;
    for(int i=0;i<2;++i){
        if(!ValidEye(previous.eye[i]) || !ValidEye(current.eye[i]))return false;
        float distance=0;for(int k=0;k<3;++k){float d=current.eye[i].position[k]-previous.eye[i].position[k];distance+=d*d;}
        // Without engine depth, substantial translation cannot be reconstructed by rotation alone.
        if(distance>0.02f*0.02f)return false;
        float dot=0;for(int k=0;k<4;++k)dot+=previous.eye[i].q[k]*current.eye[i].q[k];
        if(std::abs(dot)<0.866f)return false; // sudden >60 degree jump/recenter
    }
    return true;
}
