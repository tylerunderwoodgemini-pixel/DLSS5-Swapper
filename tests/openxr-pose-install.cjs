const fs=require('fs'),path=require('path'),os=require('os'),assert=require('node:assert/strict'),vm=require('vm');
const {createRequire}=require('module');
const file=path.resolve('src/core/openxr-pose-install.js'),native=createRequire(file),temp=fs.mkdtempSync(path.join(os.tmpdir(),'xr-pose-install-test-'));
const payload=path.join(temp,'payload'),game=path.join(temp,'game');fs.mkdirSync(path.join(payload,'vr-foveated'),{recursive:true});fs.mkdirSync(game);
fs.writeFileSync(path.join(payload,'vr-foveated/DLSS5OpenXRPose.dll'),'capture');fs.writeFileSync(path.join(payload,'vr-foveated/vr-pose-bridge-v1.addon64'),'Pose bridge v2 registered');
for(const name of ['vr-depth-bridge.addon64','vr-current-input.addon64','vr-projection-bridge.addon64','dlss5-vr-compat.addon64'])fs.writeFileSync(path.join(payload,'vr-foveated',name),name);
let enabled=false,copies=0;
const context={module:{exports:{}},Buffer,process:{...process,env:{...process.env,LOCALAPPDATA:temp}},require:n=>{
 if(n==='child_process')return {execFileSync:(exe,args)=>{if(args[0]==='add'){enabled=true;return '';}return enabled?'REG_DWORD 0x0':'';}};
 if(n==='./apply')return {copyTracked:async(m,g,src,dst)=>{fs.copyFileSync(src,dst);m.added.push(path.relative(g,dst));++copies;},writeTracked:async(m,g,dst,text)=>{fs.writeFileSync(dst,text);m.added.push(path.relative(g,dst));},saveActiveManifest:async()=>{}};
 return native(n);
}};
vm.runInNewContext(fs.readFileSync(file,'utf8'),context,{filename:file});
(async()=>{try{
 const api=context.module.exports,manifest={added:[]},opts={payloadDir:payload,gameDir:game,exePath:path.join(game,'game.exe'),manifest};
 const first=await api.install(opts);assert(first.ok);assert.equal(copies,5);assert(manifest.added.includes('dlss5-vr-compat.addon64'));assert(manifest.added.includes('ReShadeDesktopUI.ini'));assert(manifest.added.includes('ReShadeVR.ini'));assert(manifest.added.includes('vr-pose-bridge-v1.addon64'));assert.equal(manifest.openxrPose.version,2);
 const parsed=JSON.parse(fs.readFileSync(first.manifest));assert.equal(parsed.api_layer.name,'XR_APILAYER_DLSS5_pose');assert.equal(parsed.api_layer.disable_environment,'DISABLE_DLSS5_OPENXR_POSE');
 const stat=fs.statSync(first.dll).mtimeMs;await api.install(opts);assert.equal(fs.statSync(first.dll).mtimeMs,stat,'identical shared DLL not overwritten');
 fs.writeFileSync(path.join(payload,'vr-foveated/vr-depth-bridge.addon64'),'stereo-depth');
 await api.install(opts);assert(manifest.added.includes('vr-depth-bridge.addon64'));assert.equal(fs.readFileSync(path.join(game,'vr-depth-bridge.addon64'),'utf8'),'stereo-depth','stereo depth addon installed through tracking');
 fs.writeFileSync(path.join(payload,'vr-foveated/vr-current-input.addon64'),'current-input');
 fs.writeFileSync(path.join(payload,'vr-foveated/vr-projection-bridge.addon64'),'stereo-projection');
 await api.install(opts);assert(manifest.added.includes('vr-current-input.addon64'));assert.equal(fs.readFileSync(path.join(game,'vr-current-input.addon64'),'utf8'),'current-input');assert(manifest.added.includes('vr-projection-bridge.addon64'));assert.equal(fs.readFileSync(path.join(game,'vr-projection-bridge.addon64'),'utf8'),'stereo-projection','projection addon installed through tracking');
 fs.writeFileSync(first.manifest,JSON.stringify({api_layer:{name:'wrong',library_path:'.\\DLSS5OpenXRPose.dll'}}));assert(!api.query(path.dirname(first.manifest)).ok);
 fs.writeFileSync(path.join(payload,'vr-foveated/vr-pose-bridge-v1.addon64'),'legacy bridge');assert.throws(()=>api.verifyPayload(payload),/v2 support/);
 console.log('PASS: per-user OpenXR pose registration, exact library verification, tracked bridge copy, idempotent shared DLL update, legacy bridge rejected');
}finally{fs.rmSync(temp,{recursive:true,force:true});}})().catch(e=>{console.error(e);process.exitCode=1;});
