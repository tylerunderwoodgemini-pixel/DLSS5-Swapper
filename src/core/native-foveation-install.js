'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const vr=require('./vr-foveation');
const ADDON='native-vr-foveation-nvngx.dll.addon64';
const CONFIG='dlss5-native-foveation.ini';
function verifyPayload(payloadDir){
  const file=path.join(payloadDir,'vr-foveated',ADDON);
  if(!fs.existsSync(file)||!fs.readFileSync(file).includes(Buffer.from('Native DLSS VR foveation')))
    throw Error('Native DLSS foveation payload is missing or invalid: '+file);
  return file;
}
async function install({payloadDir,gameDir,exePath,manifest,preset='off'},send=()=>{}){
  const source=verifyPayload(payloadDir),dir=path.dirname(exePath),core=require('./apply'),ini=require('./feeder-config');
  const dest=path.join(dir,ADDON),config=path.join(dir,CONFIG);
  const selected=vr.configFor(preset);
  let text=fs.existsSync(config)?fs.readFileSync(config,'utf8'):'';
  text=ini.setIni(text,'NativeFoveation','Preset',String(selected.enabled?selected.preset:0));
  if(!ini.getIni(text,'NativeFoveation','StereoLayout'))text=ini.setIni(text,'NativeFoveation','StereoLayout','0');
  if(manifest){await core.copyTracked(manifest,gameDir,source,dest,{kind:'native-vr-foveation'});await core.writeTracked(manifest,gameDir,config,text,{kind:'config'});}
  else {fs.copyFileSync(source,dest);fs.writeFileSync(config,text);}
  const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  if(hash(source)!==hash(dest))throw Error('Native DLSS foveation copy failed verification.');
  // An earlier disabled-addons profile must not silently suppress the bridge.
  for(const name of ['ReShade.ini','ReShadeVR.ini','ReShadeDesktopUI.ini']){
    const file=path.join(dir,name);if(!fs.existsSync(file))continue;
    const original=fs.readFileSync(file,'utf8');
    const updated=original.replace(/^DisabledAddons=(.*)$/mi,(_line,list)=>'DisabledAddons='+list.split(',').filter(x=>x.trim().toLowerCase()!==ADDON).join(','));
    if(updated===original)continue;
    if(manifest)await core.writeTracked(manifest,gameDir,file,updated,{kind:'config'});else fs.writeFileSync(file,updated);
  }
  if(manifest){manifest.nativeFoveation={version:1,preset:vr.normalizePreset(preset),addon:path.relative(gameDir,dest),config:path.relative(gameDir,config)};await core.saveActiveManifest(gameDir,manifest);}
  send({code:'vrFoveationReady',params:{preset:vr.normalizePreset(preset),version:'native-1'}});
  return {addon:dest,config};
}
module.exports={ADDON,CONFIG,verifyPayload,install};
