const fs=require('fs'),path=require('path'),crypto=require('crypto'),vr=require('../src/core/vr-foveation');
const root=path.resolve(__dirname,'../payload/vr-foveated'),prebuilt=path.join(root,'prebuilt'),files={};
for(const n of fs.readdirSync(prebuilt).filter(n=>n!=='build-manifest.json'))files[n]=crypto.createHash('sha256').update(fs.readFileSync(path.join(prebuilt,n))).digest('hex');
fs.writeFileSync(path.join(prebuilt,'build-manifest.json'),JSON.stringify({marker:'1.16.0-beta.4-vr-universal-foveated26.28',fingerprint:vr.buildFingerprint(root),files},null,2));
