const fs=require('fs'),path=require('path'),os=require('os'),assert=require('assert/strict');
const api=require('../src/core/agility-runtime');
const root=path.resolve(__dirname,'..'),game='D:/Games/Teenage Mutant Ninja Turtles - Empire City',exe=path.join(game,'TMNT-EC.exe');
(async()=>{
 assert.deepEqual(api.inspect(exe),{sdkPath:'.\\D3D12\\',version:618});
 assert.equal(api.inspect(path.join(root,'payload/agility/618/D3D12Core.dll')).version,618);
 assert.equal(api.inspect(__filename),null);
 const temp=fs.mkdtempSync(path.join(os.tmpdir(),'dlss5-agility-'));
 fs.copyFileSync(exe,path.join(temp,'game.exe'));
 const args={payloadDir:path.join(root,'payload'),gameDir:temp,exePath:path.join(temp,'game.exe')};
 const result=await api.install(args);assert(result.installed);assert.equal(result.version,618);
 assert.equal((await api.install(args)).installed,false,'existing SDK must remain untouched');
 fs.writeFileSync(result.file,'existing custom SDK');
 assert.equal((await api.install(args)).installed,false);assert.equal(fs.readFileSync(result.file,'utf8'),'existing custom SDK');
 fs.rmSync(temp,{recursive:true,force:true});
 console.log('PASS: real PE SDK version/path, non-PE rejection, exact-version deployment, existing runtime preservation and idempotence');
})().catch(e=>{console.error(e);process.exitCode=1;});
