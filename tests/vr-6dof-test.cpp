#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../payload/vr-foveated/native/pose-data.h"
using Microsoft::WRL::ComPtr;
void check(bool v,const char *s){if(!v){printf("FAIL: %s\n",s);exit(1);}}
struct Target{ComPtr<ID3D11Texture2D> tex,read;ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11ShaderResourceView> srv;};
int main(){
 PoseEye previous={},current={};previous.q[1]=std::sqrt(.5f);previous.q[3]=std::sqrt(.5f);current=previous;current.position[0]=.02;
 float translation[3];RelativeTranslation(previous,current,translation);
 check(std::abs(translation[0])<1e-6 && std::abs(translation[2]-.02)<1e-6,"world displacement transformed into previous eye coordinates");
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx)),"WARP device");
 auto target=[&](unsigned w,unsigned h){Target t;D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&t.tex)),"target");check(SUCCEEDED(dev->CreateRenderTargetView(t.tex.Get(),nullptr,&t.rtv)),"rtv");check(SUCCEEDED(dev->CreateShaderResourceView(t.tex.Get(),nullptr,&t.srv)),"srv");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&t.read)),"readback");return t;};
 int fullpose=0,fastconf=0,engineDepth=0;
 auto compile=[&](const char *entry,const char *profile,int invalid,int reversed){ComPtr<ID3DBlob> b,e;const char *ids[]={"0","1","2","3"};D3D_SHADER_MACRO m[]={{"TEST_INVALID",ids[invalid]},{"TEST_REVERSED",ids[reversed]},{"TEST_FULLPOSE",ids[fullpose]},{"TEST_FAST_CONF",ids[fastconf]},{"TEST_ENGINE_DEPTH",ids[engineDepth]},{nullptr,nullptr}};
 auto hr=D3DCompileFromFile(L"tests/vr-6dof.generated.hlsl",m,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e);if(FAILED(hr)&&e)printf("%s\n",(char*)e->GetBufferPointer());check(SUCCEEDED(hr),"compile actual 6DoF shaders");return b;};
 ComPtr<ID3D11VertexShader> vs;auto blob=compile("VS","vs_5_0",0,1);check(SUCCEEDED(dev->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&vs)),"vs");ctx->VSSetShader(vs.Get(),nullptr,0);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 auto draw=[&](Target &t,unsigned w,unsigned h,const char *entry,int invalid,int reversed){ComPtr<ID3D11PixelShader> ps;auto b=compile(entry,"ps_5_0",invalid,reversed);check(SUCCEEDED(dev->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps)),"ps");ctx->PSSetShader(ps.Get(),nullptr,0);auto r=t.rtv.Get();ctx->OMSetRenderTargets(1,&r,nullptr);D3D11_VIEWPORT vp={0,0,float(w),float(h),0,1};ctx->RSSetViewports(1,&vp);ctx->Draw(3,0);ctx->OMSetRenderTargets(0,nullptr,nullptr);};
 Target samples=target(40,24),fit=target(2,1),pose=target(4,1),bounds=target(8,1);
 for(engineDepth=0;engineDepth<2;++engineDepth)for(fastconf=0;fastconf<2;++fastconf)for(fullpose=0;fullpose<2;++fullpose)for(int reversed=0;reversed<2;++reversed)for(int invalid=0;invalid<4;++invalid){
  ID3D11ShaderResourceView *nulls[2]={};ctx->PSSetShaderResources(0,2,nulls);draw(samples,40,24,"PS_DepthFitSamples",invalid,reversed);
  auto s=samples.srv.Get();ctx->PSSetShaderResources(0,1,&s);draw(fit,2,1,"PS_DepthFitSolve",invalid,reversed);
  ctx->CopyResource(fit.read.Get(),fit.tex.Get());D3D11_MAPPED_SUBRESOURCE map={};check(SUCCEEDED(ctx->Map(fit.read.Get(),0,D3D11_MAP_READ,0,&map)),"map fit");auto p=(float*)map.pData;
  for(int eye=0;eye<2;++eye){if(invalid && (!engineDepth||invalid==1))check(p[eye*4+3]==0,"unavailable depth or unsolved optical calibration cannot claim conversion");else{
   printf("eye %d reversed %d fit meanD=%.6f meanW=%.6f slope=%.6f valid=%.0f\n",eye,reversed,p[eye*4],p[eye*4+1],p[eye*4+2],p[eye*4+3]);
   check(p[eye*4+3]==1 && std::abs(p[eye*4+2]-(reversed?10.f:-10.f))<.01,"measures engine inverse-depth conversion independently for each eye");
  }}ctx->Unmap(fit.read.Get(),0);
  auto sf=fit.srv.Get();ctx->PSSetShaderResources(1,1,&sf);draw(pose,4,1,"PS_Pose",invalid,reversed);
  ctx->CopyResource(pose.read.Get(),pose.tex.Get());check(SUCCEEDED(ctx->Map(pose.read.Get(),0,D3D11_MAP_READ,0,&map)),"map pose");p=(float*)map.pData;
  for(int pixel=0;pixel<4;++pixel){
   double uv=(pixel+.5)/4,eye=pixel>=2?1:0,left=eye?-.8:-1,rayX=left+(uv*2-eye)*2;
   double angle=fullpose?.015:0,x=cos(angle)*rayX-sin(angle),z=-sin(angle)*rayX-cos(angle),y=0;
   if(!invalid || (engineDepth&&invalid!=1)){double inverseZ=invalid==2?(reversed?.4:9.6):.5;
    x+=(fullpose?(eye?.009:.01):(eye?-.015:.02))*inverseZ;y+=(fullpose?(eye?.006:.007):0)*inverseZ;z+=(fullpose?(eye?.013:.012):0)*inverseZ;}
   double expectedX=(((-x/z-left)/2+eye)*.5)-uv,expectedY=(1+y/z)*.5-.5;
   check(std::abs(p[pixel*4]-expectedX)<2e-6 && std::abs(p[pixel*4+1]-expectedY)<2e-6,"actual pose shader reconstructs positional parallax with combined rotation and XYZ translation");
  }ctx->Unmap(pose.read.Get(),0);
 }
 ID3D11ShaderResourceView *nulls[2]={};ctx->PSSetShaderResources(0,2,nulls);draw(bounds,8,1,"PS_Bounds",0,1);
 ctx->CopyResource(bounds.read.Get(),bounds.tex.Get());D3D11_MAPPED_SUBRESOURCE map={};check(SUCCEEDED(ctx->Map(bounds.read.Get(),0,D3D11_MAP_READ,0,&map)),"map crop");auto p=(float*)map.pData;
 for(int pixel=0;pixel<8;++pixel)check(p[pixel*4]==((pixel%4==1||pixel%4==2)?1.f:0.f),"samples leaving either center crop cannot reuse packed history");ctx->Unmap(bounds.read.Get(),0);
 puts("PASS: actual 6DoF calibration and pose shaders, independent eyes, reversed/forward depth, invalid calibration, packed history bounds");
}

