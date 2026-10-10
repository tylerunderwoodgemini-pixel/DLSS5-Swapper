'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { spawnSync, execFileSync } = require('child_process');

const VERSION = 26;
const vrFoveation = require('./vr-foveation');
const WRAP = Symbol.for('dlss5-swapper-universal-vr-backend-wrap');

function trace(gameDir, message) {
  try {
    const line = `${new Date().toISOString()} [UniversalVR v${VERSION}] ${message}\n`;
    const target = gameDir && fs.existsSync(gameDir) ? path.join(gameDir, 'dlss5-swapper-universal-vr.log') : path.join(os.tmpdir(), 'dlss5-swapper-universal-vr.log');
    fs.appendFileSync(target, line, 'utf8');
  } catch {}
}

function walkNames(root, names, maxFiles = 6000) {
  if (!root || !fs.existsSync(root)) return [];
  const wanted = new Set(names.map(x => x.toLowerCase()));
  const hits = [];
  const stack = [root];
  let seen = 0;
  while (stack.length && seen < maxFiles) {
    const dir = stack.pop();
    let rows = [];
    try { rows = fs.readdirSync(dir, { withFileTypes: true }); } catch { continue; }
    for (const row of rows) {
      if (++seen > maxFiles) break;
      const full = path.join(dir, row.name);
      if (row.isDirectory()) {
        if (!/^(?:node_modules|reshade-shaders|backup|backups|_backup)$/i.test(row.name)) stack.push(full);
      } else if (wanted.has(row.name.toLowerCase())) hits.push(full);
    }
  }
  return hits;
}

function binaryMentions(file, needles) {
  try {
    const ascii = fs.readFileSync(file).toString('latin1').toLowerCase();
    return needles.some(n => ascii.includes(n.toLowerCase()));
  } catch { return false; }
}

function detect(gameDir, exePath) {
  if (process.platform !== 'win32') return 'none';
  const xrNames = ['openxr_loader.dll','UnityOpenXR.dll','OpenXRHMD.dll','MicrosoftOpenXRGame.dll','OVRPlugin.dll'];
  const vrNames = ['openvr_api.dll','XRSDKOpenVR.dll','openvr_api64.dll'];
  const hasXR = walkNames(gameDir, xrNames).length > 0 || binaryMentions(exePath,
    ['openxr_loader.dll','xrCreateInstance','XR_KHR_D3D11_enable','XR_KHR_D3D12_enable']);
  const hasVR = walkNames(gameDir, vrNames).length > 0 || binaryMentions(exePath,
    ['openvr_api.dll','VR_InitInternal','IVRSystem_']);
  const profile=path.join(process.env.APPDATA || '', 'UnrealVRMod', path.basename(exePath || '', path.extname(exePath || '')));
  const hasMod=!!process.env.APPDATA && fs.existsSync(profile);
  if (hasXR || hasMod) return 'openxr'; // hybrids prefer OpenXR to avoid duplicate ReShade instances
  if (hasVR) return 'openvr';
  return 'none';
}

function ancestors(fileOrDir, max = 6) {
  const out = [];
  let p = fileOrDir;
  try { if (fs.existsSync(p) && fs.statSync(p).isFile()) p = path.dirname(p); } catch {}
  for (let i=0; p && i<max; i++) {
    out.push(p);
    const parent = path.dirname(p);
    if (parent === p) break;
    p = parent;
  }
  return out;
}

function resolvePayloadDir(source, reshadeSetup) {
  const starts = [];
  const add = x => { if (typeof x === 'string' && x) starts.push(x); };
  try { add(source && source.feeder && (source.feeder.addon64 || source.feeder.addon)); } catch {}
  try { add(source && source.deepFriedChicken && source.deepFriedChicken.addon64); } catch {}
  try { add(source && source.addon); } catch {}
  add(reshadeSetup);

  const candidates = [];
  const seen = new Set();
  for (const s of starts) for (const a of ancestors(s, 7)) {
    const k = path.resolve(a).toLowerCase();
    if (!seen.has(k)) { seen.add(k); candidates.push(a); }
  }
  for (const dir of candidates) {
    const feeders = walkNames(dir, ['dlss5-feed.addon64'], 12000);
    const helpers = walkNames(dir, ['dfc-universal-lifecycle-v20.addon64'], 12000);
    if (feeders.length && helpers.length) return dir;
  }
  return null;
}

function productName(file) {
  if (process.platform !== 'win32' || !fs.existsSync(file)) return '';
  const escaped = file.replace(/'/g, "''");
  try {
    return execFileSync('powershell.exe', ['-NoProfile','-Command', `(Get-Item -LiteralPath '${escaped}').VersionInfo.ProductName`],
      { encoding:'utf8', windowsHide:true, timeout:10000 }).trim();
  } catch { return ''; }
}

function isReShadeBinary(file) {
  if (/reshade/i.test(productName(file))) return true;
  return binaryMentions(file, ['ReShade version', "crosire's ReShade", 'ReShade Add-on']);
}

async function disableLocalReShadeProxy(gameDir, send = () => {}, {manifest, gameRoot=gameDir} = {}) {
  const disabled=[];
  for (const name of ['dxgi.dll','d3d11.dll','d3d12.dll','d3d9.dll','opengl32.dll']) {
    const file = path.join(gameDir, name);
    if (!fs.existsSync(file)) continue;
    if (!isReShadeBinary(file)) {
      send({ code:'vrUniversalPreservedProxy', params:{ file:name } });
      continue;
    }
    let dst = file + '.reshade-local-disabled-by-universal-vr';
    let i=1; while (fs.existsSync(dst)) dst = file + `.reshade-local-disabled-by-universal-vr.${i++}`;
    const journal=require('./file-journal');
    await journal.capture(gameRoot,file);
    await journal.capture(gameRoot,dst);
    fs.renameSync(file, dst);
    disabled.push({file,backup:dst});
    if (manifest) {
      const rel=path.relative(gameRoot,dst);
      if(!manifest.added.includes(rel)) manifest.added.push(rel);
      await require('./apply').saveActiveManifest(gameRoot,manifest);
    }
    trace(gameDir,`OpenXR proxy conflict removed file=${name} backup=${path.basename(dst)}`);
    send({ code:'vrUniversalDisabledDuplicateProxy', params:{ file:name, backup:path.basename(dst) } });
  }
  return disabled;
}

function verifyUniversalPayload(payloadDir, send = () => {}) {
  if (!payloadDir || !fs.existsSync(payloadDir)) throw new Error('Universal VR could not resolve the active Swapper payload from backend install inputs.');
  const feeders = walkNames(payloadDir, ['dlss5-feed.addon64'], 12000);
  if (!feeders.length) throw new Error(`The active Swapper payload contains no dlss5-feed.addon64 under ${payloadDir}.`);
  if (!feeders.some(f => binaryMentions(f, ['1.16.0-beta.4-vr1','vr-universal-foveated26'])) )
    throw new Error('The active Swapper payload contains no compatible Universal VR feeder build.');
  const helpers = walkNames(payloadDir, ['dfc-universal-lifecycle-v20.addon64'], 12000);
  if (!helpers.length) throw new Error('The active Swapper payload contains no dfc-universal-lifecycle-v20.addon64.');
  const xrSetups = walkNames(payloadDir, [V19_OPENXR_SETUP], 12000);
  send({ code:'vrUniversalPayloadVerified', params:{ payloadDir, feederCopies:feeders.length, helperCopies:helpers.length, v19OpenXRCopies:xrSetups.length } });
}

function findPayloadHelper(payloadDir) {
  const hits = walkNames(payloadDir, ['dfc-universal-lifecycle-v20.addon64'], 12000);
  return hits[0] || null;
}

const V19_OPENXR_SETUP = 'ReShade_Setup_6.8.0_OpenXR_EarlyLoad_V19_Addon.exe';
const V19_OPENXR_DLL = 'ReShade64.dll';
function findV19OpenXRSetup(payloadDir) {
  const hits = walkNames(payloadDir, [V19_OPENXR_SETUP], 12000);
  return hits[0] || null;
}
function findV19OpenXRDll(payloadDir) {
  const hits = walkNames(payloadDir, [V19_OPENXR_DLL], 12000);
  return hits.find(p => /[\\/]openxr[\\/]/i.test(p)) || hits[0] || null;
}

function enableAddonInReShadeIni(exeDir, stem) {
  const ini = path.join(exeDir, 'ReShade.ini');
  if (!fs.existsSync(ini)) return;
  try {
    let text = fs.readFileSync(ini, 'utf8');
    const rx = /^DisabledAddons=(.*)$/mi;
    const m = text.match(rx);
    if (!m) return;
    const kept = m[1].split(',').map(x => x.trim()).filter(Boolean).filter(x => !x.toLowerCase().includes(stem.toLowerCase()));
    text = text.replace(rx, `DisabledAddons=${kept.join(',')}`);
    fs.writeFileSync(ini, text, 'utf8');
  } catch {}
}

async function installLifecycleHelper(payloadDir, exeDir, send = () => {}, {manifest,gameDir=exeDir} = {}) {
  const helper = findPayloadHelper(payloadDir);
  if (!helper) throw new Error('Universal VR payload is missing dfc-universal-lifecycle-v20.addon64.');
  const st = fs.statSync(helper);
  if (!st.isFile() || st.size < 16384) throw new Error(`Lifecycle v20 payload looks invalid (${st.size} bytes): ${helper}`);
  let rows=[]; try { rows=fs.readdirSync(exeDir); } catch {}
  for (const row of rows) {
    if (!/^dfc-universal-lifecycle-v\d+\.addon64$/i.test(row) || row.toLowerCase()==='dfc-universal-lifecycle-v20.addon64') continue;
    const src=path.join(exeDir,row); let dst=src+'.disabled-by-universal-vr'; let n=1;
    while(fs.existsSync(dst)) dst=src+`.disabled-by-universal-vr.${n++}`;
    if(manifest){
      const core=require('./apply');await core.copyTracked(manifest,gameDir,src,dst,{kind:'retired-vr-lifecycle'});
      await core.trackBeforeWrite(manifest,gameDir,src,{kind:'retired-vr-lifecycle'});await core.saveActiveManifest(gameDir,manifest);fs.unlinkSync(src);
    }else fs.renameSync(src,dst);
  }
  const dst = path.join(exeDir,'dfc-universal-lifecycle-v20.addon64');
  if(manifest)await require('./apply').copyTracked(manifest,gameDir,helper,dst,{kind:'vr-lifecycle'});
  else fs.copyFileSync(helper, dst);
  const out = fs.statSync(dst);
  if (!out.isFile() || out.size !== st.size) throw new Error('Lifecycle v20 copy verification failed.');
  enableAddonInReShadeIni(exeDir, 'dfc-universal-lifecycle-v20');
  trace(exeDir, `lifecycle v20 installed source=${helper} bytes=${out.size}`);
  send({ code:'vrUniversalLifecycleInstalled', params:{ version:20, file:dst, bytes:out.size } });
}

function checkXrOverrides() {
  if (process.platform !== 'win32') return;
  const bad=['XR_ENABLE_API_LAYERS','XR_API_LAYER_PATH'].filter(k=>process.env[k]);
  if (bad.length) throw new Error(`OpenXR override environment variable(s) are set: ${bad.join(', ')}. Clear them before installing the universal OpenXR path.`);
}

function apiArg(api) {
  if (['d3d9','d3d10','d3d11','d3d12','dxgi','opengl','vulkan'].includes(api)) return api;
  return api==='d3d8' ? 'd3d9' : 'dxgi';
}

function queryOpenXRRegistration(bitness = 64, expectedDll) {
  const root = process.env.ProgramData || 'C:\\ProgramData';
  const arch = Number(bitness) === 32 ? '32' : '64';
  const manifest = path.join(root, 'ReShade', `ReShade${arch}_XR.json`);
  const dll = expectedDll || path.join(process.env.LOCALAPPDATA || os.tmpdir(), 'DLSS5-Swapper','openxr-router-v2','DLSS5OpenXRRouter.dll');
  const key = arch === '32'
    ? 'HKLM\\SOFTWARE\\WOW6432Node\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit'
    : 'HKLM\\SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit';
  let registryEnabled = false;
  let registryText = '';
  try {
    registryText = execFileSync('reg.exe', ['query', key, '/v', manifest], {
      encoding:'utf8', windowsHide:true, timeout:10000
    });
    registryEnabled = /REG_DWORD\s+0x0(?:\s|$)/i.test(registryText);
  } catch {}
  const manifestOk = fs.existsSync(manifest);
  let manifestLibrary = null;
  let resolvedLibrary = null;
  let manifestLibraryOk = false;
  if (manifestOk) {
    try {
      const json = JSON.parse(fs.readFileSync(manifest, 'utf8').replace(/^\uFEFF/,''));
      manifestLibrary = json && json.api_layer && json.api_layer.library_path;
      if (typeof manifestLibrary === 'string' && manifestLibrary) {
        resolvedLibrary = path.resolve(path.dirname(manifest), manifestLibrary);
        manifestLibraryOk = path.normalize(resolvedLibrary).toLowerCase() === path.normalize(dll).toLowerCase() && fs.existsSync(resolvedLibrary);
      }
    } catch {}
  }
  const dllOk = fs.existsSync(dll);
  return { ok:manifestOk && dllOk && manifestLibraryOk && registryEnabled, manifest, dll, key, manifestOk, dllOk, manifestLibraryOk, manifestLibrary, resolvedLibrary, registryEnabled, registryText };
}

function openXRSetupArgs(exePath, api) {
  // V19's reuse patch requires DXGI + OpenXR together.
  if (!['dxgi','d3d10','d3d11','d3d12'].includes(apiArg(api)))
    throw new Error('The V19 desktop/OpenXR module requires a Direct3D 10/11/12 target.');
  return [exePath,'--api','dxgi','--api','openxr','--state','update','--headless'];
}
function fileHash(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}
function runOpenXRSetup(payloadDir, exePath, api, bitness = 64, send = () => {}) {
  checkXrOverrides();
  if (Number(bitness)!==64) throw new Error('The bundled V19 OpenXR verification requires a 64-bit target.');
  const setup=findV19OpenXRSetup(payloadDir), runtimeDll=findV19OpenXRDll(payloadDir);
  if (!setup || !runtimeDll) throw new Error('Missing modified V19 installer or verification DLL.');
  const args=openXRSetupArgs(exePath,api), dll=path.join(path.dirname(exePath),'dxgi.dll');
  const shared=path.join(process.env.LOCALAPPDATA || os.tmpdir(),'DLSS5-Swapper','openxr-router-v2');
  const routerSource=path.join(payloadDir,'openxr','DLSS5OpenXRRouter.dll');
  if(!fs.existsSync(routerSource))throw new Error('Missing universal OpenXR router payload.');
  fs.mkdirSync(shared,{recursive:true});
  const router=path.join(shared,'DLSS5OpenXRRouter.dll');
  for(const [src,dst] of [[routerSource,router],[runtimeDll,path.join(shared,'ReShade64.dll')]])
    if(!fs.existsSync(dst)||fileHash(src)!==fileHash(dst))fs.copyFileSync(src,dst);
  const xrManifest=path.join(process.env.ProgramData || 'C:\\ProgramData','ReShade','ReShade64_XR.json');
  if (fs.existsSync(dll) && !isReShadeBinary(dll)) throw new Error('Refusing to overwrite unrelated proxy: '+dll);
  // The modified installer writes its local proxy, XR manifest and app list.
  // Elevate it directly and wait for the actual installer exit code.
  const q=s=>"'"+String(s).replace(/'/g,"''")+"'";
  const launcher=path.join(os.tmpdir(),'dlss5-v19-xr-setup-'+process.pid+'.ps1');
  const argumentLine=args.map(s=>'"'+s+'"').join(' ');
  const elevated=path.join(os.tmpdir(),'dlss5-v19-xr-register-'+process.pid+'.ps1');
  fs.writeFileSync(elevated,"$ErrorActionPreference='Stop'\r\ntry { $p=Start-Process -FilePath "+q(setup)+" -ArgumentList "+q(argumentLine)+" -WindowStyle Hidden -Wait -PassThru; if ($p.ExitCode -ne 0) { exit $p.ExitCode }; $m=Get-Content -LiteralPath "+q(xrManifest)+" -Raw | ConvertFrom-Json; $m.api_layer.library_path="+q(router)+"; [System.IO.File]::WriteAllText("+q(xrManifest)+",($m | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false)); exit 0 } catch { Write-Error $_; exit 1 }\r\n",'utf8');
  fs.writeFileSync(launcher,"$ErrorActionPreference='Stop'\r\ntry { $p=Start-Process -FilePath 'powershell.exe' -ArgumentList "+q('-NoProfile -ExecutionPolicy Bypass -File "'+elevated+'"')+" -Verb RunAs -WindowStyle Hidden -Wait -PassThru; exit $p.ExitCode } catch { Write-Error $_; exit 1 }\r\n",'utf8');
  send({code:'vrUniversalOpenXRSetup',params:{provider:'V19 EarlyLoad Installer',setup:path.basename(setup),state:'update'}});
  trace(path.dirname(exePath),'modified V19 setup='+setup+' args='+JSON.stringify(args));
  let result;
  try {result=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',launcher],{windowsHide:true,encoding:'utf8'});}
  finally {try{fs.unlinkSync(launcher);fs.unlinkSync(elevated);}catch{}}
  if(result.error)throw result.error;
  if(result.status!==0)throw new Error('Modified V19 installer failed: '+result.status+' '+(result.stderr||result.stdout||''));
  const reg=queryOpenXRRegistration(bitness,router);
  if(!reg.ok)throw new Error('V19 installation verification failed: '+JSON.stringify(reg));
  const hash=fileHash(dll);
  if(hash!==fileHash(runtimeDll))throw new Error('Installed ReShade differs from modified V19 payload.');
  trace(path.dirname(exePath),'V19 verified manifest='+reg.manifest+' library='+reg.resolvedLibrary+' sha256='+hash);
  send({code:'vrUniversalOpenXRReady',params:{provider:'V19 EarlyLoad Installer',manifest:reg.manifest,dll,sha256:hash}});
  return reg;
}

async function preflight({ mode, api, bitness, reshadeSetup, neuralProvider, payloadDir }, send = () => {}) {
  verifyUniversalPayload(payloadDir, send);
  if (mode==='openxr' || (Number(bitness)===64 && ['dxgi','d3d10','d3d11','d3d12'].includes(apiArg(api)))) {
    openXRSetupArgs('',api);
    require('./openxr-pose-install').verifyPayload(payloadDir);
    if (Number(bitness || 64)!==64) throw new Error('The bundled V19 OpenXR verification requires a 64-bit target.');
    if (!findV19OpenXRSetup(payloadDir) || !findV19OpenXRDll(payloadDir)) throw new Error(`OpenXR VR game detected, but ${V19_OPENXR_DLL} is missing from the active Swapper OpenXR payload.`);
    checkXrOverrides();
  }
  if (neuralProvider==='deep-fried-chicken' && !findPayloadHelper(payloadDir))
    throw new Error('Deep Fried Chicken selected for VR, but lifecycle v20 is missing from the active payload.');
}

async function finalize({ mode, gameDir, exePath, api, bitness, reshadeSetup, neuralProvider, payloadDir, manifest, route, vrFoveation: foveation='off', installReShade=true, enableVR=true }, send = () => {}) {
  const direct64=Number(bitness || 64)===64 && ['dxgi','d3d10','d3d11','d3d12'].includes(apiArg(api));
  if(mode==='none'&&!direct64)return;
  const exeDir=path.dirname(exePath);
  if (neuralProvider==='deep-fried-chicken')await installLifecycleHelper(payloadDir,exeDir,send,{manifest,gameDir});
  if (direct64 && installReShade!==false && enableVR!==false) {
    const core=require('./apply');
    if(manifest) {
      for(const name of ['dxgi.dll','ReShade.ini','ReShadePreset.ini'])
        await core.trackBeforeWrite(manifest,gameDir,path.join(exeDir,name),{kind:name==='dxgi.dll'?'reshade':'config'});
      await core.saveActiveManifest(gameDir,manifest);
    }
    runOpenXRSetup(payloadDir,exePath,api,bitness || 64,send);

    if(manifest) {
      manifest.reshade={...manifest.reshade,file:path.relative(gameDir,path.join(exeDir,'dxgi.dll')),openxrProvider:'V19 EarlyLoad Installer'};
      await core.saveActiveManifest(gameDir,manifest);
    }
    send({ code:'vrUniversalReady', params:{ mode:'OpenXR', version:VERSION } });
  }
  if(direct64&&enableVR!==false)await require('./openxr-pose-install').install({payloadDir,gameDir,exePath,manifest},send);
  if(route==='native'&&direct64&&enableVR!==false)await require('./native-foveation-install').install({payloadDir,gameDir,exePath,manifest,preset:foveation},send);
  if (mode==='openvr') {
    send({ code:'vrUniversalReady', params:{ mode:'OpenVR', version:VERSION } });
  }
}

function wrapBackendManager(backends) {
  if (!backends || typeof backends.install !== 'function') throw new Error('Universal VR: backend-manager.install is unavailable.');
  if (backends[WRAP]) return backends;
  const original=backends.install.bind(backends);
  backends.install=async function universalVrInstall(options, send = () => {}) {
    const opts=options || {};
    const route=opts.route;
    const neuralProvider=opts.neuralProvider || 'renodx';
    const vrEligible=process.platform==='win32' && (route==='feeder' || route==='native');
    const requestedMode=String(opts.vrMode||'auto').toLowerCase();
    const detected=vrEligible ? detect(opts.gameDir,opts.exePath) : 'none';
    const mode=!vrEligible || requestedMode==='off' ? 'none'
      : requestedMode==='openxr' ? 'openxr'
      : requestedMode==='openvr' ? 'openvr'
      : detected;
    const foveation=vrFoveation.normalizePreset(opts.vrFoveation);
    trace(opts.gameDir, `backend install entered route=${route} provider=${neuralProvider} requestedVR=${requestedMode} detected=${detected} resolved=${mode} foveation=${foveation}`);
    const direct64=vrEligible && Number(opts.bitness)===64 && ['dxgi','d3d10','d3d11','d3d12'].includes(apiArg(opts.api));
    if(mode==='none'&&!direct64)return original({...opts,vrFoveation:'off'},send);

    const payloadDir=resolvePayloadDir(opts.source,opts.reshadeSetup);
    trace(opts.gameDir, `resolved payload=${payloadDir || '(none)'} reshadeSetup=${opts.reshadeSetup || '(none)'}`);
    if(route==='native'&&direct64&&requestedMode!=='off')require('./native-foveation-install').verifyPayload(payloadDir);
    await preflight({mode,api:opts.api,bitness:opts.bitness,reshadeSetup:opts.reshadeSetup,neuralProvider,payloadDir},send);
    if(mode!=='none')send({ code:'vrUniversalDetected', params:{ mode:mode==='openxr'?'OpenXR':'OpenVR', version:VERSION } });

    // Use the patched build in both stages; finalize also upgrades stock proxies.
    let installOptions = { ...opts, vrFoveation:foveation,
      ...(direct64&&requestedMode!=='off'?{reshadeSetup:findV19OpenXRSetup(payloadDir),api:'dxgi'}:{}) };
    // The VR feeder also carries pose/history support when foveation is disabled.
    if (route==='feeder' && Number(opts.bitness)===64) {
      const built=vrFoveation.ensureBuilt({payloadDir,stockFeeder:opts.source&&opts.source.feeder},send);
      installOptions={
        ...installOptions,
        source:{
          ...opts.source,
          feeder:{...opts.source.feeder,addon64:built.addon64,feedShader:built.feedShader,shaderRoot:built.shaderRoot,vrPoseBridge:built.bridge,vrDepthBridge:built.depthBridge,version:built.version,ok64:true}
        }
      };
      send({code:'vrFoveationReady',params:{preset:foveation,version:vrFoveation.VERSION}});
    }
    trace(opts.gameDir, `calling original backend install installReShade=${installOptions.installReShade} topology=${mode==='openxr'?'v19-shared-local-openxr':'local-openvr'} foveation=${foveation}`);
    const manifest=await original(installOptions,send);
    await finalize({mode,route,vrFoveation:foveation,gameDir:opts.gameDir,exePath:opts.exePath,api:opts.api,bitness:opts.bitness,reshadeSetup:opts.reshadeSetup,neuralProvider,payloadDir,manifest,installReShade:opts.installReShade,enableVR:requestedMode!=='off'},send);
    trace(opts.gameDir, `finalize complete mode=${mode}`);
    return manifest;
  };
  Object.defineProperty(backends, WRAP, { value:true, enumerable:false });
  return backends;
}

module.exports={VERSION,detect,resolvePayloadDir,preflight,finalize,wrapBackendManager,disableLocalReShadeProxy,openXRSetupArgs,queryOpenXRRegistration};
