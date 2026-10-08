const assert=require('node:assert/strict'),fs=require('fs'),path=require('path'),os=require('os'),crypto=require('crypto');
const vr=require('../src/core/vr-foveation');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'dlss5-packaged-')),old=process.env.LOCALAPPDATA;
const payload=path.resolve(__dirname,'../payload'),root=path.join(payload,'vr-foveated'),events=[];
try{
 process.env.LOCALAPPDATA=temp;
 const stamp=vr.buildFingerprint(root);assert(vr.cacheValid(path.join(root,'prebuilt'),stamp));
 const stockFeeder={shaderRoot:path.join(payload,'feeder/reshade-shaders')};
 let installed=vr.ensureBuilt({payloadDir:payload,stockFeeder},e=>events.push(e));
 assert(!events.some(e=>e.params.state==='building'),'installation copies offline payload without a compiler/download');
 const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
 assert.equal(digest(installed.addon64),'6b561ae84c85c04efd3d3a1e697740aecc1b18deac4d1aef51f99d00d6f00b73');
 assert.equal(digest(installed.depthBridge),'7ff2ea7901e3a57731dd51b9bb57f354ca7efbd51a461bac3483fb2242534371');
 assert.equal(digest(path.join(path.dirname(installed.addon64),'vr-current-input.addon64')),'b63f208998a89e3c9697eb248e7262733a63021c33f798e4a9a37d5b3dcdf0df');
 assert.equal(digest(installed.feedShader),digest(path.join(root,'DLSS5_Feed.fx')));
 assert(fs.existsSync(path.join(installed.shaderRoot,'Shaders/ReShade.fxh')));
 fs.appendFileSync(installed.depthBridge,'stale');
 installed=vr.ensureBuilt({payloadDir:payload,stockFeeder});assert.equal(digest(installed.depthBridge),digest(path.join(root,'vr-depth-bridge.addon64')),'corrupt cache restored from package');
 const altered=path.join(temp,'altered');fs.cpSync(root,altered,{recursive:true});
 fs.appendFileSync(path.join(altered,'native/vr-foveated-d3d12.h'),'\n// fingerprint regression');
 assert.notEqual(vr.buildFingerprint(altered),stamp,'D3D12 native changes invalidate cache');
 fs.cpSync(root,altered,{recursive:true});
 fs.appendFileSync(path.join(altered,'native/vr-depth-layout.h'),'\n// layout fingerprint regression');
 assert.notEqual(vr.buildFingerprint(altered),stamp,'shared eye layout changes invalidate cache');
 const scan=require('../src/core/scan').scanSource(payload);assert(scan.feeder.ok32&&scan.feeder.ok64&&scan.deepFriedChicken.ok,'both architectures and provider remain installable');
 console.log('PASS: offline packaged installation, exact working binaries, full shader tree, damaged-cache recovery, native-header invalidation, 32/64-bit payload validation');
}finally{if(old===undefined)delete process.env.LOCALAPPDATA;else process.env.LOCALAPPDATA=old;fs.rmSync(temp,{recursive:true,force:true});}
