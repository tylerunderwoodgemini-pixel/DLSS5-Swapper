#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <cmath>
namespace vr_dashboard_input {
struct Mouse {int x=-1,y=-1;bool pressed=false,valid=false;float scroll_x=0,scroll_y=0;};
struct State {
    XrInstance instance={};XrSession session={};XrActionSet set={};XrAction aim={},select={},simple={},scroll={};XrSpace aim_space={};
    XrPath hand={};PFN_xrGetInstanceProcAddr gipa=nullptr;
    PFN_xrStringToPath path=nullptr;PFN_xrCreateActionSet create_set=nullptr;PFN_xrCreateAction create_action=nullptr;
    PFN_xrDestroyActionSet destroy_set=nullptr;PFN_xrCreateActionSpace create_space=nullptr;PFN_xrDestroySpace destroy_space=nullptr;
    PFN_xrLocateSpace locate=nullptr;PFN_xrGetActionStatePose pose=nullptr;PFN_xrGetActionStateFloat value=nullptr;PFN_xrGetActionStateBoolean boolean=nullptr;PFN_xrGetActionStateVector2f vector=nullptr;
    std::atomic<bool> visible{false},attached{false};std::atomic<ULONGLONG> synced{0};
    std::map<XrPath,std::vector<XrActionSuggestedBinding>> bindings;
    template<class T>bool Get(const char *name,T &fn){return XR_SUCCEEDED(gipa(instance,name,(PFN_xrVoidFunction *)&fn))&&fn;}
    bool Init(){
        if(!Get("xrStringToPath",path)||!Get("xrCreateActionSet",create_set)||!Get("xrDestroyActionSet",destroy_set)||!Get("xrCreateAction",create_action)||
           !Get("xrCreateActionSpace",create_space)||!Get("xrDestroySpace",destroy_space)||!Get("xrLocateSpace",locate)||
           !Get("xrGetActionStatePose",pose)||!Get("xrGetActionStateFloat",value)||!Get("xrGetActionStateBoolean",boolean))return false;
        Get("xrGetActionStateVector2f",vector);
        if(XR_FAILED(path(instance,"/user/hand/right",&hand)))return false;
        XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
        sprintf_s(ci.actionSetName,"dlss5_panel_%llx",(unsigned long long)(uintptr_t)session);
        sprintf_s(ci.localizedActionSetName,"DLSS5 panel %llx",(unsigned long long)(uintptr_t)session);ci.priority=100;
        if(XR_FAILED(create_set(instance,&ci,&set)))return false;
        auto action=[&](const char *name,XrActionType type,XrAction &out){XrActionCreateInfo i{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(i.actionName,name);strcpy_s(i.localizedActionName,name);i.actionType=type;i.countSubactionPaths=1;i.subactionPaths=&hand;return XR_SUCCEEDED(create_action(set,&i,&out));};
        if(!action("pointer",XR_ACTION_TYPE_POSE_INPUT,aim)||!action("select",XR_ACTION_TYPE_FLOAT_INPUT,select)||!action("simple_select",XR_ACTION_TYPE_BOOLEAN_INPUT,simple))return false;
        if(vector&&!action("scroll",XR_ACTION_TYPE_VECTOR2F_INPUT,scroll))return false;
        const char *profiles[]={"/interaction_profiles/oculus/touch_controller","/interaction_profiles/valve/index_controller","/interaction_profiles/htc/vive_controller","/interaction_profiles/microsoft/motion_controller","/interaction_profiles/khr/simple_controller"};
        for(auto profile:profiles){XrPath id={},pointer={},click={};bool basic=!strcmp(profile,"/interaction_profiles/khr/simple_controller");
            if(XR_FAILED(path(instance,profile,&id))||XR_FAILED(path(instance,"/user/hand/right/input/aim/pose",&pointer))||
               XR_FAILED(path(instance,basic?"/user/hand/right/input/select/click":"/user/hand/right/input/trigger/value",&click)))continue;
            bindings[id]={{aim,pointer},{basic?simple:select,click}};
            XrPath stick={};if(!basic&&scroll&&XR_SUCCEEDED(path(instance,!strcmp(profile,"/interaction_profiles/htc/vive_controller")?"/user/hand/right/input/trackpad":"/user/hand/right/input/thumbstick",&stick)))bindings[id].push_back({scroll,stick});}
        XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};si.action=aim;si.subactionPath=hand;si.poseInActionSpace.orientation.w=1;
        return XR_SUCCEEDED(create_space(session,&si,&aim_space));
    }
    Mouse Read(XrSpace view,XrTime time){Mouse mouse;if(!attached.load()||!synced.load()||GetTickCount64()-synced.load()>500)return mouse;
        XrActionStateGetInfo i{XR_TYPE_ACTION_STATE_GET_INFO};i.action=aim;i.subactionPath=hand;XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(pose(session,&i,&state))||!state.isActive)return mouse;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};auto flags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if(XR_FAILED(locate(aim_space,view,time,&location))||(location.locationFlags&flags)!=flags)return mouse;
        auto q=location.pose.orientation;auto p=location.pose.position;
        float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        if(!std::isfinite(norm)||norm<0.9f||norm>1.1f||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return mouse;
        float dx=-2/norm*(q.x*q.z+q.w*q.y),dy=-2/norm*(q.y*q.z-q.w*q.x),dz=-(1-2/norm*(q.x*q.x+q.y*q.y));
        if(!std::isfinite(dz)||dz>=-0.001f)return mouse;
        float t=(-1.25f-p.z)/dz;if(!std::isfinite(t)||t<0)return mouse;
        float x=p.x+t*dx,y=p.y+t*dy;
        // Keep off-panel coordinates visible to hit testing, but bounded.
        float px=(x/1.25f+0.5f)*1024,py=(0.5f-y/0.78125f)*640;
        if(!std::isfinite(px)||!std::isfinite(py)||std::abs(px)>100000||std::abs(py)>100000)return mouse;
        mouse.x=(int)px;mouse.y=(int)py;mouse.valid=true;
        i.action=select;XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};if(XR_SUCCEEDED(value(session,&i,&f))&&f.isActive&&std::isfinite(f.currentState))mouse.pressed=f.currentState>=0.65f;
        i.action=simple;XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};if(XR_SUCCEEDED(boolean(session,&i,&b))&&b.isActive)mouse.pressed|=b.currentState!=0;
        if(scroll&&vector){i.action=scroll;XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};if(XR_SUCCEEDED(vector(session,&i,&v))&&v.isActive){
            auto axis=[](float x){return std::isfinite(x)&&std::abs(x)>0.2f?std::copysign((std::min)(1.f,(std::abs(x)-0.2f)/0.8f),x):0.f;};
            mouse.scroll_x=axis(v.currentState.x);mouse.scroll_y=axis(v.currentState.y);}}
        return mouse;
    }
    void Dispose(){if(aim_space&&destroy_space)destroy_space(aim_space);aim_space={};if(set&&destroy_set)destroy_set(set);set={};}
};
static std::mutex mutex;
static std::map<XrSession,std::shared_ptr<State>> sessions;
struct Suggestion {bool extended=false;std::vector<XrActionSuggestedBinding> bindings;};
static std::map<std::pair<XrInstance,XrPath>,Suggestion> originals;
static PFN_xrSuggestInteractionProfileBindings suggest;
static PFN_xrAttachSessionActionSets attach;
static PFN_xrSyncActions sync;
inline XrResult XRAPI_CALL Suggest(XrInstance instance,const XrInteractionProfileSuggestedBinding *info){
    if(!suggest)return XR_ERROR_FUNCTION_UNSUPPORTED;
    if(!info||(info->countSuggestedBindings&&!info->suggestedBindings)||info->countSuggestedBindings>4096)return suggest(instance,info);
    std::vector<XrActionSuggestedBinding> merged;
    if(info->countSuggestedBindings)merged.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
    {std::lock_guard<std::mutex> lock(mutex);originals[{instance,info->interactionProfile}]={info->next!=nullptr,merged};
        for(auto &entry:sessions)if(entry.second->instance==instance&&!entry.second->attached.load()){
            auto it=entry.second->bindings.find(info->interactionProfile);if(it!=entry.second->bindings.end())merged.insert(merged.end(),it->second.begin(),it->second.end());}}
    auto copy=*info;copy.countSuggestedBindings=(uint32_t)merged.size();copy.suggestedBindings=merged.data();
    auto result=suggest(instance,&copy);if(XR_FAILED(result)&&merged.size()!=info->countSuggestedBindings)return suggest(instance,info);return result;
}
inline XrResult XRAPI_CALL Attach(XrSession session,const XrSessionActionSetsAttachInfo *info){
    if(!attach)return XR_ERROR_FUNCTION_UNSUPPORTED;
    std::shared_ptr<State> state;{std::lock_guard<std::mutex> lock(mutex);auto it=sessions.find(session);if(it!=sessions.end())state=it->second;}
    if(!state||!info||(info->countActionSets&&!info->actionSets)||info->countActionSets>1024)return attach(session,info);
    std::vector<XrActionSet> sets;if(info->countActionSets)sets.assign(info->actionSets,info->actionSets+info->countActionSets);
    sets.push_back(state->set);auto copy=*info;copy.countActionSets=(uint32_t)sets.size();copy.actionSets=sets.data();
    auto result=attach(session,&copy);if(XR_SUCCEEDED(result)){state->attached=true;Log("OpenXR dashboard controller action set attached; game action sets preserved");return result;}
    return attach(session,info);
}
inline XrResult XRAPI_CALL Sync(XrSession session,const XrActionsSyncInfo *info){
    if(!sync)return XR_ERROR_FUNCTION_UNSUPPORTED;
    std::shared_ptr<State> state;{std::lock_guard<std::mutex> lock(mutex);auto it=sessions.find(session);if(it!=sessions.end())state=it->second;}
    if(!state||!state->attached.load()||!state->visible.load()||!info||(info->countActiveActionSets&&!info->activeActionSets)||info->countActiveActionSets>1024)return sync(session,info);
    std::vector<XrActiveActionSet> sets;if(info->countActiveActionSets)sets.assign(info->activeActionSets,info->activeActionSets+info->countActiveActionSets);
    sets.clear();sets.push_back({state->set,state->hand});auto copy=*info;copy.next=nullptr;copy.countActiveActionSets=(uint32_t)sets.size();copy.activeActionSets=sets.data();
    auto result=sync(session,&copy);if(XR_SUCCEEDED(result))state->synced=GetTickCount64();else state->synced=0;return result;
}
inline std::shared_ptr<State> Create(XrInstance instance,XrSession session,PFN_xrGetInstanceProcAddr gipa){
    auto state=std::make_shared<State>();state->instance=instance;state->session=session;state->gipa=gipa;
    if(!state->Init()){state->Dispose();return {};}
    if(!suggest)gipa(instance,"xrSuggestInteractionProfileBindings",(PFN_xrVoidFunction *)&suggest);
    if(!attach)gipa(instance,"xrAttachSessionActionSets",(PFN_xrVoidFunction *)&attach);
    if(!sync)gipa(instance,"xrSyncActions",(PFN_xrVoidFunction *)&sync);
    if(!suggest||!attach||!sync){state->Dispose();return {};}
    std::vector<std::pair<XrPath,std::vector<XrActionSuggestedBinding>>> profiles;
    {std::lock_guard<std::mutex> lock(mutex);sessions[session]=state;
        for(auto &entry:state->bindings){auto &original=originals[{instance,entry.first}];if(original.extended)continue;auto merged=original.bindings;merged.insert(merged.end(),entry.second.begin(),entry.second.end());profiles.emplace_back(entry.first,std::move(merged));}}
    for(auto &entry:profiles){XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};info.interactionProfile=entry.first;info.countSuggestedBindings=(uint32_t)entry.second.size();info.suggestedBindings=entry.second.data();suggest(instance,&info);}
    return state;
}
inline void Destroy(XrSession session){std::shared_ptr<State> state;{std::lock_guard<std::mutex> lock(mutex);auto it=sessions.find(session);if(it==sessions.end())return;state=it->second;sessions.erase(it);}state->visible=false;state->Dispose();}
inline void Hook(const char *name,PFN_xrVoidFunction *fn){
    if(!fn||!*fn)return;
    if(!strcmp(name,"xrSuggestInteractionProfileBindings")&&*fn!=(PFN_xrVoidFunction)Suggest){suggest=(PFN_xrSuggestInteractionProfileBindings)*fn;*fn=(PFN_xrVoidFunction)Suggest;}
    if(!strcmp(name,"xrAttachSessionActionSets")&&*fn!=(PFN_xrVoidFunction)Attach){attach=(PFN_xrAttachSessionActionSets)*fn;*fn=(PFN_xrVoidFunction)Attach;}
    if(!strcmp(name,"xrSyncActions")&&*fn!=(PFN_xrVoidFunction)Sync){sync=(PFN_xrSyncActions)*fn;*fn=(PFN_xrVoidFunction)Sync;}
}
}
