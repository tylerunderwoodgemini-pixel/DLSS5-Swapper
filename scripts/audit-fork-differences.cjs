// Read-only code/payload audit; writes only the documentation inventory.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const root = path.resolve(__dirname, '..');
const codeRef = process.argv[2] || '030d43e';
const payload = path.resolve(process.argv[3] || path.join(root, '..', 'resources', 'payload'));
const git = args => execFileSync('git', args, { cwd: root, encoding: 'utf8' }).trim();
const differences = ref => git(['diff', '--no-renames', '--name-status', ref, codeRef])
  .split('\n').filter(Boolean).map(row => {
    const [status, file] = row.split('\t');
    return { status, path: file };
  });
const files = [];
function walk(dir) {
  for (const item of fs.readdirSync(dir, { withFileTypes: true }).sort((a,b) => a.name.localeCompare(b.name))) {
    const file = path.join(dir, item.name);
    if (item.isDirectory()) walk(file);
    else if (item.isFile()) {
      const bytes = fs.readFileSync(file);
      files.push({ path: path.relative(payload, file).split(path.sep).join('/'), bytes: bytes.length,
        sha256: crypto.createHash('sha256').update(bytes).digest('hex') });
    }
  }
}
walk(payload);
const audit = {
  auditDate: '2026-10-10',
  scope: 'Runtime code snapshot and complete active payload; later documentation edits are outside this snapshot.',
  runtimeCodeCommit: git(['rev-parse', codeRef]),
  base: { tag: 'v2.2.0', commit: git(['rev-parse', 'v2.2.0^{commit}']) },
  comparedUpstream: { tag: 'v2.2.9', commit: git(['rev-parse', 'v2.2.9^{commit}']) },
  statusLegend: { A: 'present only in fork', M: 'different bytes', D: 'present only in compared upstream' },
  differencesFromBase: differences('v2.2.0'),
  differencesFromCurrentUpstream: differences('v2.2.9'),
  activePayloadFiles: files
};
fs.writeFileSync(path.join(root, 'docs', 'FORK_AUDIT.json'), JSON.stringify(audit, null, 2) + '\n');
console.log(`Audit: ${audit.differencesFromBase.length} paths versus base; ${audit.differencesFromCurrentUpstream.length} versus v2.2.9; ${files.length} payload files.`);
