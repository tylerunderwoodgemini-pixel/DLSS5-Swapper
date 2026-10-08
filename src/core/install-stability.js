'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const VERSION = 1;
// Synthetic feeder guides already use NGX pixel units and declare depth
// convention themselves. DFC must consume that contract without rescaling it.
const FEED = Object.freeze({ reset_every:'0', warmup_rebuild:'0', mv_scale_x:'1.000', mv_scale_y:'1.000' });
function rules(text, chicken) {
  if (!chicken) return { ...FEED };
  const values = {};
  for (let layer=1;layer<=30;layer++) {
    for (const [suffix,value] of Object.entries({mvec_scale_x_multiplier:'1.000',mvec_scale_y_multiplier:'1.000',depth_convention:'0'})) {
      const key=`layer_${layer}_${suffix}`;
      // Avoid manufacturing unused passes in older config schemas.
      if (layer===1 || new RegExp('^\\s*'+key+'\\s*=','mi').test(text)) values[key]=value;
    }
  }
  return values;
}
function normalize(text, chicken=false) {
  const values=rules(text,chicken),seen=new Set(),changes=[];
  const lines=String(text||'').replace(/^\uFEFF/,'').split(/\r?\n/);
  const output=lines.map(line=>{
    const match=line.match(/^\s*([^#;=]+?)\s*=\s*(.*)$/);
    if(!match) return line;
    const key=match[1].trim().toLowerCase();
    if(!Object.hasOwn(values,key)) return line;
    seen.add(key);
    if(match[2].trim()!==values[key]) changes.push({key,before:match[2].trim(),after:values[key]});
    // Normalize every duplicate, since parsers differ on first/last precedence.
    return `${key}=${values[key]}`;
  });
  for(const [key,value] of Object.entries(values)) if(!seen.has(key)) {output.push(`${key}=${value}`);changes.push({key,before:null,after:value});}
  return {text:output.join('\r\n').replace(/(?:\r\n)*$/,'\r\n'),changes};
}
function verify(text, chicken=false) {
  const values=rules(text,chicken),seen=new Set();
  for(const line of text.split(/\r?\n/)) {
    const match=line.match(/^\s*([^#;=]+?)\s*=\s*(.*)$/);if(!match)continue;
    const key=match[1].trim().toLowerCase();if(!Object.hasOwn(values,key))continue;
    if(match[2].trim()!==values[key])throw new Error(`Installed temporal guide setting failed verification: ${key}`);
    seen.add(key);
  }
  for(const key of Object.keys(values))if(!seen.has(key))throw new Error(`Installed temporal guide setting missing: ${key}`);
  return true;
}
async function apply({core,manifest,gameDir,exeDir,neuralProvider}) {
  const files=[['dlss5-feed.cfg',false]];
  if(neuralProvider==='deep-fried-chicken')files.push(['deep-fried-chicken.cfg',true]);
  const report={version:VERSION,scope:'64-bit Direct3D synthetic feeder',verified:false,files:[]};
  for(const [name,chicken] of files) {
    const dest=path.join(exeDir,name),before=fs.readFileSync(dest,'utf8'),result=normalize(before,chicken);
    if(before!==result.text)await core.writeTracked(manifest,gameDir,dest,result.text,{kind:'config'});
    const installed=fs.readFileSync(dest,'utf8');verify(installed,chicken);
    report.files.push({file:path.relative(gameDir,dest),sha256:crypto.createHash('sha256').update(installed).digest('hex'),changes:result.changes});
  }
  report.verified=true;manifest.installStability=report;
  await core.saveActiveManifest(gameDir,manifest);
  return report;
}
module.exports={VERSION,normalize,verify,apply};
