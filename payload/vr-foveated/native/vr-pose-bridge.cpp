#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <reshade.hpp>
#include <algorithm>
#include <cstdio>
#include "pose-data.h"
extern "C" __declspec(dllexport) const char *NAME="VR pose bridge (OpenVR + OpenXR)";
extern "C" __declspec(dllexport) const char *DESCRIPTION="Render-synchronized stereo rotation, translation and projection guides.";
static HMODULE capture_module=nullptr;
static ReadPoseFrame read_xr=nullptr;
static ReadDepthSubmissionInfo read_depth=nullptr;
static bool (__cdecl *active_xr)()=nullptr;
struct Matrix34 {float m[3][4];};
struct TrackedPose {Matrix34 matrix;float velocity[3],angular_velocity[3];int tracking_result;bool valid,connected;};
struct CompositorPrefix {
    void *set_space;int (__cdecl *get_space)();void *wait_poses,*last_poses;
    int (__cdecl *last_pose)(unsigned,TrackedPose *,TrackedPose *);
};
// IVRSystem_022 prefix (OpenVR v1.16.8); newer function tables have different slots.
struct SystemPrefix {void *size,*projection;void (__cdecl *projection_raw)(int,float *,float *,float *,float *);void *distortion;Matrix34 (__cdecl *eye_to_head)(int);};
static CompositorPrefix *compositor=nullptr;static SystemPrefix *system_vr=nullptr;
static PoseEye OpenVREye(const Matrix34 &head,const Matrix34 &eye_to_head){
    Matrix34 composed={};
    for(int y=0;y<3;++y)for(int x=0;x<4;++x){
        for(int k=0;k<3;++k)composed.m[y][x]+=head.m[y][k]*eye_to_head.m[k][x];
        if(x==3)composed.m[y][x]+=head.m[y][3];
    }
    const auto &m=composed.m;PoseEye eye={};auto &q=eye.q;const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0){const float s=std::sqrt(trace+1)*2;q[3]=s/4;q[0]=(m[2][1]-m[1][2])/s;q[1]=(m[0][2]-m[2][0])/s;q[2]=(m[1][0]-m[0][1])/s;}
    else{int i=0;if(m[1][1]>m[i][i])i=1;if(m[2][2]>m[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;
        const float s=std::sqrt(1+m[i][i]-m[j][j]-m[k][k])*2;q[i]=s/4;q[3]=(m[k][j]-m[j][k])/s;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s;}
    for(int k=0;k<3;++k)eye.position[k]=m[k][3];
    return eye;
}
static bool OpenVRFrame(PoseFrame &frame){
    auto module=GetModuleHandleW(L"openvr_api.dll");if(!module)return false;
    auto get=reinterpret_cast<void *(__cdecl *)(const char *,int *)>(GetProcAddress(module,"VR_GetGenericInterface"));if(!get)return false;
    int error=0;
    if(!compositor)compositor=static_cast<CompositorPrefix *>(get("FnTable:IVRCompositor_027",&error));
    if(!system_vr)system_vr=static_cast<SystemPrefix *>(get("FnTable:IVRSystem_022",&error));
    if(!compositor||!system_vr||!compositor->last_pose||!system_vr->projection_raw||!system_vr->eye_to_head)return false;
    TrackedPose render={},game={};if(compositor->last_pose(0,&render,&game)!=0||!render.valid||!render.connected)return false;
    frame.session=UINT64_MAX;frame.space=compositor->get_space?compositor->get_space():0;
    frame.tick=GetTickCount64();frame.display_time=static_cast<int64_t>(frame.tick)*1000000;
    frame.valid=1;
    for(int i=0;i<2;++i){frame.eye[i]=OpenVREye(render.matrix,system_vr->eye_to_head(i));
        system_vr->projection_raw(i,&frame.eye[i].projection[0],&frame.eye[i].projection[1],&frame.eye[i].projection[2],&frame.eye[i].projection[3]);
        frame.valid = frame.valid && ValidEye(frame.eye[i]);}
    return frame.valid!=0;
}
struct State {reshade::api::effect_runtime *runtime=nullptr;PoseFrame previous={};float rotation[2][9]={};float displacement[2][3]={};float previous_projection[2][4]={};float translation=0;bool valid=false;int source=0,depth_mask=-1;};
static State states[16];static SRWLOCK state_lock=SRWLOCK_INIT;
static void Set(reshade::api::effect_runtime *rt,const char *name,const float *data,size_t count){
    const auto uniform=rt->find_uniform_variable("DLSS5_Feed.fx",name);if(uniform.handle)rt->set_uniform_value_float(uniform,data,count);
}
static void Valid(reshade::api::effect_runtime *rt,bool valid){float v=valid?1.f:0.f;Set(rt,"VR_POSE_VALID",&v,1);}
static void OnBegin(reshade::api::effect_runtime *rt,reshade::api::command_list *,reshade::api::resource_view,reshade::api::resource_view){
    if(rt->get_hwnd()!=0){Valid(rt,false);return;}
    AcquireSRWLockExclusive(&state_lock);
    // The capture layer may load after ReShade's desktop add-on scan.
    if(!capture_module && GetModuleHandleExW(0,L"DLSS5OpenXRPose.dll",&capture_module)) {
        read_xr=reinterpret_cast<ReadPoseFrame>(GetProcAddress(capture_module,"DLSS5ReadOpenXRPose"));
        active_xr=reinterpret_cast<bool (__cdecl *)()>(GetProcAddress(capture_module,"DLSS5OpenXRPoseActive"));
        read_depth=reinterpret_cast<ReadDepthSubmissionInfo>(GetProcAddress(capture_module,"DLSS5ReadOpenXRDepthSubmission"));
    }
    State *state=nullptr;for(auto &s:states)if(s.runtime==rt){state=&s;break;}
    if(!state)for(auto &s:states)if(!s.runtime){s.runtime=rt;state=&s;break;}
    DepthSubmissionInfo depth;
    if(state && read_depth && read_depth(&depth,sizeof(depth)) && state->depth_mask!=int(depth.present_mask)){
        state->depth_mask=int(depth.present_mask);
        char message[500];
        sprintf_s(message,"OpenXR submitted depth: mask=%u (both eyes=3); L rect=%dx%d layer=%u near/far=%.6g/%.6g range=%.3g/%.3g; R rect=%dx%d layer=%u near/far=%.6g/%.6g range=%.3g/%.3g. Metadata only, not yet a feeder texture.",
            depth.present_mask,depth.eye[0].width,depth.eye[0].height,depth.eye[0].array_index,depth.eye[0].near_z,depth.eye[0].far_z,depth.eye[0].min_depth,depth.eye[0].max_depth,
            depth.eye[1].width,depth.eye[1].height,depth.eye[1].array_index,depth.eye[1].near_z,depth.eye[1].far_z,depth.eye[1].min_depth,depth.eye[1].max_depth);
        reshade::log::message(reshade::log::level::info,message);
    }
    PoseFrame frame;int source=0;
    if(read_xr && read_xr(&frame,sizeof(frame)))source=2;
    else if(!(active_xr && active_xr()) && OpenVRFrame(frame))source=1;
    if(!state || !source){if(state){state->previous.valid=0;state->valid=false;}Valid(rt,false);ReleaseSRWLockExclusive(&state_lock);return;}
    // Shader luma/depth history advances on every effects render. Match its
    // cadence even when OpenXR repeats a predicted display timestamp; retaining
    // the last nonzero delta would apply the same head turn to history twice.
    {
        state->valid=ContinuousPose(state->previous,frame) && state->source==source;
        float distance=0;
        for(int k=0;k<3;++k){
            const float d=(frame.eye[0].position[k]+frame.eye[1].position[k]-state->previous.eye[0].position[k]-state->previous.eye[1].position[k])*0.5f;
            distance+=d*d;
        }
        state->translation=state->valid?std::sqrt(distance):0;
        for(int eye=0;eye<2;++eye){
            if(state->valid)RelativeTranslation(state->previous.eye[eye],frame.eye[eye],state->displacement[eye]);
            else std::fill(state->displacement[eye],state->displacement[eye]+3,0.f);
            if(state->valid)RelativeRotation(state->previous.eye[eye],frame.eye[eye],state->rotation[eye]);
            else{std::fill(state->rotation[eye],state->rotation[eye]+9,0.f);state->rotation[eye][0]=state->rotation[eye][4]=state->rotation[eye][8]=1.f;}
            std::copy(state->valid?state->previous.eye[eye].projection:frame.eye[eye].projection,
                      (state->valid?state->previous.eye[eye].projection:frame.eye[eye].projection)+4,state->previous_projection[eye]);
        }
        if(state->source!=source){reshade::log::message(reshade::log::level::info,source==2?"Pose bridge v3: OpenXR per-render 6DoF eye poses acquired":"Pose bridge v3: OpenVR per-render 6DoF eye poses acquired");}
        state->source=source;state->previous=frame;
    }
    for(int eye=0;eye<2;++eye){
        Set(rt,eye?"VR_POSE_TRANSLATION_R":"VR_POSE_TRANSLATION_L",state->displacement[eye],3);
        for(int row=0;row<3;++row){float data[4]={state->rotation[eye][row*3],state->rotation[eye][row*3+1],state->rotation[eye][row*3+2],0};
            char name[40];sprintf_s(name,eye?"VR_POSE_RIGHT_R%d":"VR_POSE_R%d",row);Set(rt,name,data,4);}
        Set(rt,eye?"VR_POSE_PROJ_R":"VR_POSE_PROJ_L",frame.eye[eye].projection,4);
        Set(rt,eye?"VR_POSE_PREV_PROJ_R":"VR_POSE_PREV_PROJ_L",state->previous_projection[eye],4);
    }
    float stereo=1.f;Set(rt,"VR_POSE_STEREO",&stereo,1);Valid(rt,state->valid);
    Set(rt,"VR_POSE_TRANSLATION_M",&state->translation,1);
    ReleaseSRWLockExclusive(&state_lock);
}
static void OnDestroy(reshade::api::effect_runtime *rt){AcquireSRWLockExclusive(&state_lock);for(auto &s:states)if(s.runtime==rt)s={};ReleaseSRWLockExclusive(&state_lock);}
extern "C" __declspec(dllexport) BOOL AddonInit(HMODULE addon,HMODULE owner){
    if(!reshade::register_addon(addon,owner))return FALSE;
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(OnBegin);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(OnDestroy);
    reshade::log::message(reshade::log::level::info,"Pose bridge v2 registered: OpenVR + OpenXR");return TRUE;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon,HMODULE){
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(OnBegin);
    reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(OnDestroy);
    if(capture_module){FreeLibrary(capture_module);capture_module=nullptr;read_xr=nullptr;active_xr=nullptr;read_depth=nullptr;}
    compositor=nullptr;system_vr=nullptr;for(auto &s:states)s={};reshade::unregister_addon(addon);
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
