#pragma once
#include <d3d11.h>
#include "pose-data.h"
// Successful reads transfer one COM reference per eye to the consumer.
struct SubmittedDepthImages {DepthSubmissionInfo metadata{};ID3D11Texture2D *texture[2]{};};
using ReadSubmittedDepthImages=bool(__cdecl *)(SubmittedDepthImages *,unsigned);
struct SubmittedDepthConversion {float multiply=1,add=0,inverse_scale=0,inverse_bias=0;};
inline bool SubmittedConversion(const DepthEyeInfo &eye,bool reversed,SubmittedDepthConversion &out){
 if(!std::isfinite(eye.min_depth)||!std::isfinite(eye.max_depth)||eye.min_depth<0||eye.max_depth>1||eye.max_depth<=eye.min_depth||std::isnan(eye.near_z)||std::isnan(eye.far_z)||eye.near_z<=0||eye.far_z<=0||eye.near_z==eye.far_z)return false;
 const double near_inverse=1.0/eye.near_z,far_inverse=1.0/eye.far_z;
 const double low=std::fmin(near_inverse,far_inverse),high=std::fmax(near_inverse,far_inverse),range=high-low;
 if(!std::isfinite(range)||range<=0)return false;
 const double scale=(far_inverse-near_inverse)/(eye.max_depth-eye.min_depth),bias=near_inverse-scale*eye.min_depth;
 out.multiply=float((reversed?scale:-scale)/range);
 out.add=float(reversed?(bias-low)/range:(high-bias)/range);
 out.inverse_scale=float(reversed?range:-range);out.inverse_bias=float(reversed?low:high);return true;
}
