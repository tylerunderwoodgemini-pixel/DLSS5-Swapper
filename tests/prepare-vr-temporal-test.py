from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'payload/vr-foveated/Build-And-Install-VRUniversalFoveated-v26.ps1').read_text()
a=s.index('static bool VrHasPoseGuide(');b=s.index('static void VrPoseReset()',a)
(root/'tests/vr-history-guard.generated.h').write_text(s[a:b])
f=(root/'payload/vr-foveated/DLSS5_Feed.fx').read_text()
a=f.index('void PS_MotionVectors(');b=f.index('// End of the technique:',a)
# Retain the small deterministic provider/depth fixture and refresh the actual shader body.
p=root/'tests/vr-motion.generated.hlsl'
fixture=p.read_text().split('void PS_MotionVectors(')[0]
fixture=fixture.split('float VR_PoseHistoryRelief(')[0]
if 'float PatchError(' not in fixture:
    fixture+='float PatchError(float2 a,float2 b,out float contrast){contrast=0;return 0;}\n'
h=f.index('float VR_PoseHistoryRelief(');e=f.index('float RawDepth(',h)
fixture=fixture.replace('float VR_PoseHistoryRelief(float2 mv,bool depth_ok){return 0;}','')
selection=f[f.index('bool VR_CoherentObjectFlow('):a]
if 'bool VR_TranslationResolved(' not in fixture:
    fixture+='bool VR_TranslationResolved(float2 uv){return false;}\n'
if 'bool VR_InsideHistoryCrop(' not in fixture:
    fixture+='bool VR_InsideHistoryCrop(float2 uv,float2 previous_uv){return true;}\n'
if 'static const float2 BUFFER_PIXEL_SIZE' not in fixture:
    fixture+='static const float2 BUFFER_PIXEL_SIZE=1.0/BUFFER_SCREEN_SIZE;\n'
p.write_text(fixture+f[h:e]+selection+f[a:b].replace('ReShade::GetLinearizedDepth','GetLinearizedDepth'))
patch=f[f.index('float PatchError('):f.index('// Per-test failure')]
patch=patch.replace('tex2Dlod(sDLSS5_PrevLuma, float4(uv_prev + o, 0.0, 0.0)).x','PreviousLuma(uv_prev + o)')
motion_fixture='''
#ifndef TEST_REJECT_FLOW
#define TEST_REJECT_FLOW 0
#endif
#ifndef TEST_FLOW_X
#define TEST_FLOW_X 0.04
#endif
static const float2 BUFFER_SCREEN_SIZE=float2(64,16),BUFFER_PIXEL_SIZE=1.0/BUFFER_SCREEN_SIZE,MV_SIGN=float2(1,1);
#ifndef TEST_CALIBRATED
#define TEST_CALIBRATED 0
#endif
#ifndef TEST_LOCAL_FLOW
#define TEST_LOCAL_FLOW 0
#endif
bool VR_TranslationResolved(float2 uv){return TEST_CALIBRATED!=0;}
bool VR_InsideHistoryCrop(float2 a,float2 b){return all(b>=0)&&all(b<=1)&&((a.x<.5)==(b.x<.5));}
float4 VR_PoseMotion(float2 uv){return float4(.01,0,0,1);}
static const float MV_SCALE=1;
float2 ProviderMV(float2 uv){return float2(TEST_LOCAL_FLOW && abs(frac(uv.x*4)-.5)>.05? .01:TEST_FLOW_X,0);}
float Luma(float2 uv){return 0.5+0.2*sin(uv.x*100)+0.15*sin(uv.y*50);}
float PreviousLuma(float2 uv){return Luma(uv-float2(TEST_FLOW_X,0))+(TEST_REJECT_FLOW==3?.2:0);}
float4 ValidateTests(float2 uv,float2 mv){return float4(TEST_REJECT_FLOW==3?1:0,TEST_REJECT_FLOW==1 && abs(mv.x-.04)<.001?1:0,0,0);}
void VS(uint id:SV_VertexID,out float4 pos:SV_Position,out float2 uv:TEXCOORD) {
 uv=float2((id<<1)&2,id&2);pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
'''
(root/'tests/vr-parallax.generated.hlsl').write_text(motion_fixture+patch+selection+'''
float4 PS_Select(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target {
 float distrust;float2 motion=VR_SelectPoseMotion(uv,float2(.01,0),distrust);return float4(motion,distrust,1);
}
''')
# Compile/render the real pose shader with opposite eye rotations and different previous FOVs.
a=f.index('float4 VR_PoseMotion(');b=f.index('float2 VR_ClampMotion(',a)
pose_fixture='''
static const float VR_POSE_STEREO=1,VR_POSE_SIGN=1,VR_POSE_SCALE=1;
static const float4 VR_POSE_PROJ_L=float4(-1,1,-1,1),VR_POSE_PROJ_R=float4(-1,1,-1,1);
static const float4 VR_POSE_PREV_PROJ_L=float4(-1.2,1.2,-1,1),VR_POSE_PREV_PROJ_R=float4(-0.8,0.8,-1,1);
static const float4 VR_POSE_R0=float4(0.995004165,0,0.099833416,0),VR_POSE_R1=float4(0,1,0,0),VR_POSE_R2=float4(-0.099833416,0,0.995004165,0);
static const float4 VR_POSE_RIGHT_R0=float4(0.995004165,0,-0.099833416,0),VR_POSE_RIGHT_R1=float4(0,1,0,0),VR_POSE_RIGHT_R2=float4(0.099833416,0,0.995004165,0);
static const float VR_STEREO_DEPTH_VALID=0;
static const float3 VR_POSE_TRANSLATION_L=0,VR_POSE_TRANSLATION_R=0;
#define tex2Dfetch(s,c) float4(0,0,0,0)
float RawDepth(float2 uv){return 0;}
'''
(root/'tests/vr-pose-motion.generated.hlsl').write_text(pose_fixture+f[a:b]+'''
float4 PS_Pose(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target {return VR_PoseMotion(uv);}
''')
