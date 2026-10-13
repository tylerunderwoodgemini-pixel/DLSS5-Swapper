'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {plan}=require('../src/core/vr-compatibility-plan');
test('capability matrix covers both routes and delayed/native/modded VR without game-name rules',()=>{
  for(const mode of ['none','openxr','openvr'])for(const route of ['native','feeder']){
    const p=plan({api:'d3d12',route,mode,bitness:64,vrFoveation:'balanced',neuralProvider:'deep-fried-chicken'});
    assert.equal(p.enabled,true);assert.equal(p.lateVrStartup,true);
    assert.equal(p.runtimeValidationRequired,true);assert.deepEqual(p.center,{widthPercent:60,heightPercent:50});
    assert.equal(p.foveationPath,route==='native'?'native-d3d12-nr':'feeder');
  }
});
test('unsupported APIs, native D3D11, 32-bit, and explicit VR off do not claim working foveation',()=>{
  for(const api of ['d3d9','d3d10','opengl','unknown','vulkan'])assert.equal(plan({api,route:'native',vrFoveation:'small'}).enabled,false);
  assert.equal(plan({api:'d3d11',route:'native',vrFoveation:'small'}).enabled,false);
  assert.equal(plan({api:'d3d12',route:'native',bitness:32,vrFoveation:'small'}).enabled,false);
  assert.equal(plan({api:'d3d12',route:'feeder',vrMode:'off',vrFoveation:'small'}).enabled,false);
});
test('Vulkan Feeder cropping requires its DFC bridge and a selected headset route',()=>{
  for(const mode of ['openxr','openvr'])assert.equal(plan({api:'vulkan',route:'feeder',mode,neuralProvider:'deep-fried-chicken',vrFoveation:'large'}).enabled,true);
  for(const provider of ['renodx','true-upstream'])assert.equal(plan({api:'vulkan',route:'feeder',mode:'openxr',neuralProvider:provider,vrFoveation:'large'}).enabled,false);
  assert.equal(plan({api:'vulkan',route:'feeder',mode:'none',neuralProvider:'deep-fried-chicken',vrFoveation:'large'}).enabled,false);
});

test('OpenXR full menu covers all supported Windows graphics bindings without claiming new foveation paths',()=>{
 for(const api of ['dxgi','d3d11','d3d12','vulkan','opengl']){
  const p=plan({api,bitness:64,route:'feeder',vrFoveation:'off'});assert.equal(p.headsetMenu.key,'F8');assert.equal(p.headsetMenu.stationary,true);assert.match(p.headsetMenu.controller,/3 seconds/);
 }
 assert.equal(plan({api:'vulkan',bitness:32,route:'feeder'}).headsetMenu,null);
 assert.equal(plan({api:'opengl',vrMode:'off',route:'feeder'}).headsetMenu,null);
 assert.equal(plan({api:'opengl',route:'feeder',vrFoveation:'small'}).enabled,false);
});
