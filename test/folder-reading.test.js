const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../src/renderer/renderer.js'), 'utf8');
function fixture(details, artFetch) {
  const sheet = { innerHTML: '' }, close = {};
  const context = vm.createContext({ setTimeout, clearTimeout, window: { lab: { details, artFetch } },
    state: { games: [{ dir: 'A' }, { dir: 'B' }] }, jobRunning: false,
    sheetGame: null, sheetDetails: null, jobLines: [], esc: String, closeSheet() {},
    $: id => id === 'sheet' ? sheet : id === 'sheetClose' ? close : { classList: { remove() {} } } });
  const start = source.indexOf('let sheetRequest = 0;');
  const end = source.indexOf('  sheetDetails = d;', start) + '  sheetDetails = d;'.length;
  vm.runInContext(source.slice(start, end) + '\n}', context);
  return { context, sheet, open: dir => context.openSheet(dir) };
}
test('stalled optional artwork does not trap folder loading', async () => {
  let cacheOnly;
  const f = fixture(async () => ({ ok: true }), (...args) => { cacheOnly = args[3]; return new Promise(() => {}); });
  await f.open('A');
  assert.equal(f.context.sheetDetails.ok, true);
  assert.equal(cacheOnly, true);
});
test('read failure displays an error and permits another selection', async () => {
  const f = fixture(async dir => { if (dir === 'A') throw Error('Read denied'); return { ok: true }; }, async () => null);
  await assert.rejects(f.open('A'), /Read denied/);
  assert.match(f.sheet.innerHTML, /Read denied/);
  await f.open('B');
  assert.equal(f.context.sheetDetails.ok, true);
});
test('late folder results cannot replace a newer selection', async () => {
  let resolve;
  const f = fixture(dir => dir === 'A' ? new Promise(r => { resolve = r; }) : Promise.resolve({ game: 'B' }), async () => null);
  const first = f.open('A');
  await f.open('B');
  resolve({ game: 'A' });
  await first;
  assert.equal(f.context.sheetDetails.game, 'B');
});
