#pragma once
#include <cstdint>
struct MenuHoldGesture {
    uint64_t began=0;bool holding=false,latched=false;
    bool Update(bool active,bool left,bool right,uint64_t now){
        if(!active){holding=false;latched=true;return false;}
        if(!left&&!right){holding=false;latched=false;return false;}
        if(!left||!right){holding=false;return false;}
        if(latched)return false;
        if(!holding){holding=true;began=now;return false;}
        if(now<began){holding=false;return false;}
        if(now-began<3000)return false;
        latched=true;holding=false;return true;
    }
};
