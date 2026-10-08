const path=require('path'),vr=require('../src/core/vr-foveation');
const root=path.resolve(__dirname,'../payload/vr-foveated');
if(!vr.cacheValid(path.join(root,'prebuilt'),vr.buildFingerprint(root)))throw Error('Missing/inconsistent VR payload; copy resources/payload from the fork portable release into payload.');
const scan=require('../src/core/scan').scanSource(path.resolve(__dirname,'../payload'));
if(!scan.feeder.ok32||!scan.feeder.ok64||!scan.deepFriedChicken.ok)throw Error('Incomplete application payload; restore it from the fork portable release.');
console.log('PASS: verified fork VR prebuilt and application payload');
