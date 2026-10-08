#pragma once
#include <cmath>
#include <vector>
#include <cstddef>
struct DepthProjection {float focal_x,focal_y,offset_x,offset_y,scale,bias;};
inline bool DecodeDepthProjection(const float *v,bool transpose,DepthProjection &out){
 float m[4][4];for(int y=0;y<4;++y)for(int x=0;x<4;++x){m[y][x]=v[transpose?x*4+y:y*4+x];if(!std::isfinite(m[y][x]))return false;}
 const float eps=0.0001f;
 if(std::abs(m[0][1])>eps||std::abs(m[1][0])>eps||std::abs(m[0][3])>eps||std::abs(m[1][3])>eps||
    std::abs(m[2][0])>eps||std::abs(m[2][1])>eps||std::abs(m[3][0])>eps||std::abs(m[3][1])>eps||std::abs(m[3][3])>eps||
    std::abs(std::abs(m[3][2])-1)>eps||std::abs(m[0][0])<.1f||std::abs(m[0][0])>10||std::abs(m[1][1])<.1f||std::abs(m[1][1])>10||
    std::abs(m[2][3])<1e-6f||std::abs(m[2][3])>1000)return false;
 float base=m[2][2]/m[3][2],scale=1/m[2][3],bias=-base/m[2][3];
 // Valid perspective depth must map at least one endpoint to a positive distance.
 // Permit small negative inverse-far values used by infinite/reversed projections.
 if(base<-.01f||base>1.01f||std::fmax(bias,scale+bias)<=0)return false;
 out={std::abs(m[0][0]),std::abs(m[1][1]),m[0][2]/m[3][2],m[1][2]/m[3][2],scale,bias};return true;
}
inline std::vector<DepthProjection> FindDepthProjections(const void *data,size_t bytes){
 std::vector<DepthProjection> result;if(!data||bytes<64||bytes>65536)return result;
 const auto *floats=static_cast<const float *>(data);
 for(size_t offset=0;offset+16<=bytes/4;offset+=4)for(bool transpose:{false,true}){
  DepthProjection p;if(DecodeDepthProjection(floats+offset,transpose,p))result.push_back(p);
 }return result;
}
inline bool MatchesProjection(const DepthProjection &p,const float *fov){
 float fx=2/(fov[1]-fov[0]),fy=2/(fov[3]-fov[2]);
 float ox=(fov[1]+fov[0])/(fov[1]-fov[0]),oy=(fov[3]+fov[2])/(fov[3]-fov[2]);
 return std::isfinite(fx)&&std::isfinite(fy)&&std::abs(p.focal_x-std::abs(fx))<.002f*std::abs(fx)&&
   std::abs(p.focal_y-std::abs(fy))<.002f*std::abs(fy)&&std::abs(std::abs(p.offset_x)-std::abs(ox))<.002f&&std::abs(std::abs(p.offset_y)-std::abs(oy))<.002f;
}
