const test=require('node:test'),assert=require('node:assert/strict');
const config=require('../src/core/feeder-config');
test('VR spatial AA follows neural output once, preserves explicit settings and unrelated effects',()=>{
 const initial='Techniques=Other@Other.fx,DLSS5_SpatialAA@DLSS5_SpatialAA.fx\n[DLSS5_SpatialAA.fx]\nAA_STRENGTH=0\nAA_PACKED_STEREO=0\n';
 const out=config.configurePreset(initial,3,{vrAA:true});
 assert.equal(config.getIni(out,'','Techniques'),'Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx,DLSS5_SpatialAA@DLSS5_SpatialAA.fx,Other@Other.fx');
 assert.equal(config.getIni(out,'DLSS5_SpatialAA.fx','AA_STRENGTH'),'0');
 assert.equal(config.getIni(out,'DLSS5_SpatialAA.fx','AA_PACKED_STEREO'),'0');
 assert.equal(config.configurePreset(out,3,{vrAA:true}),out);
 const fresh=config.configurePreset('',3,{vrAA:true});assert.equal(config.getIni(fresh,'DLSS5_SpatialAA.fx','AA_STRENGTH'),'0.65');
 assert(!config.configurePreset('',3).includes('SpatialAA'));
});
