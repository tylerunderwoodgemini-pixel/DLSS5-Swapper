#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main(){
 std::ifstream f("tests/vr-runtime-compat-work/spatial-aa-test.hlsl",std::ios::binary);std::string source((std::istreambuf_iterator<char>(f)),{});assert(!source.empty());
 ComPtr<ID3DBlob> vs,ps,error;
 assert(SUCCEEDED(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"F__PostProcessVS","vs_5_0",0,0,&vs,&error)));
 assert(SUCCEEDED(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"F__SpatialAA","ps_5_0",0,0,&ps,&error)));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
 assert(SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex)));
 assert(SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel)));
 std::vector<float> data(32*16*4);for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){auto p=&data[(y*32+x)*4];p[0]=x>=16?4.f:(x<y?1.f:0.f);p[1]=x>=16?0:p[0];p[2]=x>=16?2:p[0];p[3]=.7f;}
 D3D11_TEXTURE2D_DESC desc{};desc.Width=32;desc.Height=16;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA initial{data.data(),32*16,0};ComPtr<ID3D11Texture2D> input,output,readback;
 assert(SUCCEEDED(device->CreateTexture2D(&desc,&initial,&input)));desc.BindFlags=D3D11_BIND_RENDER_TARGET;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&output)));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
 ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11RenderTargetView> rtv;assert(SUCCEEDED(device->CreateShaderResourceView(input.Get(),nullptr,&srv)));assert(SUCCEEDED(device->CreateRenderTargetView(output.Get(),nullptr,&rtv)));
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 ComPtr<ID3D11SamplerState> sample;assert(SUCCEEDED(device->CreateSamplerState(&sampler,&sample)));
 D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> constants;assert(SUCCEEDED(device->CreateBuffer(&buffer,nullptr,&constants)));
 auto target=rtv.Get();auto texture=srv.Get();auto sampling=sample.Get();auto cb=constants.Get();D3D11_VIEWPORT viewport{0,0,32,16,0,1};
 D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> raster_state;assert(SUCCEEDED(device->CreateRasterizerState(&raster,&raster_state)));context->RSSetState(raster_state.Get());
 context->OMSetRenderTargets(1,&target,nullptr);context->RSSetViewports(1,&viewport);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->PSSetShaderResources(0,1,&texture);context->PSSetSamplers(0,1,&sampling);context->PSSetConstantBuffers(0,1,&cb);
 for(float strength:{0.f,.65f}){
  struct Params{float strength,threshold;unsigned stereo,pad;} params{strength,.08f,1,0};context->UpdateSubresource(cb,0,nullptr,&params,0,0);context->Draw(3,0);context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));unsigned blended=0;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){auto p=reinterpret_cast<float *>(static_cast<char *>(mapped.pData)+y*mapped.RowPitch)+x*4;auto original=&data[(y*32+x)*4];if(std::abs(p[3]-.7f)>=1e-6f){printf("bad pixel %u,%u strength=%f rgba=%f %f %f %f\n",x,y,strength,p[0],p[1],p[2],p[3]);fflush(stdout);}assert(std::abs(p[3]-.7f)<1e-6f);
   if(strength==0||x>=16)for(unsigned c=0;c<4;++c)assert(std::abs(p[c]-original[c])<1e-6f);
   else{assert(p[0]>=0&&p[0]<=1);if(p[0]>0&&p[0]<1)++blended;}
   if(x==15&&y<10)assert(p[0]==0&&p[2]==0); // No neighboring-eye HDR bleed.
  }if(strength>0)assert(blended>0);context->Unmap(readback.Get(),0);
 }
 puts("PASS: actual spatial-AA shader pixels: disabled identity, smoothed diagonal edges, untouched flat HDR, alpha and separate-eye boundaries");
}
