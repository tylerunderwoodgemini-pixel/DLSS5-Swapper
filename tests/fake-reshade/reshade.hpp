#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>
namespace reshade {
enum class addon_event {reshade_begin_effects,destroy_effect_runtime};
inline bool register_addon(HMODULE,HMODULE){return true;}
inline void unregister_addon(HMODULE){}
template<addon_event ev,typename T>void register_event(T){}
template<addon_event ev,typename T>void unregister_event(T){}
namespace log {enum class level {info};inline void message(level,const char*){}}
namespace api {
struct command_list{};struct resource_view{uint64_t handle=0;};struct uniform{uint64_t handle=0;};
struct effect_runtime {
    std::map<std::string,std::vector<float>> values;std::vector<std::string> names;bool desktop=false;
    uintptr_t get_hwnd(){return desktop?1:0;}
    uniform find_uniform_variable(const char*,const char *name){
        for(size_t i=0;i<names.size();++i)if(names[i]==name)return {i+1};
        names.emplace_back(name);return {names.size()};
    }
    void set_uniform_value_float(uniform u,const float *v,size_t n){values[names[u.handle-1]]=std::vector<float>(v,v+n);}
};
}
}
