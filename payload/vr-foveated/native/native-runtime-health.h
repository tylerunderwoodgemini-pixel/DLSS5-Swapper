#pragma once
#include <cmath>
#include <cstdint>
#include <string>
namespace native_foveation {
inline bool finite_guide_value(float value) { return std::isfinite(value); }
inline bool recently_evaluated(uint64_t now,uint64_t last) {
  return last && now>=last && now-last<2000;
}
inline bool may_recover(uint64_t now,uint64_t failed,unsigned attempts,bool complete,bool device_alive) {
  return failed && now>=failed && now-failed>=5000 && attempts<2 && complete && device_alive;
}
inline std::string json_string(const std::string &value) {
  static const char hex[]="0123456789abcdef";
  std::string result="\"";
  for(unsigned char c:value) {
    if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
    else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
    else result+=char(c);
  }
  return result+'"';
}
}
