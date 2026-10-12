'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto'),cfg=require('./feeder-config');
const HASH='6086e194136847afeb85c2ac96bc179a2b88dab78cb153936cf95e5844d1dbf6';
function eligible(o={}) {return o.api==='vulkan' && Number(o.bitness)===64 && /^nms\.exe$/i.test(path.basename(o.exePath||'')) && o.neuralProvider==='deep-fried-chicken' && o.nativeDlssMirror!==false;}
function payload(o) {return path.join(path.dirname(o.source.deepFriedChicken.addon64),'dlss5-native-vulkan-bridge.addon64');}
function verify(o) {
 if(!eligible(o))return false;
 const file=payload(o);
 if(!fs.existsSync(file)||crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')!==HASH)throw new Error('The verified NMS native Vulkan DLSS bridge is missing or changed. Restore the release payload.');
 return file;
}
function bridgeConfig(text) {
 for(const [key,value] of Object.entries({vk_mirror:'1',synth:'0',source:'mirror',stage:'3',mode:'2',unwrap:'0'}))text=cfg.setIni(text,'',key,value);
 return text;
}
async function install(o,core) {
 const source=verify(o);if(!source){delete o.manifest.nativeDlssMirror;return;}
 const dir=path.dirname(o.exePath),manifest=o.manifest;
 await core.copyTracked(manifest,o.gameDir,source,path.join(dir,'dlss5-bridge.addon64'),{kind:'addon'});
 for(const [name,update] of [['dlss5-bridge.cfg',bridgeConfig],['dlss5-feed.cfg',text=>cfg.setIni(text,'','enabled','0')]]) {
  const file=path.join(dir,name);await core.writeTracked(manifest,o.gameDir,file,update(cfg.readText(file)),{kind:'config'});
 }
 // The Feeder DLL stays loaded for Vulkan interop, but does not evaluate synthetic frames.
 for(const name of ['ReShade.ini','ReShadeVR.ini','ReShadeDesktopUI.ini']) {
  const ini=path.join(dir,name);if(name!=='ReShade.ini'&&!fs.existsSync(ini))continue;
  let text=cfg.readText(ini);
  for(const section of ['','GENERAL','ADDON','ADDON64']) {
   const disabled=cfg.getIni(text,section,'DisabledAddons');
   if(disabled!==null)text=cfg.setIni(text,section,'DisabledAddons',disabled.split(',').filter(x=>!['dlss5-bridge','dlss5-feed'].some(stem=>x.toLowerCase().includes(stem))).join(','));
  }
  await core.writeTracked(manifest,o.gameDir,ini,text,{kind:'config'});
 }
 manifest.nativeDlssMirror={version:1,producer:'game-native-vulkan-dlss',bridge:'dlss5-bridge.addon64',sha256:HASH,unwrap:false,feederEvaluation:false,requiresGameDlss:true};
 await core.saveActiveManifest(o.gameDir,manifest);
}
module.exports={HASH,eligible,verify,bridgeConfig,install};
