const assert=require('node:assert/strict'),fs=require('fs'),path=require('path'),os=require('os'),vm=require('vm');
const {createRequire}=require('module');
const file=path.resolve(__dirname,'../src/core/vr-auto.js'),nativeRequire=createRequire(file);
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'vr-v19-test-'));
const payload=path.join(temp,'payload'),xr=path.join(payload,'openxr'),programData=path.join(temp,'programData');
fs.mkdirSync(xr,{recursive:true});fs.mkdirSync(path.join(programData,'ReShade'),{recursive:true});
const patched=Buffer.from("crosire's ReShade patched V19 fixture"),setupName='ReShade_Setup_6.8.0_OpenXR_EarlyLoad_V19_Addon.exe';
fs.writeFileSync(path.join(xr,'DLSS5OpenXRRouter.dll'),'router');fs.writeFileSync(path.join(xr,'ReShade64.dll'),patched);fs.writeFileSync(path.join(xr,setupName),'installer');
const events=[];let fails=false,badHash=false,wrongManifest=false,activeExe;
const context={module:{exports:{}},Buffer,console,process:{...process,env:{...process.env,ProgramData:programData,LOCALAPPDATA:temp,XR_ENABLE_API_LAYERS:'',XR_API_LAYER_PATH:''}},
  require:name=>{
    if(name==='child_process')return {
      execFileSync:(exe,args)=>exe==='reg.exe'?'REG_DWORD    0x0':'',
      spawnSync:(exe,args)=>{
        const outer=fs.readFileSync(args[args.length-1],'utf8');
        assert(outer.includes('-Verb RunAs'),'elevates registration');
        const script=fs.readFileSync(path.join(os.tmpdir(),'dlss5-v19-xr-register-'+process.pid+'.ps1'),'utf8');
        assert(script.includes(setupName),'uses modified installer');
        assert(script.includes('"--api" "dxgi" "--api" "openxr"'),'activates both APIs');
        assert(script.includes('"--state" "update"'),'updates existing installations');
        assert(script.includes('-Wait -PassThru'),'waits for installer exit code');
        events.push('setup');if(fails)return {status:1,stderr:'failed fixture'};
        const local=path.join(path.dirname(activeExe),'dxgi.dll');fs.writeFileSync(local,badHash?Buffer.from("crosire's ReShade stock"):patched);
        fs.writeFileSync(path.join(programData,'ReShade/ReShade64_XR.json'),JSON.stringify({api_layer:{library_path:wrongManifest?path.join(xr,'ReShade64.dll'):path.join(temp,'DLSS5-Swapper/openxr-router-v2/DLSS5OpenXRRouter.dll')}}));
        return {status:0};
      }
    };
    if(name==='./apply')return {trackBeforeWrite:async(m,g,f)=>events.push('track '+path.basename(f)),saveActiveManifest:async()=>events.push('save')};
    if(name==='./vr-foveation')return {normalizePreset:v=>v||'off',VERSION:26,ensureBuilt:()=>({addon64:'fixed-26.28',bridge:'pose',depthBridge:'depth',feedShader:'shader',shaderRoot:'shaders',version:'26.28'})};
    if(name==='./native-foveation-install')return {verifyPayload:()=>events.push('native payload verified'),install:async opts=>{events.push('native foveation installed');assert.equal(opts.preset,'balanced');}};
    if(name==='./openxr-pose-install')return {verifyPayload:()=>{},install:async()=>events.push('pose installed')};
    return nativeRequire(name);
  }
};
vm.runInNewContext(fs.readFileSync(file,'utf8'),context,{filename:file});const api=context.module.exports;
function fixture(name){const dir=path.join(temp,name);fs.mkdirSync(dir);activeExe=path.join(dir,'game.exe');fs.writeFileSync(activeExe,'game');
fs.writeFileSync(path.join(dir,'dxgi.dll'),"crosire's ReShade stock");return {mode:'openxr',gameDir:dir,exePath:activeExe,api:'d3d11',bitness:64,payloadDir:payload,manifest:{added:['dxgi.dll']}};}
(async()=>{try{
  const opts=fixture('existing');await api.finalize(opts);
  assert(fs.readFileSync(path.join(opts.gameDir,'dxgi.dll')).equals(patched));
  assert(events.indexOf('track dxgi.dll')<events.indexOf('setup'),'captures proxy before setup');
  assert(api.queryOpenXRRegistration().ok);
  const registered=api.queryOpenXRRegistration().resolvedLibrary;
  await api.finalize(fixture('second-game'));
  assert.equal(api.queryOpenXRRegistration().resolvedLibrary,registered,'another game does not redirect the layer');
  assert.equal(opts.manifest.reshade.openxrProvider,'V19 EarlyLoad Installer');
  assert(events.indexOf('pose installed')>events.indexOf('setup'),'pose layer follows modified ReShade installation');
  assert(!fs.readdirSync(opts.gameDir).some(n=>n.includes('disabled')),'retains patched local proxy');
  fails=true;const failed=fixture('failed');await assert.rejects(api.finalize(failed),/installer failed/);fails=false;
  assert(fs.readFileSync(path.join(failed.gameDir,'dxgi.dll'),'utf8').includes('stock'));
  badHash=true;await assert.rejects(api.finalize(fixture('wrong-dll')),/differs/);badHash=false;
  wrongManifest=true;await assert.rejects(api.finalize(fixture('wrong-manifest')),/verification failed/);wrongManifest=false;
  const unrelated=fixture('unrelated');fs.writeFileSync(path.join(unrelated.gameDir,'dxgi.dll'),'native proxy');
  await assert.rejects(api.finalize(unrelated),/unrelated proxy/);
  assert.equal(fs.readFileSync(path.join(unrelated.gameDir,'dxgi.dll'),'utf8'),'native proxy');
  const vr=fixture('openvr');vr.mode='openvr';events.length=0;await api.finalize(vr);assert(events.includes('setup'));assert(events.includes('pose installed'),'OpenVR receives all dormant OpenXR/mod helpers');
  assert.throws(()=>api.openXRSetupArgs('game.exe','vulkan'),/Direct3D/);
  // The normal backend must receive V19 too, before XR finalization.
  vm.runInNewContext("detect=()=> 'openxr'; resolvePayloadDir=()=> "+JSON.stringify(payload)+"; preflight=async()=>{};",context);
  const wrapped=fixture('wrapper');let selectedSetup;
  const backend={install:async opts=>{selectedSetup=opts.reshadeSetup;return wrapped.manifest;}};
  api.wrapBackendManager(backend);
  await backend.install({...wrapped,route:'native',vrFoveation:'balanced',reshadeSetup:'ordinary.exe'});
  assert.equal(selectedSetup,path.join(xr,setupName));
  assert(events.includes('native payload verified')&&events.includes('native foveation installed'),'native route gets its own runtime and preflight');
  for(const mode of ['none','openvr','openxr']) {
    vm.runInNewContext('detect=()=> '+JSON.stringify(mode),context);
    const current=fixture('matrix-'+mode);let chosen;
    const target={install:async o=>{chosen=o;return current.manifest;}};api.wrapBackendManager(target);
    await target.install({...current,route:'feeder',vrMode:'auto',vrFoveation:'balanced',source:{feeder:{}},installReShade:true});
    assert.equal(chosen.source.feeder.addon64,'fixed-26.28',mode+' uses current feeder');
    assert.equal(chosen.reshadeSetup,path.join(xr,setupName),mode+' uses modified ReShade');
  }
  const noVR=fixture('explicit-off');events.length=0;
  await api.finalize({...noVR,route:'native',vrFoveation:'balanced',mode:'none',enableVR:false});assert(!events.includes('setup'));assert(!events.includes('pose installed'));assert(!events.includes('native foveation installed'));
  const noReShade=fixture('no-reshade');events.length=0;
  await api.finalize({...noReShade,installReShade:false});assert(!events.includes('setup'),'honors opt-out');assert(events.includes('pose installed'));
  console.log('PASS: modified installer selection, dual API arguments, local module reuse, tracking, exact hash and manifest checks, failure handling, unrelated proxy preservation, flat/OpenXR/OpenVR install routes and opt-outs');
}finally{fs.rmSync(temp,{recursive:true,force:true});}})().catch(e=>{console.error(e);process.exitCode=1;});
