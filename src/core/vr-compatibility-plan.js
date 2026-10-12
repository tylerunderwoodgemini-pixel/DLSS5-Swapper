'use strict';
const {configFor,normalizePreset}=require('./vr-foveation');
const CONFIG='dlss5-compatibility.json';
function plan(options={}) {
  const api=String(options.api||'unknown').toLowerCase();
  const bits=Number(options.bitness||64),route=options.route||'unknown';
  const provider=options.neuralProvider||'renodx';
  const requested=String(options.vrMode||'auto').toLowerCase();
  const runtime=requested==='off'?'none':options.mode||'none';
  const direct=['dxgi','d3d10','d3d11','d3d12'].includes(api);
  const d12=api==='d3d12'||api==='dxgi';
  let path='unavailable',reason='No validated foveation path for this graphics API and route.';
  if(requested==='off')reason='VR support was explicitly disabled.';
  else if(bits!==64)reason='VR foveation requires the bundled 64-bit bridges.';
  else if(route==='feeder' && ['d3d11','d3d12','dxgi'].includes(api)) {
    path='feeder';reason='Feeder selects submitted headset images at runtime; desktop images remain independent.';
  } else if(route==='feeder'&&api==='vulkan'&&provider==='deep-fried-chicken'&&runtime!=='none') {
    path='vulkan-feeder-d3d12-nr';reason='Vulkan transport with D3D12 neural cropping; headset layout must match the submitted image.';
  } else if(route==='native'&&d12) {
    path='native-d3d12-nr';reason='Native SR remains full frame; only qualified D3D12 NR calls are cropped.';
  } else if(route==='native'&&direct)reason='Native foveation requires D3D12 NR; D3D11 rendering retains the ordinary neural path.';
  const preset=normalizePreset(options.vrFoveation);
  const geometry=configFor(preset);
  return {schema:1,api,bitness:bits,route,provider,requestedRuntime:requested,detectedRuntime:runtime,
    lateVrStartup:bits===64&&direct&&requested!=='off',foveationPath:path,reason,
    preset,center:{widthPercent:geometry.width,heightPercent:geometry.height},
    enabled:!!geometry.enabled&&path!=='unavailable',
    effectiveProducer:options.manifest?.nativeDlssMirror?.producer || (route==='feeder'?'synthetic-feeder':route==='native'||route==='optiscaler'?'game-native-dlss':'unknown'),
    requiresGameDlss:!!options.manifest?.nativeDlssMirror,
    runtimeValidationRequired:true,runtimeStatusFile:path.includes('nr')?'dlss5-native-foveation-status.json':null,
    checks:['active headset submission','per-eye viewport and guides','resource states and queue identity','history reset and fenced retirement'],
    limits:['No universal game certification','Native Vulkan and native D3D11 NR cropping are not implemented','Packed layouts require explicit matching configuration']};
}
async function install(options) {
  if(!options.manifest)return;
  const path=require('path'),core=require('./apply');
  const report=plan(options);
  if(options.manifest.nativeDlssMirror) {report.nativeDlssMirror=options.manifest.nativeDlssMirror;if(report.foveationPath==='vulkan-feeder-d3d12-nr')report.foveationPath='vulkan-native-mirror-d3d12-nr';report.reason='NMS native Vulkan DLSS mirrored to D3D12; Feeder evaluation disabled. Enable DLSS or DLAA in the game.';}
  await core.writeTracked(options.manifest,options.gameDir,path.join(path.dirname(options.exePath),CONFIG),JSON.stringify(report,null,2)+'\n',{kind:'config'});
  options.manifest.vrCompatibility=report;
  await core.saveActiveManifest(options.gameDir,options.manifest);
  return report;
}
module.exports={CONFIG,plan,install};
