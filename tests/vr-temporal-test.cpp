#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
using Microsoft::WRL::ComPtr;
void check(bool v,const char *s){if(!v){std::printf("FAIL: %s\n",s);std::exit(1);}}
static unsigned long long ticks=1000;
#define GetTickCount64 TestTickCount64
unsigned long long GetTickCount64(){return ticks;}
void Log(const char*,...){}
namespace reshade::api {
struct uniform {unsigned long long handle;};
struct effect_runtime {
 float pose=0,translation=0,rotation=0,right_rotation=0; bool has_uniform=true;
 uniform find_uniform_variable(const char*,const char *name){
  if(!has_uniform)return {0};
  if(!strcmp(name,"VR_POSE_VALID"))return {1};
  if(!strcmp(name,"VR_POSE_TRANSLATION_M"))return {2};
  if(!strcmp(name,"VR_STEREO_DEPTH_VALID"))return {9};
  return {(strstr(name,"RIGHT")?6u:3u)+static_cast<unsigned>(name[strlen(name)-1]-'0')};
 }
 void get_uniform_value_float(uniform u,float *v,int count){
  if(u.handle==1){*v=pose;return;}if(u.handle==2){*v=translation;return;}
  if(u.handle==9){*v=1.f;return;}
  for(int i=0;i<count;++i)v[i]=i==int((u.handle-3)%3)?1.f:0.f;
  if(u.handle==3)v[2]=rotation;
  if(u.handle==6)v[2]=right_rotation;
 }
};
}
static struct {unsigned width=800,height=600;} g;
static bool g_vr_depth_guides_reliable=false;
static void *g_vr_guide_owner=nullptr;
static unsigned g_vr_guide_width=0,g_vr_guide_height=0;
static unsigned long long g_vr_guide_checked=0;
static float g_vr_neural_weight=1;
#include "vr-history-guard.generated.h"
int main(){
 reshade::api::effect_runtime rt,other;
 check(VrHistoryGuard(&rt),"no pose/depth must reset even while still");
 g_vr_depth_guides_reliable=true;g_vr_guide_owner=&rt;g_vr_guide_width=800;g_vr_guide_height=600;g_vr_guide_checked=1000;
 check(!VrHistoryGuard(&rt),"verified current-runtime depth may reuse history");
 check(VrHistoryGuard(&other),"another runtime's guides must not enable history");
 g.width=801;check(VrHistoryGuard(&rt),"resolution change invalidates guide evidence");g.width=800;
 ticks=2500;check(VrHistoryGuard(&rt),"expired readback evidence resets history");
 rt.pose=1;ticks=2620;check(!VrHistoryGuard(&rt),"still pose may reuse after no-depth warmup");
 check(g_vr_neural_weight==0,"reset history is not immediately displayed when guard settles");
 rt.translation=0.0002f;check(VrHistoryGuard(&rt),"submillimeter translation resets history without depth");
 rt.translation=0;ticks=2670;check(VrHistoryGuard(&rt),"settling period prevents immediately reusing moved history");
 ticks=2740;check(!VrHistoryGuard(&rt),"static scene resumes history after settling");
 ticks=3270;check(!VrHistoryGuard(&rt) && std::abs(g_vr_neural_weight-0.333333f)<0.001f,"converged current-frame neural output fades in gradually");
 ticks=3670;check(!VrHistoryGuard(&rt) && g_vr_neural_weight==1,"steady output returns to full neural strength");
 rt.rotation=0.0001f;rt.translation=0.00001f;check(!VrHistoryGuard(&rt),"tiny tracking jitter retains static history");
 rt.translation=0;rt.rotation=0;rt.right_rotation=0.002f;check(VrHistoryGuard(&rt),"right-eye-only rotation cannot retain unresolved history");
 rt.right_rotation=0;ticks+=120;check(!VrHistoryGuard(&rt),"right eye settles independently");
 rt.rotation=0.002f;check(VrHistoryGuard(&rt),"head rotation resets when no usable depth is present");
 check(g_vr_neural_weight==0,"unresolved moving history cannot reach displayed image");
 g_vr_guide_checked=ticks;check(!VrHistoryGuard(&rt),"verified depth retains pose history during motion");
 check(g_vr_neural_weight==1,"reliable depth keeps full neural output");
 g_vr_depth_guides_reliable=false;rt.rotation=0;ticks+=120;
 check(VrMotionScale(&rt,1.021f)==1.f,"geometric pose vectors must not receive optical-flow calibration");
 rt.has_uniform=false;check(VrHistoryGuard(&rt),"missing pose uniform cannot trust saved value");
 rt.has_uniform=true;rt.pose=NAN;check(VrHistoryGuard(&rt),"invalid pose flag rejected");
 check(VrMotionScale(&rt,1.021f)==1.021f,"invalid pose preserves estimated-vector calibration");

 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP");
 ComPtr<ID3DBlob> v,p,errors;
 auto compile=[&](const char *entry,const char *model,ID3DBlob **out){
  HRESULT h=D3DCompileFromFile(L"tests/vr-motion.generated.hlsl",nullptr,nullptr,entry,model,0,0,out,&errors);
  if(FAILED(h)&&errors)std::printf("%s\n",(char*)errors->GetBufferPointer());check(SUCCEEDED(h),"compile actual motion shader body");};
 compile("VS","vs_5_0",&v);compile("PS_MotionVectors","ps_5_0",&p);
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
 check(SUCCEEDED(dev->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)),"VS");
 check(SUCCEEDED(dev->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)),"PS");
 ComPtr<ID3D11Texture2D> targets[3];ComPtr<ID3D11RenderTargetView> views[3];
 D3D11_TEXTURE2D_DESC d={};d.Width=4;d.Height=1;d.MipLevels=1;d.ArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
 for(int i=0;i<3;++i){check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&targets[i])),"MRT");check(SUCCEEDED(dev->CreateRenderTargetView(targets[i].Get(),nullptr,&views[i])),"view");}
 ID3D11RenderTargetView *out[]={views[0].Get(),views[1].Get(),views[2].Get()};ctx->OMSetRenderTargets(3,out,nullptr);
 D3D11_VIEWPORT vp={0,0,4,1,0,1};ctx->RSSetViewports(1,&vp);
 ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->Draw(3,0);
 ctx->OMSetRenderTargets(0,nullptr,nullptr);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> read;check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&read)),"readback");
 ctx->CopyResource(read.Get(),targets[0].Get());D3D11_MAPPED_SUBRESOURCE m={};check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map motion");
 auto data=(float*)m.pData;
 check(std::abs(data[0]-3.2f)<0.001f && std::abs(data[1]-0.08f)<0.001f,"flat depth retains provider flow in left eye");
 check(data[4]==0 && data[5]==0,"left-to-right-eye reprojection rejected");
 check(std::abs(data[8]-3.2f)<0.001f,"right eye retains own provider flow");
 check(data[12]==0,"out-of-frame motion rejected");ctx->Unmap(read.Get(),0);
 ctx->CopyResource(read.Get(),targets[1].Get());check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map mask");data=(float*)m.pData;
 for(int i=0;i<4;++i)check(data[4*i]==1,"missing depth fully biases current color");ctx->Unmap(read.Get(),0);
 // Valid rotation plus small translation retains motion, but relieves unresolved history.
 for(int mismatch=0;mismatch<2;++mismatch){
  D3D_SHADER_MACRO defines[]={{"TEST_POSE","1"},{"TEST_TRANSLATION","0.003"},{"TEST_MISMATCH",mismatch?"1":"0"},{nullptr,nullptr}};
  errors.Reset();ComPtr<ID3DBlob> relief_blob;
  HRESULT result=D3DCompileFromFile(L"tests/vr-motion.generated.hlsl",defines,nullptr,"PS_MotionVectors","ps_5_0",0,0,&relief_blob,&errors);
  if(FAILED(result)&&errors)std::printf("%s\n",(char*)errors->GetBufferPointer());check(SUCCEEDED(result),"compile pose history relief branch");
  check(SUCCEEDED(dev->CreatePixelShader(relief_blob->GetBufferPointer(),relief_blob->GetBufferSize(),nullptr,&ps)),"history relief shader");
  ctx->PSSetShader(ps.Get(),nullptr,0);ctx->OMSetRenderTargets(3,out,nullptr);ctx->Draw(3,0);ctx->OMSetRenderTargets(0,nullptr,nullptr);
  ctx->CopyResource(read.Get(),targets[0].Get());check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map retained geometric vector");data=(float*)m.pData;
  check(std::abs(data[0]-3.2f)<0.001f,"history relief preserves exact pose displacement");ctx->Unmap(read.Get(),0);
  ctx->CopyResource(read.Get(),targets[1].Get());check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map pose relief mask");data=(float*)m.pData;
  check(std::abs(data[0]-(mismatch?1.f:0.5f))<0.001f,"translation relieves history; changed surfaces fully reject history");ctx->Unmap(read.Get(),0);
 }
 // Render the real stereo pose function, including previous-frame FOV conversion.
 errors.Reset();ComPtr<ID3DBlob> pose_blob;
 HRESULT pose_result=D3DCompileFromFile(L"tests/vr-pose-motion.generated.hlsl",nullptr,nullptr,"PS_Pose","ps_5_0",0,0,&pose_blob,&errors);
 if(FAILED(pose_result)&&errors)std::printf("%s\n",(char*)errors->GetBufferPointer());check(SUCCEEDED(pose_result),"compile actual stereo pose shader");
 check(SUCCEEDED(dev->CreatePixelShader(pose_blob->GetBufferPointer(),pose_blob->GetBufferSize(),nullptr,&ps)),"pose pixel shader");
 ctx->PSSetShader(ps.Get(),nullptr,0);ctx->OMSetRenderTargets(1,out,nullptr);ctx->Draw(3,0);ctx->OMSetRenderTargets(0,nullptr,nullptr);
 ctx->CopyResource(read.Get(),targets[0].Get());check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map pose motion");data=(float*)m.pData;
 for(int i=0;i<4;++i){
  const double u=(i+0.5)/4,eye=i>=2?1:0,euv=u*2-eye,tx=euv*2-1,angle=eye?-0.1:0.1;
  const double px=(std::cos(angle)*tx-std::sin(angle))/(std::sin(angle)*tx+std::cos(angle));
  const double extent=eye?0.8:1.2,puv=((px+extent)/(extent*2)+eye)*0.5;
  check(std::abs(data[i*4]-(puv-u))<0.00001,"independent eye reprojection uses previous FOV and current-to-previous rotation");
  check(std::abs(data[i*4+1])<0.00001 && data[i*4+3]==1,"center-row pose rays remain valid");
 }
 ctx->Unmap(read.Get(),0);
 // Real mean-removed patch comparison identifies translation beyond pose rotation.
 for(int reject=0;reject<8;++reject){
  D3D_SHADER_MACRO macros[]={{"TEST_REJECT_FLOW",reject==1?"1":reject==3?"3":"0"},{"TEST_FLOW_X",reject==2?"-0.4":(reject==4||reject==7)?"0.012":"0.04"},{"TEST_CALIBRATED",reject>=5?"1":"0"},{"TEST_LOCAL_FLOW",reject==6?"1":"0"},{nullptr,nullptr}};
  errors.Reset();ComPtr<ID3DBlob> selection_blob;
  HRESULT result=D3DCompileFromFile(L"tests/vr-parallax.generated.hlsl",macros,nullptr,"PS_Select","ps_5_0",0,0,&selection_blob,&errors);
  if(FAILED(result)&&errors)std::printf("%s\n",(char*)errors->GetBufferPointer());check(SUCCEEDED(result),"compile actual parallax selection and PatchError");
  check(SUCCEEDED(dev->CreatePixelShader(selection_blob->GetBufferPointer(),selection_blob->GetBufferSize(),nullptr,&ps)),"selection shader");
  ctx->PSSetShader(ps.Get(),nullptr,0);ctx->OMSetRenderTargets(1,out,nullptr);ctx->Draw(3,0);ctx->OMSetRenderTargets(0,nullptr,nullptr);
  ctx->CopyResource(read.Get(),targets[0].Get());check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"map parallax selection");data=(float*)m.pData;
  for(int i=0;i<4;++i){check(std::abs(data[i*4]-((reject==2||reject==6||reject==7)?.01f:reject==4?.012f:.04f))<.00001f,"observed translation survives depth and lighting changes; eye-boundary rejection stays active");
    if(reject==1)check(data[i*4+2]==1.f,"depth disagreement rejects history without discarding matching motion");}
  ctx->Unmap(read.Get(),0);
 }
 std::puts("PASS: no-depth translation/rotation reset, jitter tolerance, settling and verified-depth history; guard ownership/expiry; WARP motion, mask and stereo reprojection");
}
