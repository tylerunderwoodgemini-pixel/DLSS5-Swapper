'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const {execFileSync}=require('child_process');
const LAYER='DLSS5OpenXRPose.dll',BRIDGE='vr-pose-bridge-v1.addon64';
const KEY='HKCU\\SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function verifyPayload(payloadDir) {
  const root=path.join(payloadDir,'vr-foveated');
  for(const name of [LAYER,BRIDGE,'vr-depth-bridge.addon64','vr-projection-bridge.addon64','vr-current-input.addon64','dlss5-vr-compat.addon64'])if(!fs.existsSync(path.join(root,name)))throw new Error(`OpenXR pose payload missing: ${name}`);
  if(!fs.readFileSync(path.join(root,BRIDGE)).includes(Buffer.from('Pose bridge v2 registered')))
    throw new Error('The bundled VR pose bridge does not include OpenXR v2 support.');
  return root;
}
function query(root) {
  const manifest=path.join(root,'DLSS5OpenXRPose.json'),dll=path.join(root,LAYER);
  let registry=false,library=false;
  try{registry=/REG_DWORD\s+0x0(?:\s|$)/i.test(execFileSync('reg.exe',['query',KEY,'/v',manifest],{encoding:'utf8',windowsHide:true,timeout:10000}));}catch{}
  try{const layer=JSON.parse(fs.readFileSync(manifest,'utf8')).api_layer;
    library=layer.name==='XR_APILAYER_DLSS5_pose' && path.resolve(root,layer.library_path).toLowerCase()===path.resolve(dll).toLowerCase() && fs.existsSync(dll);
  }catch{}
  return {ok:registry&&library,manifest,dll,registry,library};
}
async function install({payloadDir,gameDir,exePath,manifest},send=()=>{}) {
  const payload=verifyPayload(payloadDir);
  await require('./agility-runtime').install({payloadDir,gameDir,exePath,manifest});
  const root=path.join(process.env.LOCALAPPDATA,'DLSS5-Swapper','openxr-pose-v2');
  fs.mkdirSync(root,{recursive:true});
  const dll=path.join(root,LAYER),src=path.join(payload,LAYER);
  // Skip identical files so a running game need not release this shared layer.
  if(!fs.existsSync(dll)||hash(src)!==hash(dll))fs.copyFileSync(src,dll);
  const file=path.join(root,'DLSS5OpenXRPose.json');
  fs.writeFileSync(file,JSON.stringify({file_format_version:'1.0.0',api_layer:{
    name:'XR_APILAYER_DLSS5_pose',library_path:'.\\'+LAYER,api_version:'1.0',implementation_version:'2',
    description:'DLSS5 frame-synchronized OpenXR pose capture',disable_environment:'DISABLE_DLSS5_OPENXR_POSE'
  }},null,2));
  execFileSync('reg.exe',['add',KEY,'/v',file,'/t','REG_DWORD','/d','0','/f'],{windowsHide:true,timeout:10000});
  const registration=query(root);
  if(!registration.ok||hash(dll)!==hash(src))throw new Error('OpenXR pose layer registration or DLL verification failed.');
  const core=require('./apply'),bridge=path.join(path.dirname(exePath),BRIDGE);
  // Retire the previous per-game adapter to prevent duplicate detours/UI.
  const legacy=path.join(path.dirname(exePath),'palia-xr-queue.addon64');
  if(fs.existsSync(legacy)) {
    const disabled=legacy+'.disabled-by-universal-vr';
    if(manifest){
      await core.copyTracked(manifest,gameDir,legacy,disabled,{kind:'retired-vr-adapter'});
      await core.trackBeforeWrite(manifest,gameDir,legacy,{kind:'retired-vr-adapter'});
      await core.saveActiveManifest(gameDir,manifest);
    }else fs.copyFileSync(legacy,disabled);
    fs.unlinkSync(legacy);
  }
  if(manifest)await core.copyTracked(manifest,gameDir,path.join(payload,BRIDGE),bridge,{kind:'vr-pose-bridge'});
  else fs.copyFileSync(path.join(payload,BRIDGE),bridge);
  if(hash(bridge)!==hash(path.join(payload,BRIDGE)))throw new Error('OpenXR pose bridge copy failed verification.');
  const depthSource=path.join(payload,'vr-depth-bridge.addon64');
  if(fs.existsSync(depthSource)){
    const dest=path.join(path.dirname(exePath),'vr-depth-bridge.addon64');
    if(manifest)await core.copyTracked(manifest,gameDir,depthSource,dest,{kind:'vr-depth-bridge'});
    else fs.copyFileSync(depthSource,dest);
    if(hash(dest)!==hash(depthSource))throw new Error('VR stereo depth bridge copy failed verification.');
  }
  const projectionSource=path.join(payload,'vr-projection-bridge.addon64');
  if(fs.existsSync(projectionSource)){
    const dest=path.join(path.dirname(exePath),'vr-projection-bridge.addon64');
    if(manifest)await core.copyTracked(manifest,gameDir,projectionSource,dest,{kind:'vr-projection-bridge'});
    else fs.copyFileSync(projectionSource,dest);
    if(hash(dest)!==hash(projectionSource))throw new Error('VR projection bridge copy failed verification.');
  }
  const currentInputSource=path.join(payload,'vr-current-input.addon64');
  if(fs.existsSync(currentInputSource)){
    const dest=path.join(path.dirname(exePath),'vr-current-input.addon64');
    if(manifest)await core.copyTracked(manifest,gameDir,currentInputSource,dest,{kind:'vr-current-input'});
    else fs.copyFileSync(currentInputSource,dest);
    if(hash(dest)!==hash(currentInputSource))throw new Error('VR current input bridge copy failed verification.');
  }
  const compatSource=path.join(payload,'dlss5-vr-compat.addon64');
  if(fs.existsSync(compatSource)){
    const dest=path.join(path.dirname(exePath),'dlss5-vr-compat.addon64');
    if(manifest)await core.copyTracked(manifest,gameDir,compatSource,dest,{kind:'dlss5-vr-compat'});
    else fs.copyFileSync(compatSource,dest);
    if(hash(dest)!==hash(compatSource))throw new Error('VR compatibility adapter copy failed verification.');
  }
  // Keep separate headset and mirror settings. Never disable the ordinary flat
  // runtime globally just because a VR mod might be injected later.
  const exeDir=path.dirname(exePath), ini=require('./feeder-config');
  const base=fs.existsSync(path.join(exeDir,'ReShade.ini'))?fs.readFileSync(path.join(exeDir,'ReShade.ini'),'utf8'):'';
  for(const name of ['ReShadeVR.ini','ReShadeDesktopUI.ini']) {
    const dest=path.join(exeDir,name);
    let text=fs.existsSync(dest)?fs.readFileSync(dest,'utf8'):base;
    text=text.replace(/^Disable=1[ \t]*\r?$/mg,'');
    text=text.replace(/^DisabledAddons=(.*)$/mi,(_line,value)=>'DisabledAddons='+value.split(',').filter(x=>!/(?:vr-pose-bridge|vr-depth-bridge|vr-projection-bridge|vr-current-input|dlss5-vr-compat)/i.test(x)).join(','));
    if(name==='ReShadeDesktopUI.ini')text=ini.setIni(text,'INPUT','KeyOverlay','0,0,0,0');
    if(manifest)await core.writeTracked(manifest,gameDir,dest,text,{kind:'config'});
    else fs.writeFileSync(dest,text);
  }
  if(manifest){manifest.openxrPose={version:2,manifest:file,dll,bridge:path.relative(gameDir,bridge)};await core.saveActiveManifest(gameDir,manifest);}
  send({code:'vrUniversalPoseReady',params:{version:2,manifest:file,bridge}});
  return registration;
}
module.exports={install,query,verifyPayload};
