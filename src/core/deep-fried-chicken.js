'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const extractZip = require('extract-zip');

const RELEASE = {
  version: '1.4.8-alpha',
  files: {
    'deep-fried-chicken.addon64': '106143de0d74853b966a7141c19c2bb0fb46e7ce32c7f67d830d2dc5cddf80ec',
    'deep-fried-chicken-nvngx.dll': 'e218ce8c20858d58e53a85b2afc2c1e6f55768a96659e819ff40d71e872393a3',
    'dlss5-dx11-bridge.addon64': '014ab26c0437fd6180595452c2acc3ecba50b1df42c088212f1b20baaeff4cec'
  }
};

function digest(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function validateRoot(root) {
  const required = ['deep-fried-chicken.addon64', 'deep-fried-chicken-nvngx.dll', 'deep-fried-chicken.cfg'];
  for (const name of required) if (!fs.existsSync(path.join(root, name))) throw new Error(`Deep Fried Chicken archive is missing ${name}`);
  for (const [name, expected] of Object.entries(RELEASE.files)) {
    const file = path.join(root, name);
    if (!fs.existsSync(file)) continue;
    if (digest(file) !== expected) throw new Error(`Deep Fried Chicken ${name} failed SHA-256 verification`);
  }
  return {
    root,
    addon: path.join(root, 'deep-fried-chicken.addon64'),
    nvngx: path.join(root, 'deep-fried-chicken-nvngx.dll'),
    cfg: path.join(root, 'deep-fried-chicken.cfg'),
    dx11Bridge: fs.existsSync(path.join(root, 'dlss5-dx11-bridge.addon64')) ? path.join(root, 'dlss5-dx11-bridge.addon64') : null,
    version: RELEASE.version
  };
}

async function importArchive(zipFile, cacheRoot) {
  if (!zipFile || !fs.existsSync(zipFile)) throw new Error('Deep Fried Chicken release archive was not found');
  const base = path.join(cacheRoot, 'components', `Deep-Fried-Chicken-${RELEASE.version}`);
  const temp = base + '.extracting';
  await fs.promises.rm(temp, { recursive: true, force: true });
  await fs.promises.mkdir(temp, { recursive: true });
  await extractZip(zipFile, { dir: temp });
  // Official 1.4.8-alpha is flat, but accept a single enclosing directory too.
  let root = temp;
  try {
    const entries = fs.readdirSync(temp, { withFileTypes: true });
    if (!fs.existsSync(path.join(temp, 'deep-fried-chicken.addon64')) && entries.length === 1 && entries[0].isDirectory()) root = path.join(temp, entries[0].name);
  } catch {}
  validateRoot(root);
  await fs.promises.rm(base, { recursive: true, force: true });
  await fs.promises.rename(temp, base);
  let finalRoot = base;
  if (!fs.existsSync(path.join(base, 'deep-fried-chicken.addon64'))) {
    const entries = fs.readdirSync(base, { withFileTypes: true });
    if (entries.length === 1 && entries[0].isDirectory()) finalRoot = path.join(base, entries[0].name);
  }
  return validateRoot(finalRoot);
}

function cached(cacheRoot) {
  const base = path.join(cacheRoot, 'components', `Deep-Fried-Chicken-${RELEASE.version}`);
  const candidates = [base];
  try { for (const e of fs.readdirSync(base, { withFileTypes: true })) if (e.isDirectory()) candidates.push(path.join(base, e.name)); } catch {}
  for (const root of candidates) {
    try { return validateRoot(root); } catch {}
  }
  return null;
}

module.exports = { RELEASE, validateRoot, importArchive, cached };
