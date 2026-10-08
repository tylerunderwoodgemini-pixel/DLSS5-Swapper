#define VR_DEPTH_TEST
#include "../payload/vr-foveated/native/vr-depth-bridge.cpp"
#include <cmath>
#include <cstdlib>
static void Check(bool ok,const char *name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
int main(){
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP device");
    for(auto format : {DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT})for(unsigned samples : {1u,2u}){
        UINT quality=0;dev->CheckMultisampleQualityLevels(format,samples,&quality);if(!quality)continue;
        Candidate c;c.desc.Width=8;c.desc.Height=4;c.desc.ArraySize=2;c.desc.MipLevels=1;c.desc.SampleDesc.Count=samples;c.desc.Format=format;c.desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;c.score=100;c.mask=3;
        Check(SUCCEEDED(dev->CreateTexture2D(&c.desc,nullptr,&c.texture)),"two-eye depth texture");
        ComPtr<ID3D11DepthStencilView> views[2];
        for(unsigned eye=0;eye<2;++eye){
            D3D11_DEPTH_STENCIL_VIEW_DESC d={};d.Format=format;
            if(samples>1){d.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;d.Texture2DMSArray.FirstArraySlice=eye;d.Texture2DMSArray.ArraySize=1;}
            else{d.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY;d.Texture2DArray.FirstArraySlice=eye;d.Texture2DArray.ArraySize=1;}
            Check(SUCCEEDED(dev->CreateDepthStencilView(c.texture.Get(),&d,&views[eye])),"independent eye DSV");
            ctx->ClearDepthStencilView(views[eye].Get(),D3D11_CLEAR_DEPTH,eye?0.75f:0.25f,0);
        }
        DepthState state;state.width=16;state.height=4;
        ctx->OMSetRenderTargets(0,nullptr,views[0].Get());
        Check(Snapshot(state,c,ctx.Get()),"capture exact stereo resource");
        for(auto &v:views)ctx->ClearDepthStencilView(v.Get(),D3D11_CLEAR_DEPTH,0,0);
        const D3D11_VIEWPORT original={1,2,3,4,0,1};ctx->RSSetViewports(1,&original);
        Check(Pack(state,ctx.Get()),"render preserved depth into packed SBS");
        D3D11_VIEWPORT restored={};UINT count=1;ctx->RSGetViewports(&count,&restored);Check(restored.TopLeftX==1 && restored.Height==4,"packing restores application viewport state");
        D3D11_TEXTURE2D_DESC rd={};state.packed->GetDesc(&rd);rd.BindFlags=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;Check(SUCCEEDED(dev->CreateTexture2D(&rd,nullptr,&read)),"readback");ctx->CopyResource(read.Get(),state.packed.Get());
        D3D11_MAPPED_SUBRESOURCE map={};Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"map packed depth");
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<16;++x){float v=*(float*)((char*)map.pData+y*map.RowPitch+x*4);Check(std::abs(v-(x<8?.25f:.75f))<0.00001f,"eye layers preserved despite source clear");}
        ctx->Unmap(read.Get(),0);state.fresh=false;Check(!Pack(state,ctx.Get()),"stale snapshot rejected");
        // Multipass stereo clears left before rendering right. Never re-copy the cleared eye.
        state.captured_mask=0;c.mask=1;c.score=100;
        ctx->ClearDepthStencilView(views[0].Get(),D3D11_CLEAR_DEPTH,.25f,0);
        Check(Snapshot(state,c,ctx.Get()) && !state.fresh,"one eye alone cannot publish stereo");
        ctx->ClearDepthStencilView(views[0].Get(),D3D11_CLEAR_DEPTH,0,0);
        ctx->ClearDepthStencilView(views[1].Get(),D3D11_CLEAR_DEPTH,.75f,0);c.mask=2;c.score=50;
        Check(Snapshot(state,c,ctx.Get()) && state.fresh && Pack(state,ctx.Get()),"lower-cost second eye completes preserved stereo pair");
        ctx->CopyResource(read.Get(),state.packed.Get());Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"multipass readback");
        Check(std::abs(*(float*)map.pData-.25f)<.00001f && std::abs(*((float*)map.pData+8)-.75f)<.00001f,"right-eye capture does not overwrite cleared left snapshot");ctx->Unmap(read.Get(),0);
        c.desc.ArraySize=1;Check(!StereoShape(c.desc,16,4),"single-eye texture cannot masquerade as both eyes");
        c.desc.ArraySize=2;c.desc.Height=5;Check(!StereoShape(c.desc,16,4),"wrong extent rejected");
    }
    // Unreal native stereo: an aligned depth allocation is larger than its
    // visible color rectangle. Preserve the visible eye seam, not half of the
    // allocation; padding must never move the right eye or enter the guide.
    {
        Candidate c;c.desc.Width=16;c.desc.Height=8;c.desc.ArraySize=1;c.desc.MipLevels=1;c.desc.SampleDesc.Count=1;c.desc.Format=DXGI_FORMAT_D32_FLOAT;c.desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;c.score=100;c.mask=3;
        float pixels[16*8];for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)pixels[y*16+x]=(y>=6||x>=14)?.875f:(x<7?.25f:.75f);
        D3D11_SUBRESOURCE_DATA data={pixels,16*4,0};Check(SUCCEEDED(dev->CreateTexture2D(&c.desc,&data,&c.texture)),"padded SBS depth allocation");
        DepthState state;state.width=14;state.height=6;
        Check(Snapshot(state,c,ctx.Get())&&Pack(state,ctx.Get()),"padded stereo depth captured and cropped");
        D3D11_TEXTURE2D_DESC rd={};state.packed->GetDesc(&rd);rd.BindFlags=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;Check(SUCCEEDED(dev->CreateTexture2D(&rd,nullptr,&read)),"padded readback");ctx->CopyResource(read.Get(),state.packed.Get());
        D3D11_MAPPED_SUBRESOURCE map={};Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"map padded guide");
        for(unsigned y=0;y<6;++y)for(unsigned x=0;x<14;++x)Check(std::abs(*(float*)((char*)map.pData+y*map.RowPitch+x*4)-(x<7?.25f:.75f))<.00001f,"visible eye seam preserved and padding excluded");
        ctx->Unmap(read.Get(),0);
        c.desc.Width=18;Check(!StereoShape(c.desc,14,6),"arbitrary larger allocation rejected");
        c.desc.Width=8;Check(!StereoShape(c.desc,14,6),"padded single-eye allocation rejected");
        c.desc.Width=6312;c.desc.Height=3352;Check(StereoShape(c.desc,6306,3350),"RetroRewind padded SBS extent accepted");
        c.desc.Width=3156;Check(!StereoShape(c.desc,6306,3350),"RetroRewind AFR single-eye still rejected");
    }
    // Actual viewports establish offsets and gaps that dimensions cannot infer.
    {
        Candidate c;c.desc.Width=24;c.desc.Height=10;c.desc.ArraySize=1;c.desc.MipLevels=1;c.desc.SampleDesc.Count=1;c.desc.Format=DXGI_FORMAT_D32_FLOAT;c.desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;c.score=100;c.mask=3;
        float pixels[24*10];for(unsigned y=0;y<10;++y)for(unsigned x=0;x<24;++x)pixels[y*24+x]=float(y*24+x)/256.f;
        D3D11_SUBRESOURCE_DATA data={pixels,24*4,0};Check(SUCCEEDED(dev->CreateTexture2D(&c.desc,&data,&c.texture)),"offset depth allocation");
        Check(!ResolveDepthLayout(c.desc,14,6).valid,"unknown large allocation is not guessed");
        c.rect_count=2;c.rects[0]={2,1,7,6,0};c.rects[1]={13,1,7,6,0};
        DepthState state;state.width=14;state.height=6;
        for(unsigned pass=0;pass<2;++pass){
            if(pass){c.rects[0].x=3;c.rects[0].y=2;c.rects[1].x=14;c.rects[1].y=2;}
            Check(Snapshot(state,c,ctx.Get())&&Pack(state,ctx.Get()),"observed offsets packed, including changed layout");
            D3D11_TEXTURE2D_DESC rd={};state.packed->GetDesc(&rd);rd.BindFlags=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> read;Check(SUCCEEDED(dev->CreateTexture2D(&rd,nullptr,&read)),"offset readback");ctx->CopyResource(read.Get(),state.packed.Get());
            D3D11_MAPPED_SUBRESOURCE map={};Check(SUCCEEDED(ctx->Map(read.Get(),0,D3D11_MAP_READ,0,&map)),"map offset guide");
            for(unsigned y=0;y<6;++y)for(unsigned x=0;x<14;++x){const auto &r=c.rects[x<7?0:1];float expected=pixels[(y+r.y)*24+x%7+r.x];Check(std::abs(*(float*)((char*)map.pData+y*map.RowPitch+x*4)-expected)<.00001f,"each output pixel uses its observed eye coordinates");}
            ctx->Unmap(read.Get(),0);
        }
        c.rects[1]=c.rects[0];Check(!ResolveDepthLayout(c.desc,14,6,c.rects,2).valid,"overlapping viewports cannot prove two eyes");
        c.rects[1]={23,0,7,6,0};Check(!ResolveDepthLayout(c.desc,14,6,c.rects,2).valid,"out-of-bounds viewport rejected");
        DepthDrawState left,right;left.current_mask=1;right.current_mask=2;left.viewport_count=right.viewport_count=1;
        left.viewports[0]={2,1,7,6,0,1};right.viewports[0]={3,2,7,6,0,1};
        Candidate layered;layered.desc=c.desc;layered.desc.ArraySize=2;
        ObserveEyeViewports(state,right,layered);ObserveEyeViewports(state,left,layered);
        auto layout=ResolveDepthLayout(layered.desc,14,6,layered.rects,layered.rect_count);
        Check(layout.valid&&layout.eye[0].layer==0&&layout.eye[1].layer==1&&layout.eye[0].x==2&&layout.eye[1].x==3,"independent draw contexts preserve eye and viewport association regardless of draw order");
    }
    puts("PASS: actual stereo depth snapshot/packing on WARP, D32/D24 and supported MSAA, source-clear preservation, freshness and stereo shape rejection");
}
