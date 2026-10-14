#pragma once
// Exact modified 6.8.0 callbacks. Nested broadcasts return false so the
// destination setter still applies its value, without rebroadcasting it.
namespace vr_settings_sync {
using UniformFn=bool(*)(reshade::api::effect_runtime *,reshade::api::effect_uniform_variable,const void *,size_t);
using EffectsFn=bool(*)(reshade::api::effect_runtime *,bool);
using TechniqueFn=bool(*)(reshade::api::effect_runtime *,reshade::api::effect_technique,bool);
using PresetFn=void(*)(reshade::api::effect_runtime *,const char *);
static UniformFn uniform{};static EffectsFn effects{};static TechniqueFn technique{};static PresetFn preset{};
static thread_local bool forwarding=false;
struct Scope {Scope(){forwarding=true;}~Scope(){forwarding=false;}};
inline bool Uniform(reshade::api::effect_runtime *rt,reshade::api::effect_uniform_variable v,const void *data,size_t size){if(forwarding)return false;Scope scope;return uniform(rt,v,data,size);}
inline bool Effects(reshade::api::effect_runtime *rt,bool value){if(forwarding)return false;Scope scope;return effects(rt,value);}
inline bool Technique(reshade::api::effect_runtime *rt,reshade::api::effect_technique t,bool value){if(forwarding)return false;Scope scope;return technique(rt,t,value);}
inline void Preset(reshade::api::effect_runtime *rt,const char *path){if(forwarding)return;Scope scope;preset(rt,path);}
inline bool Arm(HMODULE owner){
    if(uniform)return true;if(!owner)return false;auto base=(BYTE *)owner;
    auto dos=(IMAGE_DOS_HEADER *)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto nt=(IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);if(nt->OptionalHeader.SizeOfImage!=0x59a000)return false;
    const BYTE u[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x41,0x55,0x41,0x56,0x41,0x57,0x48};
    const BYTE e[]={0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x80,0x3d,0x78,0x24};
    const BYTE t[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x41,0x56,0x48,0x8d,0x6c,0x24,0xd0};
    const BYTE p[]={0x48,0x89,0x6c,0x24,0x18,0x56,0x48,0x83,0xec,0x20,0x80,0x3d,0x38,0x22};
    if(memcmp(base+0x33c90,u,sizeof(u))||memcmp(base+0x33ef0,e,sizeof(e))||memcmp(base+0x33f80,t,sizeof(t))||memcmp(base+0x34130,p,sizeof(p)))return false;
    uniform=(UniformFn)(base+0x33c90);effects=(EffectsFn)(base+0x33ef0);technique=(TechniqueFn)(base+0x33f80);preset=(PresetFn)(base+0x34130);
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    if(DetourAttach((PVOID *)&uniform,(PVOID)Uniform)||DetourAttach((PVOID *)&effects,(PVOID)Effects)||DetourAttach((PVOID *)&technique,(PVOID)Technique)||DetourAttach((PVOID *)&preset,(PVOID)Preset)){
        DetourTransactionAbort();uniform=nullptr;return false;}
    if(DetourTransactionCommit()!=NO_ERROR){uniform=nullptr;return false;}
    Log("Modified ReShade settings synchronization guarded against nested runtime broadcasts");return true;
}
inline void Clear(){if(!uniform)return;DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());DetourDetach((PVOID *)&uniform,(PVOID)Uniform);DetourDetach((PVOID *)&effects,(PVOID)Effects);DetourDetach((PVOID *)&technique,(PVOID)Technique);DetourDetach((PVOID *)&preset,(PVOID)Preset);DetourTransactionCommit();uniform=nullptr;}
}
