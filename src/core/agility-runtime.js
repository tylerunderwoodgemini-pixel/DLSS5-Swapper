'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto');
// Read exported data without loading or executing the target image.
function inspect(file) {
  try {
    const b=fs.readFileSync(file),p=b.readUInt32LE(60);
    if(b.readUInt16LE(0)!==0x5a4d||b.readUInt32LE(p)!==0x4550)return null;
    const o=p+24,magic=b.readUInt16LE(o),wide=magic===0x20b;
    if(!wide&&magic!==0x10b)return null;
    const base=wide?Number(b.readBigUInt64LE(o+24)):b.readUInt32LE(o+28);
    const sections=p+24+b.readUInt16LE(p+20),count=b.readUInt16LE(p+6);
    const offset=r=>{
      for(let i=0;i<count;i++){
        const s=sections+i*40,v=b.readUInt32LE(s+12),raw=b.readUInt32LE(s+20),size=b.readUInt32LE(s+16);
        if(r>=v&&r-v<size)return raw+r-v;
      }
      throw Error('Unmapped PE data');
    };
    const str=n=>{const end=b.indexOf(0,n);if(end<n||end-n>1024)throw Error('Invalid PE string');return b.toString('utf8',n,end);};
    const dir=b.readUInt32LE(o+(wide?112:96));if(!dir)return null;
    const e=offset(dir),n=b.readUInt32LE(e+24);if(n>65536)return null;
    const funcs=offset(b.readUInt32LE(e+28)),names=offset(b.readUInt32LE(e+32)),ords=offset(b.readUInt32LE(e+36));
    const result={};
    for(let i=0;i<n;i++){
      const label=str(offset(b.readUInt32LE(names+i*4)));
      if(label!=='D3D12SDKVersion'&&label!=='D3D12SDKPath')continue;
      const at=offset(b.readUInt32LE(funcs+b.readUInt16LE(ords+i*2)*4));
      if(label==='D3D12SDKVersion')result.version=b.readUInt32LE(at);
      else result.sdkPath=str(offset((wide?Number(b.readBigUInt64LE(at)):b.readUInt32LE(at))-base));
    }
    return result.version?result:null;
  }catch{return null;}
}
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
async function install({payloadDir,gameDir,exePath,manifest}) {
  const requested=inspect(exePath);
  if(!requested||!requested.sdkPath)return {installed:false};
  // Exported paths are data, not instructions. Only deploy within the executable directory.
  const relative=requested.sdkPath.replace(/\\/g,'/');
  if(path.isAbsolute(relative)||relative.includes(':')||relative.split('/').includes('..'))return {installed:false};
  const dest=path.resolve(path.dirname(exePath),relative,'D3D12Core.dll');
  const src=path.join(payloadDir,'agility',String(requested.version),'D3D12Core.dll');
  if(fs.existsSync(dest)||!fs.existsSync(src))return {installed:false};
  if(inspect(src)?.version!==requested.version)throw Error('Bundled Agility SDK version mismatch');
  if(manifest)await require('./apply').copyTracked(manifest,gameDir,src,dest,{kind:'agility-runtime'});
  else {fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(src,dest);}
  if(hash(src)!==hash(dest))throw Error('Agility SDK copy verification failed');
  return {installed:true,version:requested.version,file:dest};
}
module.exports={inspect,install};
