'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('fs'),os=require('os'),path=require('path');
const mirror=require('../src/core/native-vulkan-mirror'),core=require('../src/core/apply'),cfg=require('../src/core/feeder-config');
const payload=path.resolve(__dirname,'../payload');
test('native Vulkan mirror is NMS/DFC/x64-specific and refuses corrupt payload',()=>{
 const base={api:'vulkan',bitness:64,exePath:'NMS.exe',neuralProvider:'deep-fried-chicken',source:{deepFriedChicken:{addon64:path.join(payload,'deep-fried-chicken/deep-fried-chicken.addon64')}}};
 assert(mirror.verify(base));
 for(const change of [{exePath:'Other.exe'},{api:'dxgi'},{bitness:32},{neuralProvider:'renodx'},{nativeDlssMirror:false}])assert.equal(mirror.eligible({...base,...change}),false);
 assert.throws(()=>mirror.verify({...base,source:{deepFriedChicken:{addon64:path.join(os.tmpdir(),'missing-mirror/dfc.addon64')}}}),/missing or changed/);
});
test('NMS actual Vulkan installer selects native producer on fresh/repeated install and restores originals',async t=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'nms-native-auto-'));t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const gameDir=path.join(root,'game');fs.mkdirSync(gameDir);const exePath=path.join(gameDir,'NMS.exe');fs.writeFileSync(exePath,'fixture');
 const original={ 'dlss5-feed.cfg':'enabled=1\nwork_resolution=84\nenabled=1\n', 'dlss5-bridge.cfg':'unwrap=1\nreset_every=0\nunwrap=1\n', 'dlss5-bridge.addon64':'old bridge', 'nvngx_dlss.dll':'original game dll', 'ReShadeVR.ini':'[GENERAL]\nDisabledAddons=dlss5-bridge.addon64,dlss5-feed.addon64,Other.addon64\n', 'ReShade.ini':'[ADDON]\nDisabledAddons=dlss5-bridge.addon64,Other.addon64\n'};
 for(const [name,text] of Object.entries(original))fs.writeFileSync(path.join(gameDir,name),text);
 const source=require('../src/core/scan').scanSource(payload);
 const registryRunner=async (_file,args)=>({code:args[0]==='query'?1:0,stdout:'',stderr:''});
 const options={gameDir,exePath,source,api:'vulkan',bitness:64,route:'feeder',neuralProvider:'deep-fried-chicken',vulkanLayerTarget:path.join(root,'layer'),registryRunner,vrFoveation:'off'};
 for(let i=0;i<2;i++){
  const manifest=await core.applySwap(options);
  assert.equal(manifest.nativeDlssMirror.producer,'game-native-vulkan-dlss');
  const feed=fs.readFileSync(path.join(gameDir,'dlss5-feed.cfg'),'utf8');assert.equal(cfg.getIni(feed,'','enabled'),'0');assert.equal(feed.match(/^enabled=/mg).length,1);
  const bridge=fs.readFileSync(path.join(gameDir,'dlss5-bridge.cfg'),'utf8');assert.equal(cfg.getIni(bridge,'','unwrap'),'0');assert.equal(cfg.getIni(bridge,'','reset_every'),'0');
  const ini=fs.readFileSync(path.join(gameDir,'ReShade.ini'),'utf8');assert(!cfg.getIni(ini,'ADDON','DisabledAddons').includes('dlss5-bridge'));assert(ini.includes('Other.addon64'));
  const vrIni=fs.readFileSync(path.join(gameDir,'ReShadeVR.ini'),'utf8');assert.equal(cfg.getIni(vrIni,'GENERAL','DisabledAddons'),'Other.addon64');
  assert.equal(require('crypto').createHash('sha256').update(fs.readFileSync(path.join(gameDir,'dlss5-bridge.addon64'))).digest('hex'),mirror.HASH);
 }
 await core.restore(gameDir,()=>{},registryRunner);
 for(const [name,text] of Object.entries(original))assert.equal(fs.readFileSync(path.join(gameDir,name),'utf8'),text,name+' restored exactly');
});
