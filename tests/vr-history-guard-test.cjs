const fs=require('node:fs'),path=require('node:path');
const builder=fs.readFileSync(path.join(__dirname,'../payload/vr-foveated/Build-And-Install-VRUniversalFoveated-v26.ps1'),'utf8');
const start=builder.indexOf('static bool VrHistoryGuard('),end=builder.indexOf('static void VrPoseReset()',start);
if(start<0||end<0)throw Error('history guard source missing');
const dir=path.join(__dirname,'vr-runtime-compat-work');fs.mkdirSync(dir,{recursive:true});
fs.writeFileSync(path.join(dir,'history-guard-test.cpp'),`#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using ULONGLONG=uint64_t;
static ULONGLONG now=1000;
static ULONGLONG GetTickCount64(){return now;}
namespace reshade::api {struct effect_runtime {float transition=0; struct Uniform{unsigned handle=1;};Uniform find_uniform_variable(const char*,const char*){return {};};void get_uniform_value_float(Uniform,float *v,unsigned){*v=transition;}};}
static bool pose=true,depth=true,motion=false,g_vr_depth_guides_reliable=true;
static struct {unsigned width=100,height=50;}g;
static unsigned g_vr_guide_width=100,g_vr_guide_height=50;
static reshade::api::effect_runtime *g_vr_guide_owner=nullptr;
static ULONGLONG g_vr_guide_checked=1000;
static float g_vr_neural_weight=0;
static bool VrHasPoseGuide(void*){return pose;}
static bool VrHasStereoDepthGuide(void*){return depth;}
static bool VrUnresolvedPoseMotion(void*){return motion;}
template<class...T>void Log(const char*,T...){}
${builder.slice(start,end)}
static void check(bool ok,const char*name){if(!ok){puts(name);exit(1);}}
int main(){reshade::api::effect_runtime rt;g_vr_guide_owner=&rt;
check(!VrHistoryGuard(&rt)&&g_vr_neural_weight==1,"stable valid depth should display neural output");
rt.transition=1;check(VrHistoryGuard(&rt)&&g_vr_neural_weight==0,"transition must reset even with valid depth");
rt.transition=0;now=1200;check(!VrHistoryGuard(&rt)&&g_vr_neural_weight==0,"depth must not cancel transition warmup");
now=1700;check(!VrHistoryGuard(&rt)&&g_vr_neural_weight>0.49&&g_vr_neural_weight<0.51,"same-current-frame fade after warmup");
now=2001;g_vr_guide_checked=now;check(!VrHistoryGuard(&rt)&&g_vr_neural_weight==1,"stable depth recovers full output");
motion=true;now=2015;check(!VrHistoryGuard(&rt)&&g_vr_neural_weight==1,"normal head motion with depth must not reset");
depth=false;check(VrHistoryGuard(&rt)&&g_vr_neural_weight==0,"unresolved motion without depth retains fallback");
puts("PASS: actual feeder history guard transition reset with valid depth, bounded recovery, stable head motion, missing-depth fallback");}
`);
