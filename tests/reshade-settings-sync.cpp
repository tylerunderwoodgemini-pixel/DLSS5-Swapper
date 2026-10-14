#include <windows.h>
#include <detours.h>
#include <cstring>
#include <cstdio>
#include <cassert>
#include "../payload/vr-foveated/native/reshade/reshade_api.hpp"
static void Log(const char *s){puts(s);}
#include "../payload/vr-foveated/native/reshade-settings-sync-guard.h"
struct Fake {void **table;unsigned writes{};float value{};unsigned toggles{},techniques{},presets{};};
static vr_settings_sync::UniformFn broadcast;static unsigned depth=0;static bool loop=false;
static vr_settings_sync::EffectsFn broadcast_effects;static vr_settings_sync::TechniqueFn broadcast_technique;static vr_settings_sync::PresetFn broadcast_preset;
static void Effects(Fake *rt,bool value){if(++depth>8){loop=true;--depth;return;}broadcast_effects((reshade::api::effect_runtime *)rt,value);++rt->toggles;--depth;}
static void Technique(Fake *rt,uint64_t,bool value){if(++depth>8){loop=true;--depth;return;}broadcast_technique((reshade::api::effect_runtime *)rt,{1},value);++rt->techniques;--depth;}
static void Preset(Fake *rt,const char *path){if(++depth>8){loop=true;--depth;return;}broadcast_preset((reshade::api::effect_runtime *)rt,path);++rt->presets;--depth;}
static bool Annotation(void *,uint64_t,const char *,bool *,size_t,uint32_t){return false;}
static void Type(void *,uint64_t,reshade::api::format *type,uint32_t *,uint32_t *,uint32_t *){*type=reshade::api::format::r32_float;}
static void Name(void *,uint64_t,char *out,size_t *size){strcpy_s(out,*size,"test");*size=4;}
static void *Find(void *,uint64_t *out,const char *,const char *){*out=1;return out;}
static void Set(Fake *rt,uint64_t,const float *data,size_t count,uint32_t){
    assert(count==1);if(++depth>8){loop=true;--depth;return;}
    broadcast((reshade::api::effect_runtime *)rt,{1},data,4);rt->value=*data;++rt->writes;--depth;
}
int main(int argc,char **argv){assert(argc==2);auto owner=LoadLibraryA(argv[1]);assert(owner);auto base=(BYTE *)owner;
    void *table[80]{};table[0xa0/8]=(void *)Find;table[0xa8/8]=(void *)Type;table[0xb0/8]=(void *)Name;table[0xd8/8]=(void *)Annotation;table[0x108/8]=(void *)Set;table[0x110/8]=(void *)Set;table[0x220/8]=(void *)Name;
    Fake a{table},b{table};void *targets[]={&a,&b};
    table[0x180/8]=(void *)Find;table[0x188/8]=(void *)Name;table[0x230/8]=(void *)Name;table[0x1b0/8]=(void *)Annotation;table[0x1c0/8]=(void *)Technique;table[0x1e8/8]=(void *)Effects;table[0x1f8/8]=(void *)Preset;
    broadcast_effects=(vr_settings_sync::EffectsFn)(base+0x33ef0);broadcast_technique=(vr_settings_sync::TechniqueFn)(base+0x33f80);broadcast_preset=(vr_settings_sync::PresetFn)(base+0x34130);
    auto first=(void ***)(base+0x496348),last=(void ***)(base+0x496350);auto enabled=base+0x496379;
    auto saved_first=*first,saved_last=*last;auto saved_enable=*enabled;
    *first=targets;*last=targets+2;*enabled=1;
    broadcast=(vr_settings_sync::UniformFn)(base+0x33c90);float value=.75f;
    Set(&a,1,&value,1,0);assert(loop);puts("PASS: actual modified ReShade callback reproduces repeated runtime broadcasts");
    loop=false;depth=0;a.writes=b.writes=0;
    assert(vr_settings_sync::Arm(owner));Set(&a,1,&value,1,0);
    assert(!loop&&a.writes==1&&b.writes==1&&a.value==value&&b.value==value);
    Effects(&a,true);Technique(&a,1,true);Preset(&a,"test.ini");
    assert(!loop&&a.toggles==1&&b.toggles==1&&a.techniques==1&&b.techniques==1&&a.presets==1&&b.presets==1);
    *first=saved_first;*last=saved_last;*enabled=saved_enable;vr_settings_sync::Clear();
    FreeLibrary(owner);puts("PASS: actual callback with guard updates both runtimes once without recursion");
}
