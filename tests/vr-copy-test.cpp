#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "vr-generated-test.h"
using Microsoft::WRL::ComPtr;
void check(bool ok,const char* what) { if (!ok) { std::printf("FAIL: %s\n",what); std::exit(1); } }
int main() {
    check(VrCopyFormatsCompatible(DXGI_FORMAT_B8G8R8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_UNORM),"Freeland BGRA typeless/UNORM");
    check(VrCopyFormatsCompatible(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM),"BGRA sRGB storage");
    check(!VrCopyFormatsCompatible(DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM),"reject channel swizzle");
    check(!VrCopyFormatsCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_UNORM),"reject numeric reinterpretation");
    check(VrCopyFormatsCompatible(DXGI_FORMAT_R10G10B10A2_TYPELESS,DXGI_FORMAT_R10G10B10A2_UNORM),"10-bit family");
    check(VrCopyFormatsCompatible(DXGI_FORMAT_R16G16B16A16_TYPELESS,DXGI_FORMAT_R16G16B16A16_FLOAT),"FP16 family");
    check(!VrCopyFormatsCompatible(DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_UNKNOWN),"reject unknown");
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP device");
    struct Format { DXGI_FORMAT a,b; unsigned bytes; } formats[] = {
        {DXGI_FORMAT_B8G8R8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_UNORM,4},
        {DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM,4},
        {DXGI_FORMAT_B8G8R8X8_TYPELESS,DXGI_FORMAT_B8G8R8X8_UNORM,4},
        {DXGI_FORMAT_R10G10B10A2_TYPELESS,DXGI_FORMAT_R10G10B10A2_UNORM,4},
        {DXGI_FORMAT_R16G16B16A16_TYPELESS,DXGI_FORMAT_R16G16B16A16_FLOAT,8}
    };
    for (auto f:formats) {
        std::vector<unsigned char> pixels(16*8*f.bytes);
        for(unsigned y=0;y<8;y++) for(unsigned x=0;x<16;x++)
            for(unsigned c=0;c<f.bytes;c++) pixels[(y*16+x)*f.bytes+c]=(unsigned char)(x+17*y+3*c);
        D3D11_TEXTURE2D_DESC d={}; d.Width=16; d.Height=8; d.MipLevels=1; d.ArraySize=1;
        d.SampleDesc.Count=1; d.Format=f.a; d.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA initial={pixels.data(),16*f.bytes,0};
        ComPtr<ID3D11Texture2D> source, packed, staging;
        check(SUCCEEDED(dev->CreateTexture2D(&d,&initial,&source)),"source texture");
        d.Width=8; d.Height=4; d.Format=f.b;
        check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&packed)),"packed texture");
        D3D11_BOX left={2,2,0,6,6,1},right={10,2,0,14,6,1};
        ctx->CopySubresourceRegion(packed.Get(),0,0,0,0,source.Get(),0,&left);
        ctx->CopySubresourceRegion(packed.Get(),0,4,0,0,source.Get(),0,&right);
        d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&staging)),"readback texture");
        ctx->CopyResource(staging.Get(),packed.Get());
        D3D11_MAPPED_SUBRESOURCE map={};
        check(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"readback");
        for(unsigned y=0;y<4;y++) for(unsigned x=0;x<8;x++) for(unsigned c=0;c<f.bytes;c++) {
            auto expected=pixels[((y+2)*16+(x<4?x+2:x+6))*f.bytes+c];
            check(((unsigned char*)map.pData)[y*map.RowPitch+x*f.bytes+c]==expected,"independent current-frame eye crops");
        }
        ctx->Unmap(staging.Get(),0);
    }
    ComPtr<ID3DBlob> shader,errors;
    auto hr=D3DCompile(kBlendSrc,sizeof(kBlendSrc)-1,"vr-test",nullptr,nullptr,"ps_vr_feather","ps_4_0",0,0,&shader,&errors);
    if(FAILED(hr)&&errors) std::printf("%s\n",(char*)errors->GetBufferPointer());
    check(SUCCEEDED(hr),"copy-home shader compiles");
    ComPtr<ID3D11PixelShader> ps; check(SUCCEEDED(dev->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&ps)),"feather pixel shader");
    const char vsText[]="struct O { float4 pos:SV_Position; float2 uv:TEXCOORD0; }; O main(uint id:SV_VertexID) { O o; o.uv=float2((id<<1)&2,id&2); o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o; }";
    check(SUCCEEDED(D3DCompile(vsText,sizeof(vsText)-1,nullptr,nullptr,nullptr,"main","vs_4_0",0,0,&shader,&errors)),"test vertex shader");
    ComPtr<ID3D11VertexShader> vs; check(SUCCEEDED(dev->CreateVertexShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&vs)),"vertex shader");
    D3D11_TEXTURE2D_DESC d={}; d.Width=8; d.Height=4; d.MipLevels=1; d.ArraySize=1; d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_B8G8R8A8_UNORM; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned> eyePixels(32); for(unsigned y=0;y<4;y++) for(unsigned x=0;x<8;x++) eyePixels[y*8+x]=x<4?0xffff0000:0xff0000ff;
    D3D11_SUBRESOURCE_DATA initial={eyePixels.data(),32,0};
    ComPtr<ID3D11Texture2D> packed,target,readback;
    check(SUCCEEDED(dev->CreateTexture2D(&d,&initial,&packed)),"neural eye output");
    ComPtr<ID3D11ShaderResourceView> srv; check(SUCCEEDED(dev->CreateShaderResourceView(packed.Get(),nullptr,&srv)),"output SRV");
    d.Width=16; d.Height=8; d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS; d.BindFlags=D3D11_BIND_RENDER_TARGET;
    check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&target)),"Freeland typeless target");
    D3D11_RENDER_TARGET_VIEW_DESC rd={}; rd.Format=DXGI_FORMAT_B8G8R8A8_UNORM; rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv; check(SUCCEEDED(dev->CreateRenderTargetView(target.Get(),&rd,&rtv)),"typed target RTV");
    const float native[4]={0,1,0,1}; ctx->ClearRenderTargetView(rtv.Get(),native);
    D3D11_BUFFER_DESC bd={}; bd.ByteWidth=32; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    float fovea[8]={.5f,.5f,.25f,.25f,1,0,0,0}; initial={fovea,0,0};
    ComPtr<ID3D11Buffer> cb; check(SUCCEEDED(dev->CreateBuffer(&bd,&initial,&cb)),"crop constants");
    D3D11_BLEND_DESC blend={}; auto &b=blend.RenderTarget[0]; b.BlendEnable=TRUE;
    b.SrcBlend=D3D11_BLEND_SRC_ALPHA; b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; b.BlendOp=D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha=D3D11_BLEND_ONE; b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> bs; check(SUCCEEDED(dev->CreateBlendState(&blend,&bs)),"feather blend");
    D3D11_SAMPLER_DESC sd={}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler; check(SUCCEEDED(dev->CreateSamplerState(&sd,&sampler)),"sampler");
    auto rt=rtv.Get(); auto sr=srv.Get(); auto cbp=cb.Get(); auto sp=sampler.Get();
    D3D11_VIEWPORT vp={0,0,16,8,0,1};
    ctx->OMSetRenderTargets(1,&rt,nullptr); ctx->OMSetBlendState(bs.Get(),nullptr,~0u);
    ctx->RSSetViewports(1,&vp); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0);
    ctx->PSSetShaderResources(0,1,&sr); ctx->PSSetSamplers(0,1,&sp); ctx->PSSetConstantBuffers(0,1,&cbp); ctx->Draw(3,0);
    ctx->OMSetRenderTargets(0,nullptr,nullptr);
    d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&readback)),"composite readback"); ctx->CopyResource(readback.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE map={}; check(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)),"composite map");
    auto pixel=[&](unsigned x,unsigned y) { return *(unsigned*)((char*)map.pData+y*map.RowPitch+x*4); };
    check(pixel(4,4)==0xffff0000,"left eye center red"); check(pixel(12,4)==0xff0000ff,"right eye center blue");
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<16;x++)
        if(y<2||y>=6||x%8<2||x%8>=6) check(pixel(x,y)==0xff00ff00,"outer current native frame preserved");
    ctx->Unmap(readback.Get(),0);
    for(float weight : {0.f,0.5f}) {
        fovea[4]=weight;ctx->UpdateSubresource(cb.Get(),0,nullptr,fovea,0,0);
        ctx->ClearRenderTargetView(rtv.Get(),native);
        ctx->OMSetRenderTargets(1,&rt,nullptr);ctx->Draw(3,0);ctx->OMSetRenderTargets(0,nullptr,nullptr);
        ctx->CopyResource(readback.Get(),target.Get());check(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)),"presentation weight readback");
        if(weight==0) {
            for(unsigned y=0;y<8;y++)for(unsigned x=0;x<16;x++)check(pixel(x,y)==0xff00ff00,"native fallback preserves every current-frame pixel");
        } else {
            const unsigned left=pixel(4,4),right=pixel(12,4);
            check(((left>>16)&255)>=127 && ((left>>8)&255)>=127 && (left&255)==0,"left-eye current-frame fade");
            check((right&255)>=127 && ((right>>8)&255)>=127 && ((right>>16)&255)==0,"right-eye current-frame fade");
        }
        ctx->Unmap(readback.Get(),0);
    }
    std::puts("PASS: DXGI guards, stereo crop copies in five formats, actual BGRA feather rendering, independent eye centers and native outer pixels");
}
