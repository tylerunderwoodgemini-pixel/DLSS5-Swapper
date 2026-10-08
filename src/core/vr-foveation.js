'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const { spawnSync } = require('child_process');
const { createHash } = require('crypto');

const VERSION = 26;
const BUILD_MARKER = '1.16.0-beta.4-vr-universal-foveated26.28';
const PRESETS = {
  off:      { enabled:0, preset:2, width:60, height:50 },
  small:    { enabled:1, preset:1, width:50, height:45 },
  balanced: { enabled:1, preset:2, width:60, height:50 },
  wide:     { enabled:1, preset:3, width:70, height:50 },
  large:    { enabled:1, preset:4, width:75, height:60 }
};

function normalizePreset(value) {
  const key=String(value||'off').toLowerCase();
  return Object.prototype.hasOwnProperty.call(PRESETS,key) ? key : 'off';
}
function configFor(value) { return PRESETS[normalizePreset(value)]; }

function findNgxZip(payloadDir) {
  const roots=[
    path.dirname(process.execPath),
    process.resourcesPath ? path.dirname(process.resourcesPath) : null,
    process.resourcesPath || null,
    payloadDir ? path.dirname(payloadDir) : null,
    payloadDir ? path.dirname(path.dirname(payloadDir)) : null
  ].filter(Boolean);
  for (const root of roots) {
    const file=path.join(root,'DLSS-main.zip');
    if (fs.existsSync(file)) return file;
  }
  return null;
}

function cacheDir() {
  const base=process.env.LOCALAPPDATA || path.join(os.homedir(),'AppData','Local');
  return path.join(base,'DLSS5-Swapper','vr-foveated-v26');
}
function copyTree(src,dst) {
  fs.mkdirSync(dst,{recursive:true});
  for(const row of fs.readdirSync(src,{withFileTypes:true})) {
    const a=path.join(src,row.name), b=path.join(dst,row.name);
    if(row.isDirectory()) copyTree(a,b); else fs.copyFileSync(a,b);
  }
}

function buildFingerprint(root) {
  const hash=createHash('sha256');
  for (const name of ['Build-And-Install-VRUniversalFoveated-v26.ps1','DLSS5_Feed.fx','vr-pose-bridge-v1.addon64','vr-depth-bridge.addon64','vr-projection-bridge.addon64','vr-current-input.addon64','DLSS5OpenXRPose.dll','dlss5-vr-compat.addon64','native/Patch-D3D12-VRFoveation.ps1','native/vr-foveated-d3d12.h','native/vr-depth-layout.h','native/vr-depth-bridge.cpp','native/vr-projection-bridge.cpp']) {
    hash.update(name); hash.update(fs.readFileSync(path.join(root,name)));
  }
  return hash.digest('hex');
}
function cacheValid(cache,fingerprint) {
  try {
    const manifest=JSON.parse(fs.readFileSync(path.join(cache,'build-manifest.json'),'utf8'));
    if(manifest.fingerprint!==fingerprint || manifest.marker!==BUILD_MARKER) return false;
    for(const name of ['dlss5-feed.addon64','DLSS5_Feed.fx','vr-pose-bridge-v1.addon64','vr-depth-bridge.addon64','vr-projection-bridge.addon64','vr-current-input.addon64','VR_FOVEATED_V26.txt']) {
      const data=fs.readFileSync(path.join(cache,name));
      if(manifest.files[name]!==createHash('sha256').update(data).digest('hex')) return false;
      if(name==='dlss5-feed.addon64' && !data.includes(Buffer.from(BUILD_MARKER))) return false;
    }
    return true;
  } catch { return false; }
}

function ensureBuilt({payloadDir,stockFeeder}, send=()=>{}) {
  const root=path.join(payloadDir||'','vr-foveated');
  const builder=path.join(root,'Build-And-Install-VRUniversalFoveated-v26.ps1');
  if(!fs.existsSync(builder)) throw new Error(`VR foveation builder missing: ${builder}`);
  const cache=cacheDir();
  const fingerprint=buildFingerprint(root);
  const addon=path.join(cache,'dlss5-feed.addon64');
  const stamp=path.join(cache,'VR_FOVEATED_V26.txt');
  const bridge=path.join(cache,'vr-pose-bridge-v1.addon64');
  const shader=path.join(cache,'DLSS5_Feed.fx');
  const combinedShaders=path.join(cache,'reshade-shaders');
  const combinedFeedShader=path.join(combinedShaders,'Shaders','DLSS5_Feed.fx');

  if(!cacheValid(cache,fingerprint)) {
    const packaged=path.join(root,'prebuilt');
    if(cacheValid(packaged,fingerprint)) {
      fs.mkdirSync(cache,{recursive:true});
      copyTree(packaged,cache);
      send({code:'vrFoveationBuild',params:{state:'ready',version:VERSION}});
    } else {
    const ngx=findNgxZip(payloadDir);
    if(!ngx) throw new Error('VR foveation needs DLSS-main.zip beside the Swapper package, but it was not found.');
    fs.mkdirSync(cache,{recursive:true});
    send({code:'vrFoveationBuild',params:{state:'building',version:VERSION}});
    const r=spawnSync('powershell.exe',[
      '-NoProfile','-ExecutionPolicy','Bypass','-File',builder,
      '-BuildOnly','-LocalNgxZip',ngx,'-OutputDir',cache
    ],{encoding:'utf8',windowsHide:true,maxBuffer:16*1024*1024});
    if(r.status!==0 || !fs.existsSync(addon) || !fs.readFileSync(addon).includes(Buffer.from(BUILD_MARKER))) {
      const tail=((r.stdout||'')+'\n'+(r.stderr||'')).trim().split(/\r?\n/).slice(-30).join('\n');
      throw new Error(`VR foveated feeder build failed.${tail?'\n'+tail:''}`);
    }
    const files={};
    for(const name of ['dlss5-feed.addon64','DLSS5_Feed.fx','vr-pose-bridge-v1.addon64','vr-depth-bridge.addon64','vr-projection-bridge.addon64','vr-current-input.addon64','VR_FOVEATED_V26.txt']) {
      files[name]=createHash('sha256').update(fs.readFileSync(path.join(cache,name))).digest('hex');
    }
    fs.writeFileSync(path.join(cache,'build-manifest.json'),JSON.stringify({marker:BUILD_MARKER,fingerprint,files},null,2));
    send({code:'vrFoveationBuild',params:{state:'ready',version:VERSION}});
    }
  }

  // Preserve the full stock shader payload (ReShade.fxh, provider shaders, textures),
  // replacing only DLSS5_Feed.fx with the v26 companion shader.
  if(stockFeeder && stockFeeder.shaderRoot && fs.existsSync(stockFeeder.shaderRoot)) {
    if(fs.existsSync(combinedShaders)) fs.rmSync(combinedShaders,{recursive:true,force:true});
    copyTree(stockFeeder.shaderRoot,combinedShaders);
    fs.mkdirSync(path.dirname(combinedFeedShader),{recursive:true});
    fs.copyFileSync(shader,combinedFeedShader);
  }

  return { addon64:addon, feedShader:fs.existsSync(combinedFeedShader)?combinedFeedShader:shader, shaderRoot:fs.existsSync(combinedFeedShader)?combinedShaders:null, bridge, depthBridge:path.join(cache,'vr-depth-bridge.addon64'), version:BUILD_MARKER };
}

module.exports={VERSION,PRESETS,normalizePreset,configFor,ensureBuilt,buildFingerprint,cacheValid};
