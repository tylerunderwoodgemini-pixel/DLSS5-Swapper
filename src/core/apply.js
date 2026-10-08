'use strict';
// Does the actual work: backs up, swaps the DLLs in place, drops the add-on
// next to the executable, and installs ReShade headlessly.
//
// Nothing here writes user-facing prose. Every step reports a code plus its
// values, and the renderer turns that into whichever language is selected.
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const pe = require('./pe');
const { scanGame, inspectReShade } = require('./scan');
const feederConfig = require('./feeder-config');
const vulkanLayer = require('./vulkan-layer');
const journal = require('./file-journal');
const crypto = require('crypto');

const BACKUP_DIR = '_DLSS5_Backup';
const MANIFEST = 'manifest.json';

// Compatibility helper kept local to apply.js so DFC install does not depend on
// a particular feeder-config.js revision exporting configureChickenReShade().
function configureChickenReShadeCompat(text, { earlyLoad = true } = {}) {
  let out = feederConfig.setIni(String(text || ''), 'ADDON', 'AddonPath', '.\\');
  const current = String(feederConfig.getIni(out, 'ADDON', 'LoadFromDllMain') || '')
    .split(',')
    .map((x) => x.trim())
    .filter(Boolean)
    .filter((x) => x.toLowerCase() !== 'deep-fried-chicken.addon64');

  // NMS/Vulkan is a special case: DFC must NOT be early-loaded there.
  // ReShade's normal add-on scan must load it after ReShade is initialized.
  if (earlyLoad) current.push('deep-fried-chicken.addon64');

  out = feederConfig.setIni(out, 'ADDON', 'LoadFromDllMain', current.join(','));
  return out;
}

// Exact binaries used for automatic NMS-vs-normal DFC selection.
const DFC_STOCK_SHA256 = '106143de0d74853b966a7141c19c2bb0fb46e7ce32c7f67d830d2dc5cddf80ec';
const DFC_NMS_SHA256 = '19c3f85b3d7a05648d4f2d1af600aa9f323176e2c2aa717c030faae7a724d5a9';

function sha256File(file) {
  try {
    return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toLowerCase();
  } catch {
    return null;
  }
}

function findChickenBinaryByHash(defaultAddon, wantedHash) {
  if (!defaultAddon) return null;
  const dir = path.dirname(defaultAddon);
  const candidates = [defaultAddon];

  try {
    for (const name of fs.readdirSync(dir)) {
      const candidate = path.join(dir, name);
      if (!candidates.includes(candidate) && fs.statSync(candidate).isFile()) {
        candidates.push(candidate);
      }
    }
  } catch {}

  return candidates.find((file) => sha256File(file) === wantedHash) || null;
}

function selectChickenAddon(source, { nmsVulkan = false } = {}) {
  const configured =
    source &&
    source.deepFriedChicken &&
    source.deepFriedChicken.addon64;

  if (!configured) return null;

  if (nmsVulkan) {
    const nmsAddon = path.join(
      path.dirname(configured),
      'deep-fried-chicken-nms.addon64'
    );

    if (!fs.existsSync(nmsAddon)) {
      throw new Error(
        'NMS DFC payload missing: ' + nmsAddon
      );
    }

    const hash = sha256File(nmsAddon);

    if (hash !== DFC_NMS_SHA256) {
      throw new Error(
        'NMS DFC payload has the wrong SHA-256. Found: ' + hash
      );
    }

    return nmsAddon;
  }

  return configured;
}
function backupRoot(gameDir) {
  return path.join(gameDir, BACKUP_DIR);
}

function originalPath(gameDir, manifest, rel) {
  const prefix = manifest.backupPrefix || '';
  if (prefix && !/^originals\/[a-f0-9-]+$/.test(prefix)) throw fail('errBackupInvalid');
  return journal.safePath(backupRoot(gameDir), path.join(prefix, rel));
}

const relKey = (rel) => path.normalize(String(rel)).toLowerCase();

// An active manifest describes the machine state from before the first swap,
// not merely the most recent click on Install. Reusing it is what makes a
// second install (or an upgrade to a newer payload) still restore the genuine
// originals instead of forgetting files that were already up to date.
function beginManifest(gameDir, exePath, api) {
  const manifestPath = path.join(backupRoot(gameDir), MANIFEST);
  let previous = null;
  if (fs.existsSync(manifestPath)) {
    try {
      previous = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    } catch {
      throw fail('errBackupInvalid');
    }
    if (!previous || previous.version !== 1) throw fail('errBackupInvalid');
  }

  return {
    ...(previous || {}),
    version: 1,
    backupPrefix: previous ? (previous.backupPrefix || '') : `originals/${crypto.randomUUID()}`,
    date: new Date().toISOString(),
    game: { dir: gameDir, exe: path.relative(gameDir, exePath), api },
    replaced: Array.isArray(previous && previous.replaced) ? [...previous.replaced] : [],
    added: Array.isArray(previous && previous.added) ? [...previous.added] : [],
    addedDirs: Array.isArray(previous && previous.addedDirs) ? [...previous.addedDirs] : [],
    reshade: {
      installedByUs: false,
      file: null,
      filesAdded: [],
      ...((previous && previous.reshade) || {})
    }
  };
}

async function saveActiveManifest(gameDir, manifest) {
  await journal.capture(gameDir, path.join(backupRoot(gameDir), MANIFEST));
  await fs.promises.mkdir(backupRoot(gameDir), { recursive: true });
  await journal.atomicJson(path.join(backupRoot(gameDir), MANIFEST), manifest);
}

function captureReShadeAttempt(manifest, exeDir, known, hook, hookExisted) {
  manifest.reshade.filesAdded = [...new Set([
    ...(manifest.reshade.filesAdded || []),
    ...newReShadeFiles(exeDir, known)
  ])];
  if (!hookExisted && fs.existsSync(path.join(exeDir, hook))) {
    manifest.reshade.installedByUs = true;
    manifest.reshade.file = hook;
  }
}

function wasAdded(manifest, rel) {
  const key = relKey(rel);
  return manifest.added.some((item) => relKey(item) === key);
}

function rememberAdded(manifest, rel) {
  if (!wasAdded(manifest, rel)) manifest.added.push(rel);
}

// Keep the first oldVersion forever: it is the version in the backup. Later
// installs may update newVersion, but must never turn an app-added file into a
// replacement or replace the identity of the user's original file.
function rememberReplacement(manifest, item) {
  if (wasAdded(manifest, item.rel)) return;
  const key = relKey(item.rel);
  const previous = manifest.replaced.find((row) => relKey(row.rel) === key);
  if (previous) {
    previous.newVersion = item.newVersion;
    if (item.kind && !previous.kind) previous.kind = item.kind;
    return;
  }
  manifest.replaced.push(item);
}

function fail(code, params) {
  const error = new Error(code);
  error.code = code;
  error.params = params || {};
  return error;
}

function parseVersion(text) {
  const m = String(text || '').match(/(\d+)\.(\d+)\.(\d+)/);
  return m ? m.slice(1).map(Number) : null;
}

// Positive when a is newer than b.
function compareVersions(a, b) {
  const x = parseVersion(a);
  const y = parseVersion(b);
  if (!x || !y) return 0;
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
}

// A game under Program Files needs an elevated app; find that out before
// touching anything rather than half-way through the swap.
function canWrite(dir) {
  const probe = path.join(dir, `.dlss5_write_test_${Date.now()}`);
  try {
    fs.writeFileSync(probe, 'x');
    fs.unlinkSync(probe);
    return true;
  } catch {
    return false;
  }
}

async function copyOver(src, dest) {
  await fs.promises.mkdir(path.dirname(dest), { recursive: true });
  await fs.promises.copyFile(src, dest);
}

function runSetup(setupExe, args, log) {
  return new Promise((resolve) => {
    log('runningSetup', { setup: path.basename(setupExe), args: args.slice(1).join(' ') });
    const child = spawn(setupExe, args, { windowsHide: true });
    let output = '';
    child.stdout.on('data', (d) => { output += d.toString(); });
    child.stderr.on('data', (d) => { output += d.toString(); });
    child.on('error', (err) => resolve({ code: -1, output: err.message }));
    child.on('close', (code) => resolve({ code, output: output.trim() }));
    setTimeout(() => { try { child.kill(); } catch {} }, 120000);
  });
}

function listDir(dir) {
  try {
    return new Set(fs.readdirSync(dir));
  } catch {
    return new Set();
  }
}

function newReShadeFiles(dir, known) {
  return fs.readdirSync(dir).filter((f) => !known.has(f) && /^ReShade|^reshade-shaders$/i.test(f));
}

async function backupReShadeConfig(gameDir, exeDir, manifest) {
  const ini = path.join(exeDir, 'ReShade.ini');
  const targets = [ini];
  if (fs.existsSync(ini)) {
    const text = fs.readFileSync(ini, 'utf8');
    const preset = (text.match(/^PresetPath=(.+)$/m) || [])[1];
    if (preset) targets.push(path.resolve(exeDir, preset.trim()));
  }
  for (const target of targets) {
    if (!fs.existsSync(target)) continue;
    const rel = path.relative(gameDir, target);
    if (rel.startsWith('..')) continue;
    const backupPath = originalPath(gameDir, manifest, rel);
    if (!fs.existsSync(backupPath)) await copyOver(target, backupPath);
    rememberReplacement(manifest, { rel, kind: 'config' });
  }
}

function rememberAddedDir(manifest, rel) {
  const key = relKey(rel);
  if (!manifest.addedDirs.some((item) => relKey(item) === key)) manifest.addedDirs.push(rel);
}

function rememberMissingParents(manifest, gameDir, target) {
  const missing = [];
  let current = path.dirname(target);
  const root = path.resolve(gameDir);
  while (path.resolve(current).toLowerCase() !== root.toLowerCase()) {
    const rel = path.relative(root, current);
    if (rel.startsWith('..') || path.isAbsolute(rel)) break;
    if (!fs.existsSync(current)) missing.push(rel);
    current = path.dirname(current);
  }
  for (const rel of missing.reverse()) rememberAddedDir(manifest, rel);
}

async function trackBeforeWrite(manifest, gameDir, target, meta = {}) {
  const rel = path.relative(gameDir, target);
  journal.safePath(gameDir, rel);
  if (rel.split(path.sep)[0].toLowerCase() === BACKUP_DIR.toLowerCase()) throw fail('errUnsafeTarget', { rel });
  await journal.capture(gameDir, target);
  rememberMissingParents(manifest, gameDir, target);
  if (fs.existsSync(target)) {
    if (!wasAdded(manifest, rel)) {
      const backupPath = originalPath(gameDir, manifest, rel);
      if (!fs.existsSync(backupPath)) await copyOver(target, backupPath);
      rememberReplacement(manifest, {
        rel,
        oldVersion: meta.oldVersion === undefined ? pe.getFileVersion(target) : meta.oldVersion,
        newVersion: meta.newVersion,
        kind: meta.kind
      });
    }
  } else {
    rememberAdded(manifest, rel);
  }
  return rel;
}

async function copyTracked(manifest, gameDir, src, dest, meta = {}) {
  const rel = await trackBeforeWrite(manifest, gameDir, dest, meta);
  await saveActiveManifest(gameDir, manifest);
  await copyOver(src, dest);
  return rel;
}

async function writeTracked(manifest, gameDir, dest, text, meta = {}) {
  const rel = await trackBeforeWrite(manifest, gameDir, dest, meta);
  await saveActiveManifest(gameDir, manifest);
  await fs.promises.mkdir(path.dirname(dest), { recursive: true });
  await fs.promises.writeFile(dest, text, 'utf8');
  return rel;
}

async function copyTreeTracked(manifest, gameDir, srcRoot, destRoot, log) {
  const queue = [''];
  while (queue.length) {
    const relDir = queue.shift();
    const srcDir = path.join(srcRoot, relDir);
    for (const entry of fs.readdirSync(srcDir, { withFileTypes: true })) {
      const rel = path.join(relDir, entry.name);
      if (entry.isDirectory()) queue.push(rel);
      else if (entry.isFile()) {
        const dest = path.join(destRoot, rel);
        const installedRel = await copyTracked(manifest, gameDir, path.join(srcRoot, rel), dest, { kind: 'shader' });
        log('added', { rel: installedRel, version: null });
      }
    }
  }
}

function isAddonReShade(file) {
  try {
    const binary = fs.readFileSync(file);
    const identifiesAsReShade = pe.versionMentions(file, 'ReShade') || binary.includes(Buffer.from('ReShade'));
    return identifiesAsReShade && binary.includes(Buffer.from('Searching for add-ons'));
  } catch {
    return false;
  }
}

function hookForApi(api) {
  if (api === 'opengl') return 'opengl32.dll';
  if (api === 'd3d9') return 'd3d9.dll';
  return 'dxgi.dll';
}

async function installReShadeFromHelper(options) {
  const {
    gameDir, exeDir, api, bitness, source, reshadeSetup,
    setupRunner, manifest, log
  } = options;
  const helper = source && source.feeder && source.feeder.host64;
  if (bitness !== 64 || !helper || !fs.existsSync(helper)) return null;

  const tempDir = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'dlss5-reshade-'));
  try {
    const probeExe = path.join(tempDir, 'dlss5-reshade-host64.exe');
    await copyOver(helper, probeExe);
    const runner = setupRunner || runSetup;
    const result = await runner(reshadeSetup, [probeExe, '--api', api, '--headless'], log);
    const hook = hookForApi(api);
    const extracted = path.join(tempDir, hook);
    if (!fs.existsSync(extracted) || !isAddonReShade(extracted)) {
      return { ok: false, result };
    }

    const destination = path.join(exeDir, hook);
    await trackBeforeWrite(manifest, gameDir, destination, { kind: 'reshade' });
    await copyOver(extracted, destination);
    log('reshadeXboxFallback', { file: hook });
    return {
      ok: true,
      reshade: {
        installed: true,
        file: hook,
        kind: 'proxy',
        version: pe.getFileVersion(extracted),
        addonSupport: true
      }
    };
  } finally {
    await fs.promises.rm(tempDir, { recursive: true, force: true });
  }
}

async function installReShadeAt(options) {
  const {
    gameDir, exePath, api, manifest, reshadeSetup, setupRunner,
    log, gameInstance, bitness, source
  } = options;
  const exeDir = path.dirname(exePath);
  const hook = hookForApi(api);
  const hookPath = path.join(exeDir, hook);

  const bundled = source && source.feeder && source.feeder.vulkanLayerDir
    ? path.join(source.feeder.vulkanLayerDir, `ReShade${bitness}.dll`) : null;
  if (bundled && fs.existsSync(bundled)) {
    if (pe.getBitness(bundled) !== bitness || !isAddonReShade(bundled)) throw fail('errReShadeArchitecture');
    const existed = fs.existsSync(hookPath);
    await copyTracked(manifest, gameDir, bundled, hookPath, { kind: 'reshade' });
    if (gameInstance) {
      manifest.reshade.installedByUs = !existed;
      manifest.reshade.file = hook;
    }
    await saveActiveManifest(gameDir, manifest);
    log('reshadeInstalled', { version: pe.getFileVersion(hookPath), file: hook, bitness });
    return hookPath;
  }

  if (isAddonReShade(hookPath) && (!bitness || !pe.getBitness(hookPath) || pe.getBitness(hookPath) === bitness)) {
    log('reshadeAlreadyThere', {
      version: pe.getFileVersion(hookPath), file: hook, kind: 'proxy', addonSupport: true
    });
    return hookPath;
  }

  if (!reshadeSetup || !fs.existsSync(reshadeSetup)) throw fail('errReShadeSetupMissing');

  const hookExisted = fs.existsSync(hookPath);
  const known = listDir(exeDir);
  await backupReShadeConfig(gameDir, exeDir, manifest);
  await trackBeforeWrite(manifest, gameDir, hookPath, { kind: 'reshade' });
  const ini = path.join(exeDir, 'ReShade.ini');
  if (!fs.existsSync(ini)) await trackBeforeWrite(manifest, gameDir, ini, { kind: 'config' });
  const defaultPreset = path.join(exeDir, 'ReShadePreset.ini');
  if (!fs.existsSync(defaultPreset)) await trackBeforeWrite(manifest, gameDir, defaultPreset, { kind: 'config' });

  const runner = setupRunner || runSetup;
  await saveActiveManifest(gameDir, manifest);
  let result;
  try {
    result = await runner(reshadeSetup, [exePath, '--api', api, '--headless'], log);
  } catch (error) {
    captureReShadeAttempt(manifest, exeDir, known, hook, hookExisted);
    await saveActiveManifest(gameDir, manifest);
    throw error;
  }

  if (!fs.existsSync(hookPath) || !isAddonReShade(hookPath)) {
    captureReShadeAttempt(manifest, exeDir, known, hook, hookExisted);
    await saveActiveManifest(gameDir, manifest);
    throw fail('errReShadeInstall', { exit: result && result.code, output: result && result.output });
  }

  if (gameInstance) {
    manifest.reshade.installedByUs = !hookExisted;
    manifest.reshade.file = hook;
  }

  captureReShadeAttempt(manifest, exeDir, known, hook, hookExisted);
  await saveActiveManifest(gameDir, manifest);
  log('reshadeInstalled', { version: pe.getFileVersion(hookPath), file: hook });
  return hookPath;
}

async function applyFeeder(config, log) {
  const {
    gameDir, exePath, api, source, reshadeSetup, setupRunner,
    bitness: requestedBitness, vulkanLayerTarget, registryRunner, emulator, neuralProvider = 'renodx', vrFoveation = 'off'
  } = config;

  const bitness = requestedBitness || pe.getBitness(exePath);
  const exeDir = path.dirname(exePath);

 const isNmsVulkan =
  path.basename(exePath).toLowerCase() === 'nms.exe';
  const selectedChickenAddon =
    neuralProvider === 'deep-fried-chicken'
      ? selectChickenAddon(source, { nmsVulkan: isNmsVulkan })
      : null;

  if (!canWrite(exeDir)) throw fail('errNoWriteAccess');
  if (!source.hasNeuralRendering) throw fail('errNoNeuralRuntime');

  const feederReady = source.feeder && (bitness === 32
    ? (source.feeder.ok32 ?? source.feeder.ok)
    : (source.feeder.ok64 ?? source.feeder.ok));

  if (!feederReady) {
    throw fail('errFeederSupportMissing');
  }

  if (!['dxgi', 'd3d8', 'd3d9', 'opengl', 'vulkan'].includes(api) || (api === 'd3d8' && bitness !== 32)) {
    throw fail('errFeederApiUnsupported', { api, bitness });
  }

  if (api === 'vulkan' && (!source.feeder.vulkanOk || !vulkanLayerTarget)) {
    throw fail('errVulkanSupportMissing');
  }

  if (!['renodx', 'deep-fried-chicken', 'upstream3'].includes(neuralProvider)) {
    throw fail('errNeuralProviderUnsupported');
  }

  if (neuralProvider === 'upstream3' && (!source.upstream3 || !source.upstream3.ok)) {
    throw Object.assign(fail('errNeuralProviderUnsupported'), {
      message: 'True Upstream 3x payload missing. Run BUILD-TRUE-UPSTREAM-3X.cmd first.'
    });
  }

  if (neuralProvider === 'deep-fried-chicken' && (!source.deepFriedChicken || !source.deepFriedChicken.ok)) {
    throw fail('errChickenPayloadMissing');
  }

  if (neuralProvider === 'deep-fried-chicken' && !selectedChickenAddon) {
    throw Object.assign(fail('errChickenPayloadMissing'), {
      message: isNmsVulkan
        ? 'No Man\'s Sky requires the NMS-compatible DFC v1.4 binary in the DFC payload folder (SHA-256 ' + DFC_NMS_SHA256 + ').'
        : 'Deep Fried Chicken payload missing.'
    });
  }

  const manifest = beginManifest(gameDir, exePath, api);
  manifest.route = 'feeder';
  manifest.neuralProvider = neuralProvider;
  manifest.game.bitness = bitness;
  manifest.game.emulator = emulator || null;

  const payloadByName = new Map(source.payload.map((file) => [file.name.toLowerCase(), file]));
  const neural = payloadByName.get('nvngx_dlssnr.dll');
  const dlss = payloadByName.get('nvngx_dlss.dll');
  if (!neural || !dlss) throw fail('errNoNeuralRuntime');

  let reshadeApi = api;

  if (api === 'd3d8' || api === 'd3d9') {
    const dg = source.feeder.dgVoodooDir;
    if (!dg) throw fail('errDgVoodooMissing');

    const dllName = api === 'd3d8' ? 'D3D8.dll' : 'D3D9.dll';
    const officialDll = path.join(dg, 'MS', bitness === 32 ? 'x86' : 'x64', dllName);
    const dgDll = fs.existsSync(officialDll) || bitness === 64 ? officialDll : path.join(dg, dllName);
    const dgConf = path.join(dg, 'dgVoodoo.conf');
    const dgCpl = path.join(dg, 'dgVoodooCpl.exe');

    if (![dgDll, dgConf, dgCpl].every((file) => fs.existsSync(file))) {
      throw fail('errDgVoodooMissing');
    }

    if (pe.getBitness(dgDll) && pe.getBitness(dgDll) !== bitness) {
      throw fail('errReShadeArchitecture');
    }

    for (const src of [dgDll, dgCpl]) {
      const rel = await copyTracked(
        manifest,
        gameDir,
        src,
        path.join(exeDir, path.basename(src)),
        { kind: 'dgvoodoo' }
      );
      log('added', { rel, version: pe.getFileVersion(src) });
    }

    const confPath = path.join(exeDir, 'dgVoodoo.conf');
    const baseConf = fs.existsSync(confPath)
      ? feederConfig.readText(confPath)
      : feederConfig.readText(dgConf);

    await writeTracked(
      manifest,
      gameDir,
      confPath,
      feederConfig.configureDgVoodoo(baseConf),
      { kind: 'config' }
    );

    reshadeApi = 'dxgi';
  }

  if (api === 'vulkan') {
    manifest.vulkanLayer = await vulkanLayer.register({
      sourceDir: source.feeder.vulkanLayerDir,
      targetDir: vulkanLayerTarget,
      gameDir,
      bitness,
      runner: registryRunner
    });

    await saveActiveManifest(gameDir, manifest);
    log('vulkanLayerInstalled', { global: true, manifest: manifest.vulkanLayer.manifest });
  } else {
    await installReShadeAt({
      gameDir,
      exePath,
      api: reshadeApi,
      manifest,
      reshadeSetup,
      setupRunner,
      log,
      gameInstance: true,
      bitness,
      source
    });
  }

  const addonRel = await copyTracked(
    manifest,
    gameDir,
    bitness === 32 ? source.feeder.addon32 : source.feeder.addon64,
    path.join(exeDir, bitness === 32 ? 'dlss5-feed.addon32' : 'dlss5-feed.addon64'),
    { kind: 'feeder' }
  );

  log('addonInstalled', { name: path.basename(addonRel) });

  if (bitness === 64 && source.feeder.vrPoseBridge && fs.existsSync(source.feeder.vrPoseBridge)) {
    await copyTracked(
      manifest, gameDir, source.feeder.vrPoseBridge,
      path.join(exeDir, 'vr-pose-bridge-v1.addon64'),
      { kind: 'addon' }
    );
    log('addonInstalled', { name: 'vr-pose-bridge-v1.addon64' });
  }
  if (bitness === 64 && source.feeder.vrDepthBridge && fs.existsSync(source.feeder.vrDepthBridge)) {
    await copyTracked(manifest, gameDir, source.feeder.vrDepthBridge,
      path.join(exeDir, 'vr-depth-bridge.addon64'), { kind: 'addon' });
    log('addonInstalled', { name: 'vr-depth-bridge.addon64' });
  }

  await copyTreeTracked(
    manifest,
    gameDir,
    source.feeder.shaderRoot,
    path.join(exeDir, 'reshade-shaders'),
    log
  );

  const provider = source.feeder.lumeniteRoot ? 3 : 2;

  if (source.feeder.lumeniteRoot) {
    await copyTreeTracked(
      manifest,
      gameDir,
      path.join(source.feeder.lumeniteRoot, 'Shaders'),
      path.join(exeDir, 'reshade-shaders', 'Shaders'),
      log
    );

    await copyTreeTracked(
      manifest,
      gameDir,
      path.join(source.feeder.lumeniteRoot, 'Textures'),
      path.join(exeDir, 'reshade-shaders', 'Textures'),
      log
    );

    for (const name of ['LICENSE.md', 'NOTICE']) {
      const src = path.join(source.feeder.lumeniteRoot, name);

      if (fs.existsSync(src)) {
        await copyTracked(
          manifest,
          gameDir,
          src,
          path.join(exeDir, 'reshade-shaders', 'Licenses', `LumeniteFX-${name}`),
          { kind: 'license' }
        );
      }
    }
  }

  manifest.feeder = {
    version: source.feeder.version || 'unknown',
    provider
  };

  const verifier = path.join(
    path.dirname(source.feeder.feedShader),
    '..',
    '..',
    'Verify-DLSS5Feeder.ps1'
  );

  if (fs.existsSync(verifier)) {
    await copyTracked(
      manifest,
      gameDir,
      verifier,
      path.join(exeDir, 'Verify-DLSS5Feeder.ps1'),
      { kind: 'diagnostics' }
    );
  }

  const installedShaders = [
    path.join(exeDir, 'reshade-shaders', 'Shaders', 'DLSS5_Feed.fx'),
    path.join(exeDir, 'reshade-shaders', 'Shaders', provider === 3 ? 'lumenite_Kernel.fx' : 'vort_Motion.fx'),
    path.join(exeDir, 'reshade-shaders', 'Shaders', 'ReShade.fxh'),
    path.join(exeDir, 'reshade-shaders', 'Shaders', 'ReShadeUI.fxh'),
    path.join(exeDir, 'reshade-shaders', 'Shaders', 'Includes', 'vort_Defs.fxh'),
    path.join(exeDir, 'reshade-shaders', 'Textures', 'vort_BlueNoise.png')
  ];

  if (!installedShaders.every((file) => fs.existsSync(file))) {
    throw fail('errShaderInstall');
  }

  const gameIniPath = path.join(exeDir, 'ReShade.ini');

  const vrUnrealDepth =
    /-Win(?:32|64)-Shipping\.exe$/i.test(path.basename(exePath)) &&
    (
      fs.existsSync(path.join(exeDir, 'openvr_api.dll')) ||
      fs.existsSync(path.join(gameDir, 'openvr_api.dll'))
    );

  let gameIni = feederConfig.configureGameReShade(
    feederConfig.readText(gameIniPath),
    provider,
    { vrUnrealDepth }
  );

  const xenia = emulator && emulator.key === 'xenia';

  if (bitness === 64) {
    gameIni =
      neuralProvider === 'deep-fried-chicken'
        ? configureChickenReShadeCompat(gameIni, {
            earlyLoad: !isNmsVulkan
          })
        : feederConfig.configureConsumer(gameIni, { xenia });
  }

  let preset = feederConfig.presetPath(exeDir, gameIni);
  const presetRel = path.relative(gameDir, preset);

  if (presetRel.startsWith('..') || path.isAbsolute(presetRel)) {
    gameIni = feederConfig.setIni(
      gameIni,
      'GENERAL',
      'PresetPath',
      '.\\ReShadePreset.ini'
    );

    preset = path.join(exeDir, 'ReShadePreset.ini');
  }

  await writeTracked(
    manifest,
    gameDir,
    gameIniPath,
    gameIni,
    { kind: 'config' }
  );

  await writeTracked(
    manifest,
    gameDir,
    preset,
    feederConfig.configurePreset(
      feederConfig.readText(preset),
      provider,
      { xenia }
    ),
    { kind: 'config' }
  );

  const cfgPath = path.join(exeDir, 'dlss5-feed.cfg');

  const vrPresetMap = {
    off: { vr_foveation: '0', vr_foveation_preset: '2', vr_foveation_width: '60', vr_foveation_height: '50' },
    small: { vr_foveation: '1', vr_foveation_preset: '1', vr_foveation_width: '50', vr_foveation_height: '45' },
    balanced: { vr_foveation: '1', vr_foveation_preset: '2', vr_foveation_width: '60', vr_foveation_height: '50' },
    wide: { vr_foveation: '1', vr_foveation_preset: '3', vr_foveation_width: '70', vr_foveation_height: '50' },
    large: { vr_foveation: '1', vr_foveation_preset: '4', vr_foveation_width: '75', vr_foveation_height: '60' }
  };
  const vrFeed = vrPresetMap[vrFoveation] || vrPresetMap.off;

  await writeTracked(
    manifest,
    gameDir,
    cfgPath,
    feederConfig.configureFeed(
      feederConfig.readText(cfgPath),
      {
        ...vrFeed,
        ...(neuralProvider === 'deep-fried-chicken'
          ? { warmup_rebuild: '0' }
          : neuralProvider === 'upstream3'
            ? { warmup_rebuild: '0', work_resolution: '67', work_upscale: '2' }
            : {})
      }
    ),
    { kind: 'config' }
  );

  const hostDir =
    bitness === 32
      ? path.join(exeDir, 'host64')
      : exeDir;

  const hostExe =
    bitness === 32
      ? path.join(hostDir, 'dlss5-feed-host64.exe')
      : null;

  const competingReno = [
    'renodx-dlss5.addon64',
    'renodx-dlss.addon64'
  ].map((name) => path.join(hostDir, name));

  const competingChicken =
    path.join(hostDir, 'deep-fried-chicken.addon64');

  if (
    neuralProvider === 'deep-fried-chicken' &&
    competingReno.some((file) => fs.existsSync(file))
  ) {
    throw Object.assign(fail('errNeuralProviderConflict'), {
      message:
        'Deep Fried Chicken cannot be installed while a RenoDX DLSS neural provider is present. Restore/remove RenoDX first.'
    });
  }

  const competingUpstream =
    path.join(hostDir, 'nvngx.dll.addon64');

  if (
    neuralProvider === 'renodx' &&
    (
      fs.existsSync(competingChicken) ||
      fs.existsSync(competingUpstream)
    )
  ) {
    throw Object.assign(fail('errNeuralProviderConflict'), {
      message:
        'RenoDX cannot be installed while Deep Fried Chicken is present. Restore/remove Deep Fried Chicken first.'
    });
  }

  const commonHostFiles =
    bitness === 32
      ? [
          [source.feeder.host64, hostExe, 'feeder'],
          [neural.path, path.join(hostDir, neural.name), 'runtime'],
          [dlss.path, path.join(hostDir, dlss.name), 'runtime']
        ]
      : [
          [neural.path, path.join(exeDir, neural.name), 'runtime'],
          [dlss.path, path.join(exeDir, dlss.name), 'runtime']
        ];

  const providerFiles =
    neuralProvider === 'deep-fried-chicken'
      ? [
          [
            selectedChickenAddon,
            path.join(hostDir, 'deep-fried-chicken.addon64'),
            'addon'
          ],
          [
            source.deepFriedChicken.nvngx,
            path.join(hostDir, 'deep-fried-chicken-nvngx.dll'),
            'runtime'
          ]
        ]
      : neuralProvider === 'upstream3'
        ? [
            [
              source.upstream3.addon64,
              path.join(hostDir, 'nvngx.dll.addon64'),
              'addon'
            ]
          ]
        : [
            [
              source.feeder.hostAddon,
              path.join(hostDir, 'renodx-dlss5.addon64'),
              'addon'
            ]
          ];

  for (const [src, dest, kind] of [...commonHostFiles, ...providerFiles]) {
    const rel = await copyTracked(
      manifest,
      gameDir,
      src,
      dest,
      {
        kind,
        newVersion: pe.getFileVersion(src)
      }
    );

    log(
      kind === 'addon'
        ? 'addonInstalled'
        : 'added',
      {
        rel,
        name: path.basename(dest),
        version: pe.getFileVersion(src)
      }
    );
  }

  if (neuralProvider === 'deep-fried-chicken') {
    const chickenCfg =
      path.join(hostDir, 'deep-fried-chicken.cfg');

    const existingChickenCfg =
      feederConfig.readText(chickenCfg);

    const defaultChickenCfg =
      feederConfig.readText(source.deepFriedChicken.config);

    await writeTracked(
      manifest,
      gameDir,
      chickenCfg,
      existingChickenCfg || defaultChickenCfg,
      { kind: 'config' }
    );

    manifest.deepFriedChicken = {
      version: source.deepFriedChicken.version || 'unknown',
      variant: isNmsVulkan
        ? 'nms-v1.4-normal-scan'
        : 'stock',
      addonSha256: sha256File(selectedChickenAddon)
    };
  }

  if (bitness === 32) {
    await installReShadeAt({
      gameDir,
      exePath: hostExe,
      api: 'dxgi',
      manifest,
      reshadeSetup,
      setupRunner,
      log,
      gameInstance: false,
      bitness: 64,
      source
    });

    const hostIniPath =
      path.join(hostDir, 'ReShade.ini');

    await writeTracked(
      manifest,
      gameDir,
      hostIniPath,
      neuralProvider === 'deep-fried-chicken'
        ? configureChickenReShadeCompat(
            feederConfig.readText(hostIniPath),
            { earlyLoad: true }
          )
        : feederConfig.configureHostReShade(
            feederConfig.readText(hostIniPath)
          ),
      { kind: 'config' }
    );
  }

  await enableAddonInIni(
    exeDir,
    bitness === 32
      ? 'dlss5-feed.addon32'
      : 'dlss5-feed.addon64',
    log,
    gameDir,
    manifest
  );

  await enableAddonInIni(
    hostDir,
    neuralProvider === 'deep-fried-chicken'
      ? 'deep-fried-chicken.addon64'
      : neuralProvider === 'upstream3'
        ? 'nvngx.dll.addon64'
        : 'renodx-dlss5.addon64',
    log,
    gameDir,
    manifest
  );

  if (bitness === 64 && api === 'dxgi') {
    await require('./install-stability').apply({
      core: { writeTracked, saveActiveManifest }, manifest, gameDir, exeDir, neuralProvider
    });
  }

  await saveActiveManifest(gameDir, manifest);

  log('applyDone');
  return manifest;
}

async function enableAddonInIni(
  exeDir,
  addonName,
  log,
  gameDir,
  manifest
) {
  const ini = path.join(exeDir, 'ReShade.ini');

  if (!fs.existsSync(ini)) return;

  let text = fs.readFileSync(ini, 'utf8');
  const stem = addonName.replace(/\.addon(?:32|64)?$/i, '');

  const match = text.match(/^DisabledAddons=(.*)$/m);

  if (
    match &&
    match[1].toLowerCase().includes(stem.toLowerCase())
  ) {
    const kept = match[1]
      .split(',')
      .filter(
        (name) =>
          !name.toLowerCase().includes(stem.toLowerCase())
      );

    text = text.replace(
      /^DisabledAddons=.*$/m,
      'DisabledAddons=' + kept.join(',')
    );

    await writeTracked(
      manifest,
      gameDir,
      ini,
      text,
      { kind: 'config' }
    );

    log('addonEnabledInIni');
  }
}

async function applySwap(config, onLog) {
  const log = (code, params) =>
    onLog &&
    onLog({
      code,
      params: params || {}
    });

  const bitness =
    config.bitness || pe.getBitness(config.exePath);

  if (bitness === 32 || config.route === 'feeder') {
    return applyFeeder(config, log);
  }

  const {
    gameDir,
    exePath,
    api,
    source,
    reshadeSetup,
    setupRunner,
    installReShade,
    addMissingDlss,
    upgradeReShade,
    neuralProvider = 'renodx'
  } = config;

  const exeDir = path.dirname(exePath);

  if (!canWrite(exeDir)) throw fail('errNoWriteAccess');
  if (!source.hasNeuralRendering) throw fail('errNoNeuralRuntime');

  const scan = await scanGame(gameDir);
  const manifest = beginManifest(gameDir, exePath, api);

  manifest.route = 'native';
  manifest.neuralProvider = neuralProvider;

  const setup = setupRunner || runSetup;

  const payloadByName = new Map(
    source.payload.map((f) => [
      f.name.toLowerCase(),
      f
    ])
  );

  const existing = scan.dlssFiles.filter(
    (file) =>
      /^nvngx_dlss(?:nr)?\.dll$/i.test(file.name)
  );

  for (const file of existing) {
    const replacement =
      payloadByName.get(file.name.toLowerCase());

    if (!replacement) continue;

    if (
      file.bitness !== bitness ||
      pe.getBitness(replacement.path) !== bitness
    ) {
      log('skipRuntimeArchitecture', {
        rel: file.rel
      });

      continue;
    }

    if (
      replacement.version &&
      replacement.version === file.version
    ) {
      log('skipSameVersion', {
        rel: file.rel,
        version: file.version
      });

      continue;
    }

    await copyTracked(
      manifest,
      gameDir,
      replacement.path,
      file.path,
      {
        oldVersion: file.version,
        newVersion: replacement.version
      }
    );

    log('replaced', {
      rel: file.rel,
      from: file.version,
      to: replacement.version
    });
  }

  const beside = ['nvngx_dlssnr.dll'];

  if (
    addMissingDlss &&
    !existing.some(
      (file) =>
        /^nvngx_dlss\.dll$/i.test(file.name) &&
        file.bitness === bitness
    )
  ) {
    beside.push('nvngx_dlss.dll');
  }

  for (const name of new Set(beside)) {
    const item =
      payloadByName.get(name.toLowerCase());

    if (!item) continue;

    if (pe.getBitness(item.path) !== bitness) {
      throw fail('errReShadeArchitecture');
    }

    const dest = path.join(exeDir, name);
    const rel = path.relative(gameDir, dest);

    if (fs.existsSync(dest)) {
      if (pe.getBitness(dest) !== bitness) {
        throw Object.assign(
          fail('errRuntimeArchitecture'),
          { message: rel }
        );
      }

      const current = pe.getFileVersion(dest);

      if (current === item.version) {
        log('skipSameVersion', {
          rel,
          version: current
        });

        continue;
      }

      const backupPath =
        originalPath(gameDir, manifest, rel);

      if (
        !wasAdded(manifest, rel) &&
        !fs.existsSync(backupPath)
      ) {
        await copyOver(dest, backupPath);
      }

      rememberReplacement(manifest, {
        rel,
        oldVersion: current,
        newVersion: item.version
      });

      log('replaced', {
        rel,
        from: current,
        to: item.version
      });
    } else {
      rememberAdded(manifest, rel);

      log('added', {
        rel,
        version: item.version
      });
    }

    await copyTracked(
      manifest,
      gameDir,
      item.path,
      dest,
      { newVersion: item.version }
    );
  }

  let nativeAddon = null;
  let nativeAddonName = null;

  if (neuralProvider === 'upstream3') {
    nativeAddon =
      source.upstream3 &&
      source.upstream3.addon64;

    nativeAddonName = 'nvngx.dll.addon64';

    if (
      !nativeAddon ||
      !fs.existsSync(nativeAddon)
    ) {
      throw Object.assign(
        fail('errNeuralProviderUnsupported'),
        {
          message:
            'TRUE Upstream payload missing. Build the upstream payload first, then reopen Swapper.'
        }
      );
    }
  } else if (neuralProvider === 'deep-fried-chicken') {
    if (
      !source.deepFriedChicken ||
      !source.deepFriedChicken.ok
    ) {
      throw Object.assign(
        fail('errNeuralProviderUnsupported'),
        {
          message:
            'Deep Fried Chicken payload is missing or failed SHA-256 verification.'
        }
      );
    }

    nativeAddon =
      source.deepFriedChicken.addon64;

    nativeAddonName =
      'deep-fried-chicken.addon64';
  } else {
    nativeAddon = source.addon;

    nativeAddonName =
      nativeAddon
        ? path.basename(nativeAddon)
        : 'renodx-dlss5.addon64';
  }

  if (nativeAddon) {
    const dest =
      path.join(exeDir, nativeAddonName);

    const rel =
      path.relative(gameDir, dest);

    if (fs.existsSync(dest)) {
      const backupPath =
        originalPath(gameDir, manifest, rel);

      if (
        !wasAdded(manifest, rel) &&
        !fs.existsSync(backupPath)
      ) {
        await copyOver(dest, backupPath);
      }

      rememberReplacement(manifest, {
        rel,
        oldVersion: pe.getFileVersion(dest),
        newVersion: pe.getFileVersion(nativeAddon)
      });
    } else {
      rememberAdded(manifest, rel);
    }

    await copyTracked(
      manifest,
      gameDir,
      nativeAddon,
      dest,
      { kind: 'addon' }
    );

    log('addonInstalled', {
      name: nativeAddonName
    });
  }

  if (neuralProvider === 'deep-fried-chicken') {
    const bridgeDest =
      path.join(
        exeDir,
        'deep-fried-chicken-nvngx.dll'
      );

    const bridgeRel =
      path.relative(gameDir, bridgeDest);

    if (fs.existsSync(bridgeDest)) {
      const backupPath =
        originalPath(
          gameDir,
          manifest,
          bridgeRel
        );

      if (
        !wasAdded(manifest, bridgeRel) &&
        !fs.existsSync(backupPath)
      ) {
        await copyOver(
          bridgeDest,
          backupPath
        );
      }

      rememberReplacement(manifest, {
        rel: bridgeRel,
        oldVersion:
          pe.getFileVersion(bridgeDest),
        newVersion:
          pe.getFileVersion(
            source.deepFriedChicken.nvngx
          )
      });
    } else {
      rememberAdded(
        manifest,
        bridgeRel
      );
    }

    await copyTracked(
      manifest,
      gameDir,
      source.deepFriedChicken.nvngx,
      bridgeDest,
      { kind: 'runtime' }
    );

    const chickenCfg =
      path.join(
        exeDir,
        'deep-fried-chicken.cfg'
      );

    const existingChickenCfg =
      feederConfig.readText(chickenCfg);

    const defaultChickenCfg =
      feederConfig.readText(
        source.deepFriedChicken.config
      );

    await writeTracked(
      manifest,
      gameDir,
      chickenCfg,
      existingChickenCfg ||
        defaultChickenCfg,
      { kind: 'config' }
    );

    manifest.deepFriedChicken = {
      version:
        source.deepFriedChicken.version ||
        'unknown'
    };
  }

  manifest.neuralProvider = neuralProvider;

  const before =
    inspectReShade(exeDir);

  const setupVersion =
    reshadeSetup
      ? (
          reshadeSetup.match(
            /(\d+\.\d+\.\d+)/
          ) || []
        )[1]
      : null;

  const setupIsNewer =
    before.installed &&
    compareVersions(
      setupVersion,
      before.version
    ) > 0;

  const haveSetup =
    reshadeSetup &&
    fs.existsSync(reshadeSetup);

  manifest.reshade.file =
    before.file;

  const upgradingAsi =
    upgradeReShade &&
    before.kind === 'asi' &&
    setupIsNewer;

  const upgradingProxy =
    upgradeReShade &&
    before.kind === 'proxy' &&
    setupIsNewer;

  const installingFresh =
    installReShade &&
    (
      !before.installed ||
      (
        before.kind === 'proxy' &&
        !before.addonSupport
      )
    );

  const directProxy =
    source.feeder &&
    fs.existsSync(
      path.join(
        source.feeder.vulkanLayerDir || '',
        `ReShade${bitness}.dll`
      )
    );

  if (installingFresh && directProxy) {
    await installReShadeAt({
      gameDir,
      exePath,
      api,
      bitness,
      source,
      manifest,
      reshadeSetup,
      setupRunner,
      log,
      gameInstance: true
    });
  } else if (
    !haveSetup &&
    (
      installingFresh ||
      upgradingAsi ||
      upgradingProxy
    )
  ) {
    log('reshadeSetupMissing');
  } else if (upgradingAsi) {
    const asiRel =
      path.relative(
        gameDir,
        path.join(
          exeDir,
          before.file
        )
      );

    const asiBackup =
      originalPath(
        gameDir,
        manifest,
        asiRel
      );

    if (!fs.existsSync(asiBackup)) {
      await copyOver(
        path.join(
          exeDir,
          before.file
        ),
        asiBackup
      );
    }

    await backupReShadeConfig(
      gameDir,
      exeDir,
      manifest
    );

    const known = listDir(exeDir);

    const proxyPath =
      path.join(
        exeDir,
        'dxgi.dll'
      );

    const proxyExisted =
      fs.existsSync(proxyPath);

    const result =
      await setup(
        reshadeSetup,
        [
          exePath,
          '--api',
          api,
          '--headless'
        ],
        log
      );

    manifest.reshade.filesAdded =
      newReShadeFiles(
        exeDir,
        known
      );

    if (!fs.existsSync(proxyPath)) {
      throw fail(
        'errReShadeExtract',
        {
          exit: result.code,
          output: result.output
        }
      );
    }

    await copyOver(
      proxyPath,
      path.join(
        exeDir,
        before.file
      )
    );

    if (!proxyExisted) {
      await fs.promises.unlink(
        proxyPath
      );
    }

    rememberReplacement(
      manifest,
      {
        rel: asiRel,
        oldVersion: before.version,
        newVersion: setupVersion
      }
    );

    log(
      'asiUpgraded',
      {
        file: before.file,
        from: before.version,
        to: setupVersion
      }
    );
  } else if (upgradingProxy) {
    const rel =
      path.relative(
        gameDir,
        path.join(
          exeDir,
          before.file
        )
      );

    const backupPath =
      originalPath(
        gameDir,
        manifest,
        rel
      );

    if (!fs.existsSync(backupPath)) {
      await copyOver(
        path.join(
          exeDir,
          before.file
        ),
        backupPath
      );
    }

    await backupReShadeConfig(
      gameDir,
      exeDir,
      manifest
    );

    const known = listDir(exeDir);

    const result =
      await setup(
        reshadeSetup,
        [
          exePath,
          '--api',
          api,
          '--headless'
        ],
        log
      );

    manifest.reshade.filesAdded =
      newReShadeFiles(
        exeDir,
        known
      );

    const after =
      inspectReShade(exeDir);

    if (!after.installed) {
      throw fail(
        'errReShadeUpgrade',
        {
          exit: result.code,
          output: result.output
        }
      );
    }

    rememberReplacement(
      manifest,
      {
        rel,
        oldVersion: before.version,
        newVersion: after.version
      }
    );

    log(
      'proxyUpgraded',
      {
        from: before.version,
        to: after.version
      }
    );
  } else if (installingFresh) {
    await backupReShadeConfig(
      gameDir,
      exeDir,
      manifest
    );

    const known =
      listDir(exeDir);

    const hookPath =
      path.join(
        exeDir,
        hookForApi(api)
      );

    const hookExisted =
      fs.existsSync(hookPath);

    await trackBeforeWrite(
      manifest,
      gameDir,
      hookPath,
      { kind: 'reshade' }
    );

    await saveActiveManifest(
      gameDir,
      manifest
    );

    let result;

    try {
      result = await setup(
        reshadeSetup,
        [
          exePath,
          '--api',
          api,
          '--headless'
        ],
        log
      );
    } catch (error) {
      captureReShadeAttempt(
        manifest,
        exeDir,
        known,
        path.basename(hookPath),
        hookExisted
      );

      await saveActiveManifest(
        gameDir,
        manifest
      );

      throw error;
    }

    let after =
      inspectReShade(exeDir);

    if (
      !after.installed ||
      !after.addonSupport
    ) {
      const fallback =
        await installReShadeFromHelper({
          gameDir,
          exeDir,
          api,
          bitness,
          source,
          reshadeSetup,
          setupRunner,
          manifest,
          log
        });

      if (
        fallback &&
        fallback.ok
      ) {
        after =
          fallback.reshade;
      }
    }

    if (
      after.installed &&
      after.addonSupport
    ) {
      manifest.reshade.installedByUs =
        !hookExisted;

      manifest.reshade.file =
        after.file;

      manifest.reshade.filesAdded =
        newReShadeFiles(
          exeDir,
          known
        );

      log(
        'reshadeInstalled',
        {
          version: after.version,
          file: after.file
        }
      );
    } else if (
      after.installed
    ) {
      log(
        'reshadeNoAddonSupport'
      );
    } else {
      captureReShadeAttempt(
        manifest,
        exeDir,
        known,
        path.basename(hookPath),
        hookExisted
      );

      await saveActiveManifest(
        gameDir,
        manifest
      );

      throw fail(
        'errReShadeInstall',
        {
          exit: result.code,
          output: result.output
        }
      );
    }

    captureReShadeAttempt(
      manifest,
      exeDir,
      known,
      path.basename(hookPath),
      hookExisted
    );

    await saveActiveManifest(
      gameDir,
      manifest
    );
  } else if (
    before.installed
  ) {
    log(
      'reshadeAlreadyThere',
      {
        version: before.version,
        file: before.file,
        kind: before.kind,
        addonSupport:
          before.addonSupport
      }
    );

    if (setupIsNewer) {
      log(
        'reshadeNewerAvailable',
        {
          version: setupVersion
        }
      );
    }
  }

  if (
    neuralProvider ===
    'deep-fried-chicken'
  ) {
    const nativeIniPath =
      path.join(
        exeDir,
        'ReShade.ini'
      );

    const nativeIni =
      configureChickenReShadeCompat(
        feederConfig.readText(
          nativeIniPath
        ),
        {
          earlyLoad: true
        }
      );

    await writeTracked(
      manifest,
      gameDir,
      nativeIniPath,
      nativeIni,
      { kind: 'config' }
    );
  }

  if (nativeAddon) {
    await enableAddonInIni(
      exeDir,
      nativeAddonName,
      log,
      gameDir,
      manifest
    );
  }

  await saveActiveManifest(
    gameDir,
    manifest
  );

  log('applyDone');

  return manifest;
}

async function restoreFiles(
  gameDir,
  manifest,
  onLog
) {
  const log =
    (code, params) =>
      onLog &&
      onLog({
        code,
        params: params || {}
      });

  if (
    manifest.version !== 1 ||
    !Array.isArray(
      manifest.replaced
    ) ||
    !Array.isArray(
      manifest.added
    )
  ) {
    throw fail(
      'errBackupInvalid'
    );
  }

  for (
    const item of
    manifest.replaced
  ) {
    journal.safePath(
      gameDir,
      item.rel
    );

    if (
      !fs.existsSync(
        originalPath(
          gameDir,
          manifest,
          item.rel
        )
      )
    ) {
      throw Object.assign(
        fail(
          'errBackupInvalid'
        ),
        {
          message:
            'Missing original backup: ' +
            item.rel
        }
      );
    }
  }

  for (
    const rel of [
      ...manifest.added,
      ...(manifest.addedDirs || [])
    ]
  ) {
    journal.safePath(
      gameDir,
      rel
    );
  }

  journal.safePath(
    gameDir,
    manifest.game.exe
  );

  for (
    const item of
    manifest.replaced
  ) {
    const backupPath =
      originalPath(
        gameDir,
        manifest,
        item.rel
      );

    const target =
      journal.safePath(
        gameDir,
        item.rel
      );

    if (
      fs.existsSync(
        backupPath
      )
    ) {
      await journal.capture(
        gameDir,
        target
      );

      await copyOver(
        backupPath,
        target
      );

      log(
        'restored',
        {
          rel: item.rel,
          version:
            item.oldVersion ||
            null,
          kind:
            item.kind ||
            null
        }
      );
    }
  }

  for (
    const rel of
    manifest.added
  ) {
    const target =
      journal.safePath(
        gameDir,
        rel
      );

    if (
      fs.existsSync(
        target
      )
    ) {
      await journal.capture(
        gameDir,
        target
      );

      await fs.promises.unlink(
        target
      );

      log(
        'deleted',
        { rel }
      );
    }
  }

  for (
    const rel of [
      ...(manifest.addedDirs || [])
    ].sort(
      (a, b) =>
        b.length -
        a.length
    )
  ) {
    const target =
      journal.safePath(
        gameDir,
        rel
      );

    if (
      !fs.existsSync(
        target
      )
    ) {
      continue;
    }

    try {
      await fs.promises.rmdir(
        target
      );

      log(
        'deleted',
        { rel }
      );
    } catch {}
  }

  const exeDir =
    path.dirname(
      path.join(
        gameDir,
        manifest.game.exe
      )
    );

  const leftovers = [
    ...(
      manifest.reshade
        ?.filesAdded ||
      []
    )
  ];

  if (
    manifest.reshade
      ?.installedByUs &&
    manifest.reshade
      .file
  ) {
    leftovers.push(
      manifest.reshade.file
    );
  }

  for (
    const name of
    leftovers
  ) {
    const target =
      journal.safePath(
        gameDir,
        path.relative(
          gameDir,
          path.join(
            exeDir,
            name
          )
        )
      );

    if (
      manifest.replaced.some(
        (item) =>
          relKey(item.rel) ===
          relKey(
            path.relative(
              gameDir,
              target
            )
          )
      )
    ) {
      continue;
    }

    if (
      !fs.existsSync(
        target
      )
    ) {
      continue;
    }

    try {
      if (
        fs.statSync(
          target
        ).isDirectory()
      ) {
        await fs.promises.rmdir(
          target
        );
      } else {
        await journal.capture(
          gameDir,
          target
        );

        await fs.promises.unlink(
          target
        );
      }

      log(
        'deleted',
        { rel: name }
      );
    } catch {}
  }
}

async function restore(
  gameDir,
  onLog
) {
  const log =
    (code, params) =>
      onLog &&
      onLog({
        code,
        params: params || {}
      });

  const manifestPath =
    path.join(
      backupRoot(gameDir),
      MANIFEST
    );

  if (
    !fs.existsSync(
      manifestPath
    )
  ) {
    throw fail(
      'errNoBackup'
    );
  }

  const manifest =
    JSON.parse(
      fs.readFileSync(
        manifestPath,
        'utf8'
      )
    );

  await restoreFiles(
    gameDir,
    manifest,
    onLog
  );

  if (
    manifest.vulkanLayer
  ) {
    const removed =
      await vulkanLayer.detach(
        manifest.vulkanLayer,
        gameDir
      );

    log(
      removed
        ? 'vulkanLayerRemoved'
        : 'vulkanLayerKept'
    );
  }

  await fs.promises.rename(
    manifestPath,
    manifestPath +
      `.done-${Date.now()}`
  );

  log(
    'restoreDone',
    {
      date: manifest.date,
      route:
        manifest.route,
      game:
        manifest.game,
      replaced:
        manifest.replaced.length,
      added:
        manifest.added.length
    }
  );

  return true;
}

module.exports = {
  applySwap,
  restore,
  restoreFiles,
  canWrite,
  backupRoot,
  compareVersions,
  beginManifest,
  originalPath,
  copyTracked,
  writeTracked,
  saveActiveManifest,
  enableAddonInIni,
  trackBeforeWrite
};
