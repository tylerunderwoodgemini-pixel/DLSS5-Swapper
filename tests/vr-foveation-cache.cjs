const assert=require('node:assert/strict');
const fs=require('fs');
const os=require('os');
const path=require('path');
const crypto=require('crypto');
const {cacheValid,buildFingerprint,configFor}=require('../src/core/vr-foveation');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'vr-cache-test-'));
try {
  const marker='1.16.0-beta.4-vr-universal-foveated26.28';
  const files={};
  for(const name of ['dlss5-feed.addon64','DLSS5_Feed.fx','vr-pose-bridge-v1.addon64','vr-depth-bridge.addon64','vr-projection-bridge.addon64','vr-current-input.addon64','VR_FOVEATED_V26.txt']) {
    const data=Buffer.from(name==='dlss5-feed.addon64'?marker:name);
    fs.writeFileSync(path.join(temp,name),data);
    files[name]=crypto.createHash('sha256').update(data).digest('hex');
  }
  assert.equal(cacheValid(temp,'new-builder'),false,'old caches without manifest rebuild');
  fs.writeFileSync(path.join(temp,'build-manifest.json'),JSON.stringify({marker,fingerprint:'new-builder',files}));
  assert.equal(cacheValid(temp,'new-builder'),true);
  assert.equal(cacheValid(temp,'changed-builder'),false,'builder changes force rebuild');
  fs.appendFileSync(path.join(temp,'dlss5-feed.addon64'),'corruption');
  assert.equal(cacheValid(temp,'new-builder'),false,'corrupted addon forces rebuild');
  const payload=path.resolve(__dirname,'../payload/vr-foveated');
  assert.match(buildFingerprint(payload),/^[a-f0-9]{64}$/);
  assert.equal(configFor('balanced').width,60);
  console.log('PASS: cache invalidation, corruption detection, payload fingerprint and preset');
} finally { fs.rmSync(temp,{recursive:true,force:true}); }
