#define XR_NO_PROTOTYPES
#include "../payload/vr-foveated/native/openxr/openxr_loader_negotiation.h"
#include "../payload/vr-foveated/native/vr-pose-bridge.cpp"
#include <cstring>
#include <cstdlib>
static XrInstance instance=reinterpret_cast<XrInstance>(11);
static XrSession session=reinterpret_cast<XrSession>(22);
static XrSpace space=reinterpret_cast<XrSpace>(33);
static XrApiLayerNextInfo *expected_next=nullptr;
static PoseEye current_eye[2];static bool orientation_valid=true;
static ReadPoseFrame reader=nullptr;static int event_type=0,end_calls=0;
void check(bool ok,const char *what){if(!ok){printf("FAIL: %s\n",what);exit(1);}}
static XrResult XRAPI_CALL StubCreate(const XrInstanceCreateInfo *,const XrApiLayerCreateInfo *layer,XrInstance *out){check(layer->nextInfo==expected_next,"layer chain advances exactly once");*out=instance;return XR_SUCCESS;}
static XrResult XRAPI_CALL StubDestroyInstance(XrInstance){return XR_SUCCESS;}
static XrResult XRAPI_CALL StubCreateSession(XrInstance,const XrSessionCreateInfo *,XrSession *out){*out=session;return XR_SUCCESS;}
static XrResult XRAPI_CALL StubDestroySession(XrSession){return XR_SUCCESS;}
static XrResult XRAPI_CALL StubLocate(XrSession,const XrViewLocateInfo *,XrViewState *state,uint32_t capacity,uint32_t *count,XrView *views){
    *count=2;state->viewStateFlags=orientation_valid?XR_VIEW_STATE_ORIENTATION_VALID_BIT:0;
    if(capacity>=2)for(int i=0;i<2;++i){views[i].pose.orientation={current_eye[i].q[0],current_eye[i].q[1],current_eye[i].q[2],current_eye[i].q[3]};
        views[i].pose.position={current_eye[i].position[0],current_eye[i].position[1],current_eye[i].position[2]};
        views[i].fov={atanf(current_eye[i].projection[0]),atanf(current_eye[i].projection[1]),atanf(current_eye[i].projection[3]),atanf(current_eye[i].projection[2])};}
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL StubEnd(XrSession,const XrFrameEndInfo *info){PoseFrame f;
    if(info->layerCount)check(reader(&f,sizeof(f)) && f.display_time==info->displayTime,"submitted pose published before forwarding EndFrame");
    ++end_calls;return XR_SUCCESS;
}
static XrResult XRAPI_CALL StubPoll(XrInstance,XrEventDataBuffer *data){
    if(!event_type)return XR_EVENT_UNAVAILABLE;
    auto e=reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(data);e->type=XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;e->session=session;event_type=0;return XR_SUCCESS;
}
static void XRAPI_CALL Unrelated(){}
static XrResult XRAPI_CALL StubGipa(XrInstance,const char *name,PFN_xrVoidFunction *out){
#define F(n,p) if(strcmp(name,n)==0)*out=reinterpret_cast<PFN_xrVoidFunction>(p)
    F("xrDestroyInstance",StubDestroyInstance);else F("xrCreateSession",StubCreateSession);else F("xrDestroySession",StubDestroySession);
    else F("xrLocateViews",StubLocate);else F("xrEndFrame",StubEnd);else F("xrPollEvent",StubPoll);else *out=Unrelated;
#undef F
    return XR_SUCCESS;
}
template<typename T>T Proc(PFN_xrGetInstanceProcAddr g,const char *name){PFN_xrVoidFunction p=nullptr;check(g(instance,name,&p)==XR_SUCCESS && p,"resolve intercepted function");return reinterpret_cast<T>(p);}
int main(){
    Matrix34 head={{{cosf(.1f),0,sinf(.1f),0},{0,1,0,0},{-sinf(.1f),0,cosf(.1f),0}}};
    Matrix34 eye_offset={{{1,0,0,-.032f},{0,1,0,0},{0,0,1,0}}};
    auto steam_eye=OpenVREye(head,eye_offset);
    check(std::abs(steam_eye.position[0]+.032f*cosf(.1f))<1e-6 &&
          std::abs(steam_eye.position[2]-.032f*sinf(.1f))<1e-6,
          "OpenVR head rotation moves the eye origin using the actual eye-to-head transform");
    auto module=LoadLibraryW(L"payload/vr-foveated/DLSS5OpenXRPose.dll");check(module!=nullptr,"load actual capture DLL");
    auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(module,"xrNegotiateLoaderApiLayerInterface"));
    reader=reinterpret_cast<ReadPoseFrame>(GetProcAddress(module,"DLSS5ReadOpenXRPose"));check(negotiate&&reader,"exports");
    XrNegotiateLoaderInfo loader={XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,1,sizeof(loader),1,1,XR_MAKE_VERSION(1,0,0),XR_MAKE_VERSION(1,1,0)};
    XrNegotiateApiLayerRequest req={XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST,1,sizeof(req)};
    check(negotiate(&loader,"bad_name",&req)==XR_ERROR_INITIALIZATION_FAILED,"reject unrelated layer negotiation");
    check(negotiate(&loader,"XR_APILAYER_DLSS5_pose",&req)==XR_SUCCESS,"negotiate layer ABI");
    XrApiLayerNextInfo next={XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO,1,sizeof(next)};next.nextGetInstanceProcAddr=StubGipa;next.nextCreateApiLayerInstance=StubCreate;
    XrApiLayerCreateInfo layer={XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO,1,sizeof(layer)};layer.nextInfo=&next;
    XrInstanceCreateInfo create={XR_TYPE_INSTANCE_CREATE_INFO};XrInstance out={};check(req.createApiLayerInstance(&create,&layer,&out)==XR_SUCCESS && out==instance,"create forwarding");
    auto g=req.getInstanceProcAddr;
    check(Proc<PFN_xrVoidFunction>(g,"xrUnrelated")==Unrelated,"unintercepted extension stays unchanged");
    auto create_session=Proc<PFN_xrCreateSession>(g,"xrCreateSession");XrSessionCreateInfo sci={XR_TYPE_SESSION_CREATE_INFO};XrSession so={};check(create_session(instance,&sci,&so)==XR_SUCCESS && so==session,"session dispatch");
    auto locate=Proc<PFN_xrLocateViews>(g,"xrLocateViews");auto end=Proc<PFN_xrEndFrame>(g,"xrEndFrame");auto poll=Proc<PFN_xrPollEvent>(g,"xrPollEvent");
    for(auto &eye:current_eye){eye.q[3]=1;eye.projection[0]=-1;eye.projection[1]=1;eye.projection[2]=-1;eye.projection[3]=1;}
    XrViewLocateInfo info={XR_TYPE_VIEW_LOCATE_INFO};info.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;info.space=space;info.displayTime=1000000000;
    XrViewState view_state={XR_TYPE_VIEW_STATE};XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t count=0;
    check(locate(session,&info,&view_state,2,&count,views)==XR_SUCCESS,"locate forwards");PoseFrame first;check(reader(&first,sizeof(first)) && first.abi==2,"capture successful stereo views");
    reshade::api::effect_runtime rt;OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==0,"first pose warms history");
    const float angle=0.1f;current_eye[0].q[1]=sinf(angle/2);current_eye[0].q[3]=cosf(angle/2);
    current_eye[1].q[1]=-sinf(angle/2);current_eye[1].q[3]=cosf(angle/2);
    info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});
    check(rt.values["VR_POSE_VALID"][0]==1,"bridge obtains valid OpenXR history");
    check(std::abs(rt.values["VR_POSE_R0"][2]-sinf(angle))<0.00001f,"left current-to-previous quaternion direction");
    check(std::abs(rt.values["VR_POSE_RIGHT_R0"][2]+sinf(angle))<0.00001f,"independent right-eye rotation");
    OnBegin(&rt,nullptr,{},{});
    check(rt.values["VR_POSE_VALID"][0]==1 && std::abs(rt.values["VR_POSE_R0"][0]-1)<1e-6f &&
          std::abs(rt.values["VR_POSE_R0"][2])<1e-6f && std::abs(rt.values["VR_POSE_RIGHT_R0"][2])<1e-6f,
          "repeated pose renders do not apply the preceding head turn again");
    current_eye[0].position[0]+=0.003f;current_eye[1].position[0]+=0.003f;info.displayTime+=14000000;
    locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});
    check(rt.values["VR_POSE_VALID"][0]==1 && std::abs(rt.values["VR_POSE_TRANSLATION_M"][0]-0.003f)<0.00001f,"bridge publishes small head-center translations while keeping rotational pose valid");
    check(std::abs(rt.values["VR_POSE_TRANSLATION_L"][0]-.003f*cosf(angle))<1e-6f &&
          std::abs(rt.values["VR_POSE_TRANSLATION_L"][2]-.003f*sinf(angle))<1e-6f &&
          std::abs(rt.values["VR_POSE_TRANSLATION_R"][2]+.003f*sinf(angle))<1e-6f,
          "bridge delivers independent eye displacement in previous camera coordinates");
    OnBegin(&rt,nullptr,{},{});
    check(rt.values["VR_POSE_TRANSLATION_M"][0]==0 && rt.values["VR_POSE_TRANSLATION_L"][0]==0 &&
          rt.values["VR_POSE_TRANSLATION_R"][2]==0,"repeated pose renders do not repeat positional displacement");
    reshade::api::effect_runtime desktop;desktop.desktop=true;OnBegin(&desktop,nullptr,{},{});check(desktop.values["VR_POSE_VALID"][0]==0,"desktop mirror excluded");
    // Final submitted poses override a LocateViews estimate when available.
    XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(int i=0;i<2;++i){pv[i].pose=views[i].pose;pv[i].fov=views[i].fov;}
    XrCompositionLayerProjection projection={XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=space;projection.viewCount=2;projection.views=pv;
    auto base=reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection);XrFrameEndInfo ei={XR_TYPE_FRAME_END_INFO};ei.displayTime=info.displayTime;ei.layerCount=1;ei.layers=&base;
    auto capture_submitted=reinterpret_cast<bool (__cdecl *)(XrSession,const XrFrameEndInfo *)>(GetProcAddress(module,"DLSS5CaptureOpenXRSubmittedFrame"));
    check(capture_submitted!=nullptr,"pre-ReShade image-pose capture export exists");
    pv[0].pose.position.x+=0.004f;
    PoseFrame exact_image;
    check(capture_submitted(session,&ei) && reader(&exact_image,sizeof(exact_image)) &&
          std::abs(exact_image.eye[0].position[0]-pv[0].pose.position.x)<1e-6f && end_calls==0,
          "adapter capture uses submitted image pose before effects without dispatching EndFrame twice");
    pv[0].pose.position.x-=0.004f;
    check(end(session,&ei)==XR_SUCCESS && end_calls==1,"EndFrame forwards once");
    auto read_depth=reinterpret_cast<ReadDepthSubmissionInfo>(GetProcAddress(module,"DLSS5ReadOpenXRDepthSubmission"));
    DepthSubmissionInfo depth;
    check(read_depth && read_depth(&depth,sizeof(depth)) && depth.present_mask==0,"projection without depth is reported explicitly");
    XrCompositionLayerDepthInfoKHR di[2]={{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}};
    for(int i=0;i<2;++i){
        di[i].subImage.swapchain=reinterpret_cast<XrSwapchain>(0x4000);
        di[i].subImage.imageArrayIndex=i;di[i].subImage.imageRect.extent={3080,3272};
        di[i].minDepth=0;di[i].maxDepth=1;di[i].nearZ=0.05f;di[i].farZ=1000;
        pv[i].next=&di[i];
    }
    check(end(session,&ei)==XR_SUCCESS && end_calls==2,"depth metadata interception preserves dispatch count");
    check(read_depth(&depth,sizeof(depth)) && depth.present_mask==3 && depth.eye[1].array_index==1 && depth.eye[0].width==3080 && depth.eye[0].near_z==0.05f,"both-eye submission metadata preserves array slices, rectangle and projection depths");
    OnBegin(&rt,nullptr,{},{});
    pv[1].next=nullptr;end(session,&ei);check(read_depth(&depth,sizeof(depth)) && depth.present_mask==1,"missing right depth never claims stereo geometry");
    pv[0].next=nullptr;end(session,&ei);check(read_depth(&depth,sizeof(depth)) && depth.present_mask==0,"depth removed from submission clears old metadata");
    orientation_valid=false;info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);PoseFrame frame;
    check(!reader(&frame,sizeof(frame)),"tracking loss invalidates capture");OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==0,"tracking loss clears shader flag");
    check(!read_depth(&depth,sizeof(depth)),"tracking loss invalidates submitted-depth metadata");
    orientation_valid=true;info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==0,"tracking recovery warms history");
    info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==1,"tracking recovery becomes valid");
    event_type=1;XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};poll(instance,&event);
    info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==0,"recenter epoch resets even if space handle unchanged");
    current_eye[0].position[0]=0.1f;info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);OnBegin(&rt,nullptr,{},{});check(rt.values["VR_POSE_VALID"][0]==0,"large translation cannot claim rotation-only validity");
    current_eye[0].q[3]=NAN;info.displayTime+=14000000;locate(session,&info,&view_state,2,&count,views);check(!reader(&frame,sizeof(frame)),"NaN pose rejected");
    Proc<PFN_xrDestroySession>(g,"xrDestroySession")(session);check(!reader(&frame,sizeof(frame)),"destroyed session cannot leak pose");
    check(Proc<PFN_xrDestroyInstance>(g,"xrDestroyInstance")(instance)==XR_SUCCESS,"destroy forwarding");
    OnDestroy(&rt);if(capture_module){FreeLibrary(capture_module);capture_module=nullptr;}FreeLibrary(module);
    puts("PASS: actual OpenXR layer dispatch and capture, bridge uniform delivery, independent eye math, duplicate frame handling, tracking/recenter/translation/NaN/session resets");
}
