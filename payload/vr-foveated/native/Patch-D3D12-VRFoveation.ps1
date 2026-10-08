param([string]$Text)
$dimensions = @'
    const UINT w = static_cast<UINT>(cd.Width), h = cd.Height;
    UINT crop_w=0,crop_h=0,work_w=w,work_h=h;
    const bool vr_foveated=rt->get_hwnd()==0 && g_cfg.mode>=2 &&
        cd.Format!=DXGI_FORMAT_R10G10B10A2_UNORM &&
        VrFoveatedLayout(w,h,&crop_w,&crop_h,&work_w,&work_h);
    if(!vr_foveated){work_w=w;work_h=h;}
'@
$Text = Replace-LiteralOnce $Text '    const UINT w = static_cast<UINT>(cd.Width), h = cd.Height;' $dimensions 'D3D12 VR work geometry'
$Text = Replace-LiteralOnce $Text 'w != g.width || h != g.height || cd.Format != g.bb_fmt' 'work_w != g.width || work_h != g.height || cd.Format != g.bb_fmt' 'D3D12 packed rebuild dimensions'
$Text = Replace-LiteralOnce $Text 'ok = BuildResources12(w, h, cd.Format);' @'
ok = BuildResources12(work_w, work_h, cd.Format);
        if(ok && vr_foveated)ok=Vr12Prepare();
        if(ok && vr_foveated)Log("[feed] D3D12 VR FOVEATION ACTIVE: source=%ux%u center=%ux%u per eye packed=%ux%u; color/depth/motion/mask share current-frame crop",w,h,crop_w,crop_h,work_w,work_h);
'@ 'D3D12 packed resources'
$Text = Replace-LiteralOnce $Text '        cl->copy_resource(bb_res, color12);' @'
        if(vr_foveated)Vr12CopyEyes(cl,bb_res,color12,w,h,crop_w,crop_h,false);
        else cl->copy_resource(bb_res,color12);
'@ 'D3D12 color crop'
$Text = Replace-LiteralOnce $Text '            g.rs_queue->flush_immediate_command_list();' @'
            if(vr_foveated){
                Vr12PackGuide(cl,mv,SLOT_MV,w,h,crop_w,crop_h);
                Vr12PackGuide(cl,depth,SLOT_DEPTH,w,h,crop_w,crop_h);
                if(g.mask_ok)Vr12PackGuide(cl,mask,SLOT_MASK,w,h,crop_w,crop_h);
                mv=g.tex12[SLOT_MV];depth=g.tex12[SLOT_DEPTH];
                if(g.mask_ok)mask=g.tex12[SLOT_MASK];
            }
            g.rs_queue->flush_immediate_command_list();
'@ 'D3D12 crop all temporal guides'
$Text = Replace-LiteralOnce $Text '                    submitted = EndCommands();' @'
                    if(vr_foveated && NVSDK_NGX_SUCCEED(re))Vr12Composite(bb,w,h,crop_w,crop_h,1.0f);
                    submitted = EndCommands();
'@ 'D3D12 current-frame feather composite'
$Text = Replace-LiteralOnce $Text "                    // The copy home is recorded on the (fresh) immediate list: it executes on" @'
                    if(vr_foveated){
                        const resource_usage from=resource_usage::copy_dest,to=resource_usage::render_target;
                        cl->barrier(1,&bb_res,&from,&to);
                    }else{
                    // The copy home is recorded on the (fresh) immediate list: it executes on
'@ 'D3D12 avoid full-frame copy home'
$Text = Replace-LiteralOnce $Text '                    restored = true;' "                    }`n                    restored = true;" 'D3D12 compositor state restore'
$Text = Replace-LiteralOnce $Text 'ep.InMVScaleX        = g_cfg.mv_scale_x;' 'ep.InMVScaleX        = VrMotionScale(rt,g_cfg.mv_scale_x);' 'D3D12 exact pose MV X'
$Text = Replace-LiteralOnce $Text 'ep.InMVScaleY        = g_cfg.mv_scale_y;' 'ep.InMVScaleY        = VrMotionScale(rt,g_cfg.mv_scale_y);' 'D3D12 exact pose MV Y'
$Text
