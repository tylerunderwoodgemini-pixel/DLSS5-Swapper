// Original-authored current-frame edge AA. No synthetic jitter or temporal history.
#include "ReShade.fxh"
uniform float AA_STRENGTH < ui_type="slider"; ui_min=0.0; ui_max=1.0; ui_label="Edge AA strength"; > = 0.65;
uniform float AA_THRESHOLD < ui_type="slider"; ui_min=0.02; ui_max=0.20; ui_label="Edge contrast threshold"; > = 0.08;
uniform bool AA_PACKED_STEREO < ui_label="Packed side-by-side eyes"; > = true;
float2 AAClamp(float2 uv,float2 center) {
 float2 minimum=BUFFER_PIXEL_SIZE*0.5,maximum=1.0-minimum;
 if(AA_PACKED_STEREO){if(center.x<0.5)maximum.x=0.5-minimum.x;else minimum.x=0.5+minimum.x;}
 return clamp(uv,minimum,maximum);
}
float4 AARead(float2 uv,float2 center){return tex2Dlod(ReShade::BackBuffer,float4(AAClamp(uv,center),0,0));}
float AALuma(float3 color){return dot(color,float3(0.299,0.587,0.114));}
float4 SpatialAA(float4 position:SV_Position,float2 uv:TEXCOORD):SV_Target {
 float4 original=AARead(uv,uv);if(AA_STRENGTH<=0)return original;
 float2 pixel=BUFFER_PIXEL_SIZE;
 float3 n=AARead(uv-float2(0,pixel.y),uv).rgb,s=AARead(uv+float2(0,pixel.y),uv).rgb;
 float3 w=AARead(uv-float2(pixel.x,0),uv).rgb,e=AARead(uv+float2(pixel.x,0),uv).rgb;
 float l=AALuma(original.rgb),ln=AALuma(n),ls=AALuma(s),lw=AALuma(w),le=AALuma(e);
 float low=min(l,min(min(ln,ls),min(lw,le))),high=max(l,max(max(ln,ls),max(lw,le)));
 float contrast=high-low;if(contrast<max(0.025,AA_THRESHOLD*max(abs(high),abs(low))))return original;
 // Sample across the stronger luminance gradient, within this eye only.
 float2 direction=abs(lw-le)>=abs(ln-ls)?float2(pixel.x,0):float2(0,pixel.y);
 float3 filtered=0.5*(AARead(uv-direction*0.5,uv).rgb+AARead(uv+direction*0.5,uv).rgb);
 float3 minimum=min(original.rgb,min(min(n,s),min(w,e))),maximum=max(original.rgb,max(max(n,s),max(w,e)));
 filtered=clamp(filtered,minimum,maximum);
 return float4(lerp(original.rgb,filtered,AA_STRENGTH),original.a);
}
technique DLSS5_SpatialAA < ui_label="Whole-view edge AA (spatial)"; ui_tooltip="Place after DLSS 5 Feed. Smooths current-frame edges in the center and periphery. No history or sharpening. This is spatial AA, not engine-integrated DLAA."; > {
 pass EdgeAA { VertexShader=PostProcessVS; PixelShader=SpatialAA; }
}
