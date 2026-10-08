#define VR_PROJECTION_TEST
#include "../payload/vr-foveated/native/vr-projection-bridge.cpp"
#include <cstdlib>
void check(bool v,const char *s){if(!v){printf("FAIL: %s\n",s);exit(1);}}
int main(){
 float reversed[16]={1,0,.1f,0,0,1,0,0,0,0,0,.01f,0,0,-1,0};
 float forward[16]={1,0,.1f,0,0,1,0,0,0,0,-1.00010001f,-.100010001f,0,0,-1,0};
 float fov[2][4]={{-.9f,1.1f,-1,1},{-.9f,1.1f,-1,1}};
 DepthProjection p;check(DecodeDepthProjection(reversed,false,p)&&MatchesProjection(p,fov[0]),"recognizes matching reversed infinite perspective");
 check(std::abs(p.scale-100)<.001f&&p.bias==0,"inverse Z is raw depth / near for reversed infinite projection");
 check(DecodeDepthProjection(forward,false,p)&&std::abs((p.scale*0+p.bias)-10)<.001f&&std::abs((p.scale+p.bias)-.001f)<.00001f,"forward finite projection produces physical near/far distances");
 float transposed[16];for(int y=0;y<4;++y)for(int x=0;x<4;++x)transposed[y*4+x]=forward[x*4+y];check(DecodeDepthProjection(transposed,true,p),"column major matrices decoded");
 auto candidates=FindDepthProjections(forward,sizeof(forward));float result[2][4]{};check(Resolve(candidates,fov,result)==3&&result[0][3]==1,"consistent conversion assigned to both matching eyes");
 auto conflict=FindDepthProjections(reversed,sizeof(reversed));candidates.insert(candidates.end(),conflict.begin(),conflict.end());check(Resolve(candidates,fov,result)==0,"conflicting perspective constants never select a guessed conversion");
 float bad[16];memcpy(bad,forward,sizeof(bad));bad[15]=1;check(!DecodeDepthProjection(bad,false,p),"orthographic/affine matrices rejected");bad[15]=0;bad[3]=5;check(!DecodeDepthProjection(bad,false,p),"view transform rejected");bad[3]=0;bad[0]=NAN;check(!DecodeDepthProjection(bad,false,p),"nonfinite rejected");
 check(DecodeDepthProjection(reversed,false,p),"valid projection");float wrongfov[4]={-2,2,-1,1};check(!MatchesProjection(p,wrongfov),"shadow/other camera FOV rejected");
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP");
 D3D11_BUFFER_DESC d{};d.ByteWidth=sizeof(forward);d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA init{forward,0,0};ComPtr<ID3D11Buffer> buffer;check(SUCCEEDED(dev->CreateBuffer(&d,&init,&buffer)),"game camera constant buffer");
 auto b=buffer.Get();ctx->VSSetConstantBuffers(0,1,&b);Copy copy;check(CaptureBuffer(ctx.Get(),b,copy),"snapshot bound camera constants");D3D11_MAPPED_SUBRESOURCE map{};check(SUCCEEDED(ctx->Map(copy.stage.Get(),0,D3D11_MAP_READ,0,&map)),"read snapshot");
 auto gpu=FindDepthProjections(map.pData,sizeof(forward));ctx->Unmap(copy.stage.Get(),0);float value[2][4]{};check(Resolve(gpu,fov,value)==3&&std::abs(value[0][1]-10)<.001f,"actual GPU camera buffer yields physical inverse depth");
 ComPtr<ID3D11Buffer> bound;ctx->VSGetConstantBuffers(0,1,&bound);check(bound.Get()==buffer.Get(),"capture leaves application bindings unchanged");
 puts("PASS: actual D3D11 projection capture, physical reversed/forward depth, transpose, FOV/ambiguity rejection and bindings");
}
