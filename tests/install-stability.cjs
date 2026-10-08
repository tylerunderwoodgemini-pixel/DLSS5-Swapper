'use strict';
const assert=require('node:assert/strict'),fs=require('fs'),path=require('path'),os=require('os');
const stable=require('../src/core/install-stability'),core=require('../src/core/apply');
const feed='\uFEFF# custom config\nRESET_EVERY=1\nreset_every=1\nwarmup_rebuild=180\nmv_scale_x=0.5\nmv_scale_y=0.838\nvr_foveation_width=75\nwork_sharpness=0.15\ncustom_key=keep\n';
const chicken='layers=5\nlayer_1_nr_style=2\nlayer_1_local_tone=0.75\nlayer_1_depth_convention=2\nlayer_1_mvec_scale_y_multiplier=0.838\nLAYER_1_MVEC_SCALE_Y_MULTIPLIER=0.7\nlayer_5_mvec_scale_x_multiplier=2\nlayer_5_depth_convention=1\ncolor_strength=0.14\n';
(async()=>{
 const fixed=stable.normalize(feed),dfc=stable.normalize(chicken,true);
 stable.verify(fixed.text);stable.verify(dfc.text,true);
 assert(fixed.text.includes('vr_foveation_width=75'));assert(fixed.text.includes('custom_key=keep'));assert(fixed.text.includes('work_sharpness=0.15'));
 assert(dfc.text.includes('layers=5'));assert(dfc.text.includes('layer_1_nr_style=2'));assert(dfc.text.includes('layer_1_local_tone=0.75'));assert(dfc.text.includes('color_strength=0.14'));
 assert.equal((fixed.text.match(/reset_every=0/g)||[]).length,2);assert.equal((dfc.text.match(/layer_1_mvec_scale_y_multiplier=1.000/g)||[]).length,2);
 assert(dfc.text.includes('layer_5_mvec_scale_x_multiplier=1.000'));assert(dfc.text.includes('layer_5_depth_convention=0'));
 assert.equal(stable.normalize(fixed.text).text,fixed.text);assert.equal(stable.normalize(dfc.text,true).text,dfc.text);
 assert.throws(()=>stable.verify(fixed.text.replace('reset_every=0','reset_every=1')),/verification/);
 assert.throws(()=>stable.verify(dfc.text.replace('layer_5_depth_convention=0','layer_5_depth_convention=2'),true),/verification/);
 const fresh=fs.readFileSync(path.resolve(__dirname,'../payload/deep-fried-chicken/deep-fried-chicken.cfg'),'utf8');stable.verify(stable.normalize(fresh,true).text,true);
 const game=fs.mkdtempSync(path.join(os.tmpdir(),'swapper-stability-')),dir=path.join(game,'Binaries','Win64');fs.mkdirSync(dir,{recursive:true});
 try {
  fs.writeFileSync(path.join(dir,'dlss5-feed.cfg'),feed);fs.writeFileSync(path.join(dir,'deep-fried-chicken.cfg'),chicken);
  const manifest=core.beginManifest(game,path.join(dir,'game.exe'),'dxgi');
  const report=await stable.apply({core,manifest,gameDir:game,exeDir:dir,neuralProvider:'deep-fried-chicken'});assert(report.verified);assert.equal(report.files.length,2);
  for(const [name,original] of [['dlss5-feed.cfg',feed],['deep-fried-chicken.cfg',chicken]])assert.equal(fs.readFileSync(core.originalPath(game,manifest,path.relative(game,path.join(dir,name))),'utf8'),original,'tracked original configuration remains recoverable');
  const second=await stable.apply({core,manifest,gameDir:game,exeDir:dir,neuralProvider:'deep-fried-chicken'});assert(second.files.every(f=>f.changes.length===0));
  const disk=JSON.parse(fs.readFileSync(path.join(game,'_DLSS5_Backup/manifest.json'),'utf8'));assert(disk.installStability.verified);
 }finally{fs.rmSync(game,{recursive:true,force:true});}
 console.log('PASS: stale/duplicate settings repaired, selected appearance and foveation preserved, fresh defaults verified, tampering detected, actual tracked backups and repeat-install idempotence');
})().catch(e=>{console.error(e);process.exitCode=1;});
