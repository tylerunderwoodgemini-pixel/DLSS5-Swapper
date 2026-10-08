'use strict';

// LumeniteFX's licence requires redistribution through the author's official
// links. It is therefore fetched directly from the upstream GitHub repository
// on first use, verified, cached locally, and never bundled in our release.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const extractZip = require('extract-zip');
const pe = require('./pe');

const DGVOODOO = {
  version: '2.87.4',
  url: 'https://github.com/dege-diosg/dgVoodoo2/releases/download/v2.87.4/dgVoodoo2_87_4.zip',
  sha256: '74aeb464d829db80e3f4aa8fae235e6e3b38fc01188776c5c2376bb0dea0956e'
};

async function ensureDgVoodoo(cacheRoot) {
  const base = path.join(path.resolve(cacheRoot), 'components', `dgVoodoo2-${DGVOODOO.version}`);
  const archive = base + '.zip';
  if (!fs.existsSync(archive) || digest(archive) !== DGVOODOO.sha256) await download(DGVOODOO.url, archive);
  if (digest(archive) !== DGVOODOO.sha256) throw new Error('dgVoodoo2 SHA-256 verification failed');
  // Re-extract the verified archive, never trust previously cached loose DLLs.
  await extractZip(archive, { dir: base });
  return base;
}

function missingVCRuntime(bitness, exeDir, systemRoot = process.env.SystemRoot, extra = []) {
  if (!systemRoot) return [];
  const systemDir = path.join(systemRoot, bitness === 32 ? 'SysWOW64' : 'System32');
  const names = ['msvcp140.dll', 'vcruntime140.dll', ...(bitness === 64 ? ['vcruntime140_1.dll'] : []), ...extra];
  return names.filter(name => {
    const local = exeDir && path.join(exeDir, name);
    const file = local && fs.existsSync(local) ? local : path.join(systemDir, name);
    return pe.getBitness(file) !== bitness;
  });
}

const NEURAL_UPSTREAM = {
  version: 'v0.3.0',
  url: 'https://github.com/matiasLombo/neural-upstream/releases/download/v0.3.0/nvngx.dll.addon64',
  filename: 'nvngx.dll.addon64'
};

async function ensureNeuralUpstream(cacheRoot) {
  const dir = path.join(path.resolve(cacheRoot), 'components', `neural-upstream-${NEURAL_UPSTREAM.version}`);
  const file = path.join(dir, NEURAL_UPSTREAM.filename);
  if (!fs.existsSync(file)) await download(NEURAL_UPSTREAM.url, file);
  // Basic PE sanity check. The upstream project intentionally ships a loose .addon64.
  if (pe.getBitness(file) !== 64) { try { await fs.promises.unlink(file); } catch {} throw new Error('Neural Upstream download is not a 64-bit PE add-on'); }
  return file;
}


const AIO_UPSTREAM = {
  version: 'v2.1.1',
  url: 'https://github.com/kibblerz/DLSS5-Reshade-AIO/releases/download/v2.1.1/DLSS5-ReShade-AIO-v2.1.1-64-bit.zip',
  filename: 'DLSS5-ReShade-AIO-v2.1.1-64-bit.zip'
};

async function ensureAioUpstream(cacheRoot) {
  const base = path.join(path.resolve(cacheRoot), 'components', `DLSS5-ReShade-AIO-${AIO_UPSTREAM.version}`);
  const archive = path.join(path.dirname(base), AIO_UPSTREAM.filename);
  const marker = path.join(base, '.ready');
  if (!fs.existsSync(archive)) await download(AIO_UPSTREAM.url, archive);
  if (!fs.existsSync(marker)) {
    await fs.promises.rm(base, { recursive: true, force: true });
    await fs.promises.mkdir(base, { recursive: true });
    await extractZip(archive, { dir: base });
  }
  const find = (name) => {
    const stack=[base];
    while(stack.length){
      const d=stack.pop();
      for(const e of fs.readdirSync(d,{withFileTypes:true})){
        const q=path.join(d,e.name);
        if(e.isDirectory()) stack.push(q); else if(e.name.toLowerCase()===name.toLowerCase()) return q;
      }
    }
    return null;
  };
  const addon=find('standalone-dlssnr.addon64');
  const bridge=find('nvngx.dll');
  if(!addon || !bridge || pe.getBitness(addon)!==64 || pe.getBitness(bridge)!==64) {
    await fs.promises.rm(base,{recursive:true,force:true});
    throw new Error('DLSS5 ReShade AIO archive layout/architecture is invalid');
  }
  await fs.promises.writeFile(marker, AIO_UPSTREAM.version, 'utf8');
  const shaderRoot = path.join(path.dirname(addon), 'reshade-shaders');
  return { root:path.dirname(addon), addon, bridge, shaderRoot: fs.existsSync(shaderRoot) ? shaderRoot : null, version:AIO_UPSTREAM.version };
}

const LUMENITE = {
  commit: '76fa3e4d601c97e9bc63f119c01405b7b9938885',
  url: 'https://codeload.github.com/umar-afzaal/LumeniteFX/zip/76fa3e4d601c97e9bc63f119c01405b7b9938885',
  sha256: 'bf574543a6af6527587af0bad139922e8c0363bb154cdfb3e41133c7dca2ee3f'
};

function digest(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

async function download(url, file) {
  const response = await fetch(url, {
    headers: { 'User-Agent': 'DLSS5-Swapper/2.1' },
    signal: AbortSignal.timeout(120000)
  });
  if (!response.ok) throw new Error(`Download failed (${response.status})`);
  const temp = file + '.part';
  await fs.promises.mkdir(path.dirname(file), { recursive: true });
  await fs.promises.writeFile(temp, Buffer.from(await response.arrayBuffer()));
  await fs.promises.rename(temp, file);
}

async function ensureLumenite(cacheRoot) {
  const base = path.join(cacheRoot, 'components', `LumeniteFX-${LUMENITE.commit.slice(0, 8)}`);
  const archive = base + '.zip';
  const marker = path.join(base, '.verified');
  if (fs.existsSync(marker)) {
    const folders = fs.readdirSync(base).filter((name) => name !== '.verified');
    const root = folders.map((name) => path.join(base, name)).find((item) => fs.existsSync(path.join(item, 'Shaders')));
    if (root) return root;
  }

  if (!fs.existsSync(archive) || digest(archive) !== LUMENITE.sha256) {
    try { await fs.promises.unlink(archive); } catch {}
    await download(LUMENITE.url, archive);
  }
  if (digest(archive) !== LUMENITE.sha256) {
    try { await fs.promises.unlink(archive); } catch {}
    throw new Error('LumeniteFX SHA-256 verification failed');
  }

  await fs.promises.rm(base, { recursive: true, force: true });
  await extractZip(archive, { dir: base });
  const folders = fs.readdirSync(base);
  const root = folders.map((name) => path.join(base, name)).find((item) => fs.existsSync(path.join(item, 'Shaders')));
  if (!root) throw new Error('LumeniteFX archive layout is invalid');
  await fs.promises.writeFile(marker, LUMENITE.sha256, 'utf8');
  return root;
}

module.exports = { LUMENITE, DGVOODOO, NEURAL_UPSTREAM, AIO_UPSTREAM, ensureNeuralUpstream, ensureAioUpstream, ensureLumenite, ensureDgVoodoo, missingVCRuntime, digest, download };
