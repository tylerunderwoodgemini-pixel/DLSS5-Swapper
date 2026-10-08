from pathlib import Path
root=Path(__file__).resolve().parents[1]
f=(root/'payload/vr-foveated/DLSS5_Feed.fx').read_text()
functions=f[f.index('float3 VR_RotatedRay('):f.index('float2 VR_ClampMotion(')]
fixture='''
#ifndef TEST_INVALID
#define TEST_INVALID 0
#endif
#ifndef TEST_REVERSED
#define TEST_REVERSED 1
#endif
#ifndef TEST_FULLPOSE
#define TEST_FULLPOSE 0
#endif
#ifndef TEST_FAST_CONF
#define TEST_FAST_CONF 0
#endif
#ifndef TEST_ENGINE_DEPTH
#define TEST_ENGINE_DEPTH 0
#endif
static const float BUFFER_WIDTH=4096,BUFFER_HEIGHT=2048;
static const float2 BUFFER_PIXEL_SIZE=float2(1.0/4096,1.0/2048);
static const float VR_POSE_VALID=1,VR_STEREO_DEPTH_VALID=TEST_INVALID==1?0:1,VR_POSE_STEREO=1,VR_POSE_SIGN=1,VR_POSE_SCALE=1,MV_SCALE=1;
static const float2 MV_SIGN=float2(1,1);
static const float4 VR_FOVEA_BOUNDS=float4(.2,.25,.6,.5);
static const float4 VR_DEPTH_TO_INVZ_L=TEST_ENGINE_DEPTH?float4(TEST_REVERSED?10:-10,TEST_REVERSED?0:10,0,1):0;
static const float4 VR_DEPTH_TO_INVZ_R=VR_DEPTH_TO_INVZ_L;
static const float3 VR_POSE_TRANSLATION_L=TEST_FULLPOSE?float3(.01,.007,.012):float3(.02,0,0),VR_POSE_TRANSLATION_R=TEST_FULLPOSE?float3(.009,.006,.013):float3(-.015,0,0);
static const float4 VR_POSE_PROJ_L=float4(-1,1,-1,1),VR_POSE_PROJ_R=float4(-.8,1.2,-1,1);
static const float4 VR_POSE_PREV_PROJ_L=VR_POSE_PROJ_L,VR_POSE_PREV_PROJ_R=VR_POSE_PROJ_R;
static const float4 VR_POSE_R0=TEST_FULLPOSE?float4(.999887502,0,.014999438,0):float4(1,0,0,0),VR_POSE_R1=float4(0,1,0,0),VR_POSE_R2=TEST_FULLPOSE?float4(-.014999438,0,.999887502,0):float4(0,0,1,0);
static const float4 VR_POSE_RIGHT_R0=VR_POSE_R0,VR_POSE_RIGHT_R1=VR_POSE_R1,VR_POSE_RIGHT_R2=VR_POSE_R2;
Texture2D<float4> sVR_DepthFitSamples:register(t0);
Texture2D<float4> sVR_DepthFit:register(t1);
#define tex2Dfetch(s,c) s.Load(int3(c,0))
float InverseDepth(float2 uv){return .1+.8*uv.y;}
float RawDepth(float2 uv){return TEST_INVALID==2?.04:(TEST_REVERSED==1?InverseDepth(uv)*.1:1-InverseDepth(uv)*.1);}
float2 ProviderMV(float2 uv){
 bool right=uv.x>=.5;float4 p=right?VR_POSE_PROJ_R:VR_POSE_PROJ_L;
 float3 r=float3(lerp(p.x,p.y,uv.x*2-(right?1:0)),1-2*uv.y,-1);
 float3 q=float3(dot(VR_POSE_R0.xyz,r),dot(VR_POSE_R1.xyz,r),dot(VR_POSE_R2.xyz,r));
 q+=(right?VR_POSE_TRANSLATION_R:VR_POSE_TRANSLATION_L)*InverseDepth(uv);
 float2 previous=float2(((-q.x/q.z-p.x)/(p.y-p.x)+(right?1:0))*.5,(1+q.y/q.z)*.5);
 return previous-uv;
}
float VR_ProviderConfidence(float2 uv){return TEST_INVALID==3?0:(TEST_FAST_CONF?.25:1);}
float Luma(float2 uv){return .5+.2*sin(uv.x*1000)+.1*sin(uv.y*700);}
float PreviousLuma(float2 uv){
 bool right=uv.x>=.5;float4 p=right?VR_POSE_PROJ_R:VR_POSE_PROJ_L;
 float3 r=float3(lerp(p.x,p.y,uv.x*2-(right?1:0)),1-2*uv.y,-1);
 float3 t=right?VR_POSE_TRANSLATION_R:VR_POSE_TRANSLATION_L;
 float3 a=VR_POSE_R0.xyz+t.x*float3(0,-.4,-.5);
 float3 b=VR_POSE_R1.xyz+t.y*float3(0,-.4,-.5);
 float3 c=VR_POSE_R2.xyz+t.z*float3(0,-.4,-.5);
 float3 current=cross(b,c)*r.x+cross(c,a)*r.y+cross(a,b)*r.z;
 current/=-current.z;
 return Luma(float2(((current.x-p.x)/(p.y-p.x)+(right?1:0))*.5,(1-current.y)*.5));
}
void VS(uint id:SV_VertexID,out float4 pos:SV_Position,out float2 uv:TEXCOORD){uv=float2((id<<1)&2,id&2);pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);}
'''
patch=f[f.index('float PatchError('):f.index('// Per-test failure')]
patch=patch.replace('tex2Dlod(sDLSS5_PrevLuma, float4(uv_prev + o, 0.0, 0.0)).x','PreviousLuma(uv_prev+o)')
(root/'tests/vr-6dof.generated.hlsl').write_text(fixture+patch+functions+'''
float4 PS_Pose(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{return VR_PoseMotion(uv);}
float4 PS_Bounds(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
 float2 p=uv+float2(-.05,0);return VR_InsideHistoryCrop(uv,p)?1:0;
}
''')

