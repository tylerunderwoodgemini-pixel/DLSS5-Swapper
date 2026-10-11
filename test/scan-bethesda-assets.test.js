const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { writePe } = require('./fixtures/pe');
const { scanGame } = require('../src/core/scan');
test('Bethesda scans skip asset trees, retain runtime plugins and choose the game over Creation Kit', async t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'swapper-bethesda-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  writePe(path.join(dir, 'Starfield.exe'), { text: 'D3D12CreateDevice' });
  writePe(path.join(dir, 'CreationKit.exe'), { text: 'D3D12CreateDevice', size: 128 * 1024 });
  writePe(path.join(dir, 'sfse_loader.exe'), { text: 'D3D12CreateDevice' });
  for (const asset of ['meshes', 'textures', 'geometries', 'temp-mesh']) {
    writePe(path.join(dir, 'Data', asset, 'decoy.exe'), { text: 'D3D12CreateDevice' });
    writePe(path.join(dir, 'Data', asset, 'nvngx_dlss.dll'));
  }
  writePe(path.join(dir, 'Data', 'SFSE', 'Plugins', 'nvngx_dlss.dll'));
  const result = await scanGame(dir);
  assert.equal(result.chosen.rel, 'Starfield.exe');
  assert.equal(result.exeCandidates.length, 1);
  assert.deepEqual(result.dlssFiles.map(f => f.rel), [path.join('Data', 'SFSE', 'Plugins', 'nvngx_dlss.dll')]);
});
