#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <cmath>
#include "openxr-menu-gesture.h"
namespace vr_dashboard_input {
static PFN_xrSyncActions sync;
struct Mouse {int x=-1,y=-1;bool pressed=false,valid=false;float scroll_x=0,scroll_y=0;};
struct State {
    XrInstance instance={};XrSession session={};XrActionSet set={},opener={};XrAction aim={},select={},simple={},scroll={},toggle={},close_toggle={};XrSpace aim_space={};
    XrPath hand={},left_hand={};std::map<unsigned,std::pair<XrActionSet,XrAction>> openers;XrAction active_left={},active_right={};MenuHoldGesture gesture;std::atomic<bool> toggle_pending{false};PFN_xrGetInstanceProcAddr gipa=nullptr;
    PFN_xrStringToPath path=nullptr;PFN_xrCreateActionSet create_set=nullptr;PFN_xrCreateAction create_action=nullptr;
    PFN_xrDestroyActionSet destroy_set=nullptr;PFN_xrCreateActionSpace create_space=nullptr;PFN_xrDestroySpace destroy_space=nullptr;
    PFN_xrLocateSpace locate=nullptr;PFN_xrGetActionStatePose pose=nullptr;PFN_xrGetActionStateFloat value=nullptr;PFN_xrGetActionStateBoolean boolean=nullptr;PFN_xrGetActionStateVector2f vector=nullptr;
    std::atomic<bool> visible{false},attached{false};std::atomic<ULONGLONG> synced{0};
    std::map<XrPath,std::vector<XrActionSuggestedBinding>> bindings;std::recursive_mutex action_mutex;std::vector<XrActiveActionSet> last_game_sets;std::vector<XrActiveActionSetPriorityEXT> last_priorities;std::atomic<bool> input_failed{false};
    template<class T>bool Get(const char *name,T &fn){return XR_SUCCEEDED(gipa(instance,name,(PFN_xrVoidFunction *)&fn))&&fn;}
    bool Init(){
        if(!Get("xrStringToPath",path)||!Get("xrCreateActionSet",create_set)||!Get("xrDestroyActionSet",destroy_set)||!Get("xrCreateAction",create_action)||
           !Get("xrCreateActionSpace",create_space)||!Get("xrDestroySpace",destroy_space)||!Get("xrLocateSpace",locate)||
           !Get("xrGetActionStatePose",pose)||!Get("xrGetActionStateFloat",value)||!Get("xrGetActionStateBoolean",boolean))return false;
        Get("xrGetActionStateVector2f",vector);
        if(XR_FAILED(path(instance,"/user/hand/right",&hand))||XR_FAILED(path(instance,"/user/hand/left",&left_hand)))return false;
        XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
        sprintf_s(ci.actionSetName,"dlss5_panel_%llx",(unsigned long long)(uintptr_t)session);
        sprintf_s(ci.localizedActionSetName,"DLSS5 panel %llx",(unsigned long long)(uintptr_t)session);ci.priority=UINT32_MAX;
        if(XR_FAILED(create_set(instance,&ci,&set)))return false;
        sprintf_s(ci.actionSetName,"dlss5_open_%llx",(unsigned long long)(uintptr_t)session);
        sprintf_s(ci.localizedActionSetName,"DLSS5 menu shortcut %llx",(unsigned long long)(uintptr_t)session);ci.priority=0;
        if(XR_FAILED(create_set(instance,&ci,&opener)))return false;
        XrPath hands[]={left_hand,hand};XrActionCreateInfo ti{XR_TYPE_ACTION_CREATE_INFO};
        strcpy_s(ti.actionName,"menu_hold");strcpy_s(ti.localizedActionName,"Hold both stick clicks to toggle menu");
        ti.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;ti.countSubactionPaths=2;ti.subactionPaths=hands;
        if(XR_FAILED(create_action(opener,&ti,&toggle)))return false;openers[0]={opener,toggle};active_left=active_right=toggle;
        strcpy_s(ti.actionName,"menu_close_hold");if(XR_FAILED(create_action(set,&ti,&close_toggle)))return false;
        auto action=[&](const char *name,XrActionType type,XrAction &out){XrActionCreateInfo i{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(i.actionName,name);strcpy_s(i.localizedActionName,name);i.actionType=type;i.countSubactionPaths=1;i.subactionPaths=&hand;return XR_SUCCEEDED(create_action(set,&i,&out));};
        if(!action("pointer",XR_ACTION_TYPE_POSE_INPUT,aim)||!action("select",XR_ACTION_TYPE_FLOAT_INPUT,select)||!action("simple_select",XR_ACTION_TYPE_BOOLEAN_INPUT,simple))return false;
        if(vector&&!action("scroll",XR_ACTION_TYPE_VECTOR2F_INPUT,scroll))return false;
        const char *profiles[]={"/interaction_profiles/oculus/touch_controller","/interaction_profiles/valve/index_controller","/interaction_profiles/htc/vive_controller","/interaction_profiles/microsoft/motion_controller","/interaction_profiles/facebook/touch_controller_pro","/interaction_profiles/meta/touch_controller_plus","/interaction_profiles/hp/mixed_reality_controller","/interaction_profiles/htc/vive_cosmos_controller","/interaction_profiles/htc/vive_focus3_controller","/interaction_profiles/khr/simple_controller"};
        for(auto profile:profiles){XrPath id={},pointer={},click={};bool basic=!strcmp(profile,"/interaction_profiles/khr/simple_controller");
            if(XR_FAILED(path(instance,profile,&id))||XR_FAILED(path(instance,"/user/hand/right/input/aim/pose",&pointer))||
               XR_FAILED(path(instance,basic?"/user/hand/right/input/select/click":"/user/hand/right/input/trigger/value",&click)))continue;
            bindings[id]={{aim,pointer},{basic?simple:select,click}};
            XrPath stick={};if(!basic&&scroll&&XR_SUCCEEDED(path(instance,!strcmp(profile,"/interaction_profiles/htc/vive_controller")?"/user/hand/right/input/trackpad":"/user/hand/right/input/thumbstick",&stick)))bindings[id].push_back({scroll,stick});
            if(!basic){bool pad=!strcmp(profile,"/interaction_profiles/htc/vive_controller");
                XrPath left={},right={};
                if(XR_SUCCEEDED(path(instance,pad?"/user/hand/left/input/trackpad/click":"/user/hand/left/input/thumbstick/click",&left))&&
                   XR_SUCCEEDED(path(instance,pad?"/user/hand/right/input/trackpad/click":"/user/hand/right/input/thumbstick/click",&right))){bindings[id].push_back({toggle,left});bindings[id].push_back({toggle,right});bindings[id].push_back({close_toggle,left});bindings[id].push_back({close_toggle,right});}}
        }
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


    bool AddPriority(unsigned priority){
        if(openers.count(priority))return true;if(openers.size()>=32)return false;
        XrActionSet new_set{};XrAction new_action{};XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
        sprintf_s(ci.actionSetName,"dlss5_open_%llx_%u",(unsigned long long)(uintptr_t)session,priority);
        sprintf_s(ci.localizedActionSetName,"DLSS5 menu shortcut %llx priority %u",(unsigned long long)(uintptr_t)session,priority);ci.priority=priority;
        if(XR_FAILED(create_set(instance,&ci,&new_set)))return false;
        XrPath hands[]={left_hand,hand};XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(ai.actionName,"menu_hold");strcpy_s(ai.localizedActionName,"Hold both stick clicks to toggle menu");ai.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;ai.countSubactionPaths=2;ai.subactionPaths=hands;
        if(XR_FAILED(create_action(new_set,&ai,&new_action))){destroy_set(new_set);return false;}
        openers[priority]={new_set,new_action};
        for(auto &profile:bindings){auto initial=profile.second;for(const auto &binding:initial)if(binding.action==toggle)profile.second.push_back({new_action,binding.binding});}
        return true;
    }
    void Shortcut(ULONGLONG now){
        XrActionStateGetInfo i{XR_TYPE_ACTION_STATE_GET_INFO};i.action=visible.load()?close_toggle:active_left;
        XrActionStateBoolean left{XR_TYPE_ACTION_STATE_BOOLEAN},right{XR_TYPE_ACTION_STATE_BOOLEAN};
        i.subactionPath=left_hand;bool ok=XR_SUCCEEDED(boolean(session,&i,&left));
        i.action=visible.load()?close_toggle:active_right;i.subactionPath=hand;ok=XR_SUCCEEDED(boolean(session,&i,&right))&&ok;
        if(gesture.Update(ok&&left.isActive&&right.isActive,left.currentState!=0,right.currentState!=0,now))toggle_pending=true;
    }
    void RefreshVisible(){
        if(!visible.load()||!attached.load()||GetTickCount64()-synced.load()<100)return;
        std::lock_guard<std::recursive_mutex> lock(action_mutex);if(!sync)return;
        auto sets=last_game_sets;sets.push_back({set,XR_NULL_PATH});
        XrActionsSyncInfo i{XR_TYPE_ACTIONS_SYNC_INFO};i.countActiveActionSets=(uint32_t)sets.size();i.activeActionSets=sets.data();
        XrActiveActionSetPrioritiesEXT priorities{XR_TYPE_ACTIVE_ACTION_SET_PRIORITIES_EXT};if(!last_priorities.empty()){priorities.actionSetPriorityCount=(uint32_t)last_priorities.size();priorities.actionSetPriorities=last_priorities.data();i.next=&priorities;}
        auto now=GetTickCount64();if(XR_SUCCEEDED(sync(session,&i))){synced=now;Shortcut(now);}else{synced=0;gesture.Update(false,false,false,now);}
    }
    void Dispose(){if(aim_space&&destroy_space)destroy_space(aim_space);aim_space={};if(set&&destroy_set)destroy_set(set);set={};for(auto &entry:openers)if(destroy_set)destroy_set(entry.second.first);openers.clear();opener={};}
};
static std::mutex mutex;
static std::map<XrSession,std::shared_ptr<State>> sessions;
struct Suggestion {bool extended=false;std::vector<XrActionSuggestedBinding> bindings;};
static std::map<std::pair<XrInstance,XrPath>,Suggestion> originals;
static PFN_xrSuggestInteractionProfileBindings suggest;
static PFN_xrAttachSessionActionSets attach;
static std::map<XrActionSet,unsigned> game_priorities;
static PFN_xrCreateActionSet create_game_set=nullptr;
inline XrResult XRAPI_CALL GameSet(XrInstance instance,const XrActionSetCreateInfo *info,XrActionSet *out){
    auto result=create_game_set?create_game_set(instance,info,out):XR_ERROR_FUNCTION_UNSUPPORTED;
    if(XR_SUCCEEDED(result)&&info&&out){std::lock_guard<std::mutex> lock(mutex);game_priorities[*out]=info->priority;}return result;
}


static PFN_xrGetActionStateBoolean query_boolean=nullptr;static PFN_xrGetActionStateFloat query_float=nullptr;static PFN_xrGetActionStateVector2f query_vector=nullptr;
inline bool Mask(XrSession session,const XrActionStateGetInfo *info){
    if(!info)return false;std::lock_guard<std::mutex> lock(mutex);auto it=sessions.find(session);if(it==sessions.end()||!it->second->visible.load())return false;
    auto &s=*it->second;for(auto action:{s.aim,s.select,s.simple,s.scroll,s.close_toggle})if(action==info->action)return false;
    for(auto &entry:s.openers)if(entry.second.second==info->action)return false;return true;
}
inline XrResult XRAPI_CALL Boolean(XrSession session,const XrActionStateGetInfo *info,XrActionStateBoolean *out){auto r=query_boolean(session,info,out);
    if(XR_SUCCEEDED(r)&&out&&Mask(session,info)){out->currentState=XR_FALSE;out->changedSinceLastSync=XR_FALSE;out->isActive=XR_FALSE;}return r;}
inline XrResult XRAPI_CALL Float(XrSession session,const XrActionStateGetInfo *info,XrActionStateFloat *out){auto r=query_float(session,info,out);
    if(XR_SUCCEEDED(r)&&out&&Mask(session,info)){out->currentState=0;out->changedSinceLastSync=XR_FALSE;out->isActive=XR_FALSE;}return r;}
inline XrResult XRAPI_CALL Vector(XrSession session,const XrActionStateGetInfo *info,XrActionStateVector2f *out){auto r=query_vector(session,info,out);
    if(XR_SUCCEEDED(r)&&out&&Mask(session,info)){out->currentState={};out->changedSinceLastSync=XR_FALSE;out->isActive=XR_FALSE;}return r;}
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

    std::vector<unsigned> priorities{0};{std::lock_guard<std::mutex> lock(mutex);for(auto set:sets){auto it=game_priorities.find(set);if(it!=game_priorities.end()&&std::find(priorities.begin(),priorities.end(),it->second)==priorities.end())priorities.push_back(it->second);}}
    bool added=false;for(auto priority:priorities)if(!state->openers.count(priority))added=state->AddPriority(priority)||added;
    if(added){
        std::vector<std::pair<XrPath,std::vector<XrActionSuggestedBinding>>> profiles;
        {std::lock_guard<std::mutex> lock(mutex);for(const auto &entry:state->bindings){const auto &original=originals[{state->instance,entry.first}];if(original.extended)continue;auto merged=original.bindings;merged.insert(merged.end(),entry.second.begin(),entry.second.end());profiles.emplace_back(entry.first,std::move(merged));}}
        for(const auto &entry:profiles){XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};info.interactionProfile=entry.first;info.countSuggestedBindings=(uint32_t)entry.second.size();info.suggestedBindings=entry.second.data();suggest(state->instance,&info);}
    }
    sets.push_back(state->set);for(const auto &entry:state->openers)sets.push_back(entry.second.first);
    auto copy=*info;copy.countActionSets=(uint32_t)sets.size();copy.actionSets=sets.data();
    auto result=attach(session,&copy);if(XR_SUCCEEDED(result)){state->attached=true;Log("OpenXR dashboard controller action set attached; game action sets preserved");return result;}
    return attach(session,info);
}
inline XrResult XRAPI_CALL Sync(XrSession session,const XrActionsSyncInfo *info){
    if(!sync)return XR_ERROR_FUNCTION_UNSUPPORTED;
    std::shared_ptr<State> state;{std::lock_guard<std::mutex> lock(mutex);auto it=sessions.find(session);if(it!=sessions.end())state=it->second;}
    if(!state||!state->attached.load()||!info||(info->countActiveActionSets&&!info->activeActionSets)||info->countActiveActionSets>1024)return sync(session,info);
    std::lock_guard<std::recursive_mutex> action_lock(state->action_mutex);
    std::vector<XrActiveActionSet> sets;if(info->countActiveActionSets)sets.assign(info->activeActionSets,info->activeActionSets+info->countActiveActionSets);
    state->last_game_sets=sets;
    if(state->visible.load())sets.push_back({state->set,XR_NULL_PATH});
    XrActiveActionSetPrioritiesEXT overrides{XR_TYPE_ACTIVE_ACTION_SET_PRIORITIES_EXT};std::vector<XrActiveActionSetPriorityEXT> override_values;
    auto original_override=info->next&&((const XrBaseInStructure *)info->next)->type==XR_TYPE_ACTIVE_ACTION_SET_PRIORITIES_EXT?(const XrActiveActionSetPrioritiesEXT *)info->next:nullptr;
    state->last_priorities.clear();if(original_override&&original_override->actionSetPriorityCount<=1024&&original_override->actionSetPriorities)state->last_priorities.assign(original_override->actionSetPriorities,original_override->actionSetPriorities+original_override->actionSetPriorityCount);
    auto copy=*info;
    if(!state->visible.load()){
        unsigned left=0,right=0;{std::lock_guard<std::mutex> lock(mutex);for(const auto &set:sets){unsigned priority=0;auto it=game_priorities.find(set.actionSet);if(it!=game_priorities.end())priority=it->second;
            if(original_override&&original_override->actionSetPriorityCount<=1024&&original_override->actionSetPriorities)for(unsigned i=0;i<original_override->actionSetPriorityCount;++i)if(original_override->actionSetPriorities[i].actionSet==set.actionSet)priority=original_override->actionSetPriorities[i].priorityOverride;
            if(!set.subactionPath||set.subactionPath==state->left_hand)left=(std::max)(left,priority);
            if(!set.subactionPath||set.subactionPath==state->hand)right=(std::max)(right,priority);
        }}
        auto l=state->openers.find(left),r=state->openers.find(right);if(l==state->openers.end())l=state->openers.find(0);if(r==state->openers.end())r=state->openers.find(0);
        state->active_left=l->second.second;state->active_right=r->second.second;
        if(l==r)sets.push_back({l->second.first,XR_NULL_PATH});else{sets.push_back({l->second.first,state->left_hand});sets.push_back({r->second.first,state->hand});}
        if(original_override&&original_override->actionSetPriorityCount<=1024&&original_override->actionSetPriorities&&(l!=r||left==right)){
            override_values.assign(original_override->actionSetPriorities,original_override->actionSetPriorities+original_override->actionSetPriorityCount);
            override_values.push_back({l->second.first,left});if(l!=r)override_values.push_back({r->second.first,right});
            overrides=*original_override;overrides.actionSetPriorityCount=(uint32_t)override_values.size();overrides.actionSetPriorities=override_values.data();copy.next=&overrides;
        }
    }
    copy.countActiveActionSets=(uint32_t)sets.size();copy.activeActionSets=sets.data();
    auto result=sync(session,&copy);if(XR_FAILED(result)){state->input_failed=true;state->visible=false;state->synced=0;state->gesture.Update(false,false,false,GetTickCount64());return sync(session,info);}
    if(XR_SUCCEEDED(result)){auto now=GetTickCount64();state->synced=now;state->Shortcut(now);}else{state->synced=0;state->gesture.Update(false,false,false,GetTickCount64());}return result;
}
inline std::shared_ptr<State> Create(XrInstance instance,XrSession session,PFN_xrGetInstanceProcAddr gipa){
    auto state=std::make_shared<State>();state->instance=instance;state->session=session;state->gipa=gipa;
    if(!state->Init()){state->Dispose();return {};}
    if(!suggest){PFN_xrVoidFunction fn=nullptr;gipa(instance,"xrSuggestInteractionProfileBindings",&fn);if(fn!=(PFN_xrVoidFunction)Suggest)suggest=(PFN_xrSuggestInteractionProfileBindings)fn;}
    if(!attach){PFN_xrVoidFunction fn=nullptr;gipa(instance,"xrAttachSessionActionSets",&fn);if(fn!=(PFN_xrVoidFunction)Attach)attach=(PFN_xrAttachSessionActionSets)fn;}
    if(!sync){PFN_xrVoidFunction fn=nullptr;gipa(instance,"xrSyncActions",&fn);if(fn!=(PFN_xrVoidFunction)Sync)sync=(PFN_xrSyncActions)fn;}
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
    if(!strcmp(name,"xrGetActionStateBoolean")&&*fn!=(PFN_xrVoidFunction)Boolean){query_boolean=(PFN_xrGetActionStateBoolean)*fn;*fn=(PFN_xrVoidFunction)Boolean;}
    if(!strcmp(name,"xrGetActionStateFloat")&&*fn!=(PFN_xrVoidFunction)Float){query_float=(PFN_xrGetActionStateFloat)*fn;*fn=(PFN_xrVoidFunction)Float;}
    if(!strcmp(name,"xrGetActionStateVector2f")&&*fn!=(PFN_xrVoidFunction)Vector){query_vector=(PFN_xrGetActionStateVector2f)*fn;*fn=(PFN_xrVoidFunction)Vector;}
    if(!strcmp(name,"xrCreateActionSet")&&*fn!=(PFN_xrVoidFunction)GameSet){create_game_set=(PFN_xrCreateActionSet)*fn;*fn=(PFN_xrVoidFunction)GameSet;}
    if(!strcmp(name,"xrSuggestInteractionProfileBindings")&&*fn!=(PFN_xrVoidFunction)Suggest){suggest=(PFN_xrSuggestInteractionProfileBindings)*fn;*fn=(PFN_xrVoidFunction)Suggest;}
    if(!strcmp(name,"xrAttachSessionActionSets")&&*fn!=(PFN_xrVoidFunction)Attach){attach=(PFN_xrAttachSessionActionSets)*fn;*fn=(PFN_xrVoidFunction)Attach;}
    if(!strcmp(name,"xrSyncActions")&&*fn!=(PFN_xrVoidFunction)Sync){sync=(PFN_xrSyncActions)*fn;*fn=(PFN_xrVoidFunction)Sync;}
}
}
