// Included in the feeder after its D3D11 VR helpers. Uses the same queue and
// crop geometry; never scales vectors or copies pixels between eyes.
static ID3D12RootSignature *g_vr12_root;
static ID3D12PipelineState *g_vr12_pso;
static ID3D12DescriptorHeap *g_vr12_srv, *g_vr12_rtv;
static void Vr12Release()
{
    SafeRelease(g_vr12_pso); SafeRelease(g_vr12_root);
    SafeRelease(g_vr12_srv); SafeRelease(g_vr12_rtv);
}
static bool Vr12Prepare()
{
    const auto shader = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (!g.tex12[SLOT_MV] && !MakeTex12(SLOT_MV,g.width,g.height,DXGI_FORMAT_R16G16_FLOAT,false,shader)) return false;
    if (!g.tex12[SLOT_DEPTH] && !MakeTex12(SLOT_DEPTH,g.width,g.height,DXGI_FORMAT_R32_FLOAT,false,shader)) return false;
    if (!g.tex12[SLOT_MASK] && !MakeTex12(SLOT_MASK,g.width,g.height,DXGI_FORMAT_R8_UNORM,false,shader)) return false;
    if (g_vr12_pso) return true;
    auto compiler=LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile=compiler?reinterpret_cast<pD3DCompile>(GetProcAddress(compiler,"D3DCompile")):nullptr;
    auto serialize=reinterpret_cast<decltype(&D3D12SerializeRootSignature)>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"),"D3D12SerializeRootSignature"));
    if (!compile || !serialize) return false;
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
    const char *ps=R"(
Texture2D<float4> image:register(t0);
cbuffer Params:register(b0){float ox,oy,cw,ch,eye,weight,fx,fy;};
float4 main(float4 p:SV_Position):SV_Target {
 float2 local=p.xy-float2(ox,oy);
 float2 edge=min(local,float2(cw,ch)-local);
 float alpha=smoothstep(0,fx,edge.x)*smoothstep(0,fy,edge.y)*weight;
 return float4(image.Load(int3(int2(local)+int2(eye*cw,0),0)).rgb,alpha);
})";
    ID3DBlob *vb=nullptr,*pb=nullptr,*rb=nullptr,*errors=nullptr;
    HRESULT hr=compile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vb,&errors);
    SafeRelease(errors);
    if(SUCCEEDED(hr))hr=compile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&pb,&errors);
    SafeRelease(errors);
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER parameters[2]={};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={1,&range};parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,8};parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root={2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    if(SUCCEEDED(hr))hr=serialize(&root,D3D_ROOT_SIGNATURE_VERSION_1,&rb,&errors);
    SafeRelease(errors);
    if(SUCCEEDED(hr))hr=g.dev12->CreateRootSignature(0,rb->GetBufferPointer(),rb->GetBufferSize(),IID_PPV_ARGS(&g_vr12_root));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline={};pipeline.pRootSignature=g_vr12_root;
    if(vb)pipeline.VS={vb->GetBufferPointer(),vb->GetBufferSize()};
    if(pb)pipeline.PS={pb->GetBufferPointer(),pb->GetBufferSize()};
    pipeline.SampleMask=UINT_MAX;pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=g.output_fmt;pipeline.SampleDesc.Count=1;
    pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable=TRUE;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    auto &blend=pipeline.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
    blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=7;
    if(SUCCEEDED(hr))hr=g.dev12->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&g_vr12_pso));
    SafeRelease(vb);SafeRelease(pb);SafeRelease(rb);
    D3D12_DESCRIPTOR_HEAP_DESC heap={};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(SUCCEEDED(hr))hr=g.dev12->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&g_vr12_srv));
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if(SUCCEEDED(hr))hr=g.dev12->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&g_vr12_rtv));
    if(FAILED(hr)){Log("[feed] D3D12 VR compositor creation failed: 0x%08X",hr);Vr12Release();return false;}
    D3D12_SHADER_RESOURCE_VIEW_DESC view={};view.Format=g.output_fmt;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
    g.dev12->CreateShaderResourceView(g.tex12[SLOT_OUTPUT],&view,g_vr12_srv->GetCPUDescriptorHandleForHeapStart());
    return true;
}
static void Vr12CopyEyes(reshade::api::command_list *cl,reshade::api::resource source,reshade::api::resource dest,
                         UINT source_w,UINT source_h,UINT crop_w,UINT crop_h,bool home)
{
    const UINT ew=source_w/2,ix=(ew-crop_w)/2,iy=(source_h-crop_h)/2;
    for(UINT eye=0;eye<2;++eye){
        const UINT sx=home?eye*crop_w:eye*ew+ix, sy=home?0:iy;
        const UINT dx=home?eye*ew+ix:eye*crop_w, dy=home?iy:0;
        const reshade::api::subresource_box from={sx,sy,0,sx+crop_w,sy+crop_h,1};
        const reshade::api::subresource_box to={dx,dy,0,dx+crop_w,dy+crop_h,1};
        cl->copy_texture_region(source,0,&from,dest,0,&to,reshade::api::filter_mode::min_mag_mip_point);
    }
}
static void Vr12PackGuide(reshade::api::command_list *cl,ID3D12Resource *source,int slot,
                         UINT sw,UINT sh,UINT cw,UINT ch)
{
    using namespace reshade::api;
    const resource r[2]={{reinterpret_cast<uint64_t>(source)},{reinterpret_cast<uint64_t>(g.tex12[slot])}};
    const resource_usage before[2]={resource_usage::shader_resource,resource_usage::shader_resource};
    const resource_usage copy[2]={resource_usage::copy_source,resource_usage::copy_dest};
    cl->barrier(2,r,before,copy);Vr12CopyEyes(cl,r[0],r[1],sw,sh,cw,ch,false);cl->barrier(2,r,copy,before);
}
static void Vr12Composite(ID3D12Resource *bb,UINT sw,UINT sh,UINT cw,UINT ch,float weight)
{
    // Recorded after NGX on the same native queue; peripheral pixels stay from
    // this frame. No stale-image or cross-eye history blend is performed here.
    if(weight<=0)return;
    Barrier(bb,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(g.tex12[SLOT_OUTPUT],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_RENDER_TARGET_VIEW_DESC view={};view.Format=g.output_fmt;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto target=g_vr12_rtv->GetCPUDescriptorHandleForHeapStart();g.dev12->CreateRenderTargetView(bb,&view,target);
    g.list->SetGraphicsRootSignature(g_vr12_root);g.list->SetPipelineState(g_vr12_pso);
    ID3D12DescriptorHeap *heaps[]={g_vr12_srv};g.list->SetDescriptorHeaps(1,heaps);
    g.list->SetGraphicsRootDescriptorTable(0,g_vr12_srv->GetGPUDescriptorHandleForHeapStart());
    g.list->OMSetRenderTargets(1,&target,FALSE,nullptr);g.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT ew=sw/2,ix=(ew-cw)/2,iy=(sh-ch)/2;
    for(UINT eye=0;eye<2;++eye){
        const UINT x=eye*ew+ix;
        const float constants[8]={float(x),float(iy),float(cw),float(ch),float(eye),weight,float(cw)*0.08f,float(ch)*0.08f};
        const D3D12_VIEWPORT viewport={float(x),float(iy),float(cw),float(ch),0,1};
        const D3D12_RECT rect={LONG(x),LONG(iy),LONG(x+cw),LONG(iy+ch)};
        g.list->RSSetViewports(1,&viewport);g.list->RSSetScissorRects(1,&rect);
        g.list->SetGraphicsRoot32BitConstants(1,8,constants,0);g.list->DrawInstanced(3,1,0,0);
    }
    Barrier(g.tex12[SLOT_OUTPUT],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(bb,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
}
