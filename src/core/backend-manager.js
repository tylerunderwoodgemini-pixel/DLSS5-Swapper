'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const journal = require('./file-journal');
const core = require('./apply');
const ini = require('./feeder-config');
const optiscaler = require('./optiscaler');
const compatibility = require('./compatibility');


// No Man's Sky needs provider-specific payload variants for both DFC and
// TRUE Upstream. Keep the selection in backend-manager so the final install
// path cannot silently fall back to the stock provider DLL.
const NMS_DFC_SHA256 = '19c3f85b3d7a05648d4f2d1af600aa9f323176e2c2aa717c030faae7a724d5a9';

function sha256File(file) {
  try {
    return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toLowerCase();
  } catch {
    return null;
  }
}

function prepareNmsDfcConfig(config) {
  const provider = config && config.neuralProvider ? config.neuralProvider : 'renodx';
  if (provider !== 'deep-fried-chicken') return config;
  if (!config || !config.exePath) return config;

  const isNms = path.basename(config.exePath).toLowerCase() === 'nms.exe';
  if (!isNms) return config;

  const chicken = config.source && config.source.deepFriedChicken;
  const configured = chicken && chicken.addon64;

  if (!configured) {
    throw Object.assign(new Error('NMS DFC payload is not configured.'), {
      code: 'errChickenPayloadMissing'
    });
  }

  const nmsAddon = path.join(path.dirname(configured), 'deep-fried-chicken-nms.addon64');
  if (!fs.existsSync(nmsAddon)) {
    throw Object.assign(new Error(`NMS DFC payload missing: ${nmsAddon}`), {
      code: 'errChickenPayloadMissing'
    });
  }

  const hash = sha256File(nmsAddon);
  if (hash !== NMS_DFC_SHA256) {
    throw Object.assign(new Error(
      `NMS DFC payload has the wrong SHA-256. Expected ${NMS_DFC_SHA256}, found ${hash || 'unreadable'}.`
    ), { code: 'errChickenPayloadMissing' });
  }

  return {
    ...config,
    source: {
      ...config.source,
      deepFriedChicken: {
        ...chicken,
        addon64: nmsAddon
      }
    }
  };
}

async function enforceNmsDfcNormalScan(config, manifest) {
  const provider = config && config.neuralProvider ? config.neuralProvider : 'renodx';
  const isNms = config && config.exePath && path.basename(config.exePath).toLowerCase() === 'nms.exe';
  if (!isNms || provider !== 'deep-fried-chicken') return manifest;

  const reshadeIni = path.join(path.dirname(config.exePath), 'ReShade.ini');
  if (!fs.existsSync(reshadeIni)) return manifest;

  let text = ini.readText(reshadeIni);
  const current = String(ini.getIni(text, 'ADDON', 'LoadFromDllMain') || '')
    .split(',')
    .map((x) => x.trim())
    .filter(Boolean)
    .filter((x) => x.toLowerCase() !== 'deep-fried-chicken.addon64');

  text = ini.setIni(text, 'ADDON', 'LoadFromDllMain', current.join(','));
  await core.writeTracked(manifest, config.gameDir, reshadeIni, text, { kind: 'config' });
  await core.saveActiveManifest(config.gameDir, manifest);
  return manifest;
}

function readManifest(gameDir) {
  const file = path.join(core.backupRoot(gameDir), 'manifest.json');
  if (!fs.existsSync(file)) return null;
  journal.safePath(gameDir, path.relative(gameDir, file));
  const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
  if (manifest.version !== 1) throw Object.assign(new Error('errBackupInvalid'), { code: 'errBackupInvalid' });
  return manifest;
}

function profileFile(gameDir, exePath, api, route, neuralProvider = 'renodx') {
  if (!['native', 'feeder', 'optiscaler'].includes(route)) throw new Error('Invalid route');
  const provider = (route === 'feeder' || route === 'native') ? neuralProvider : 'default';
  const id = crypto.createHash('sha256').update(`${path.relative(gameDir, exePath).toLowerCase()}|${api}`).digest('hex').slice(0, 24);
  return journal.safePath(gameDir, `_DLSS5_Backup/.profiles/${id}-${route}-${provider}.json`);
}

function configPaths(gameDir, exePath, route, neuralProvider = 'renodx', bitness = 64) {
  const dir = path.dirname(exePath);
  if (route === 'optiscaler') return [path.join(dir, 'OptiScaler.ini')];

  const reshade = path.join(dir, 'ReShade.ini');
  const preset = ini.presetPath(dir, ini.readText(reshade));
  const files = [reshade, path.join(dir, 'dlss5-feed.cfg'), path.join(dir, 'host64', 'ReShade.ini')];

  if ((route === 'feeder' || route === 'native') && neuralProvider === 'deep-fried-chicken') {
    files.push(path.join(bitness === 32 ? path.join(dir, 'host64') : dir, 'deep-fried-chicken.cfg'));
  }

  const rel = path.relative(gameDir, preset);
  if (rel && !rel.startsWith('..') && !path.isAbsolute(rel)) files.push(preset);

  return files;
}

async function saveProfile(gameDir, old) {
  const exe = journal.safePath(gameDir, old.game.exe);
  const route = old.route || (old.game.bitness === 32 ? 'feeder' : 'native');
  const neuralProvider = old.neuralProvider || 'renodx';
  const files = {};

  for (const file of configPaths(gameDir, exe, route, neuralProvider, old.game.bitness || 64)) {
    const rel = path.relative(gameDir, file);
    journal.safePath(gameDir, rel);
    if (fs.existsSync(file) && fs.statSync(file).size < 4 * 1024 * 1024) {
      files[rel] = ini.readText(file);
    }
  }

  const file = profileFile(gameDir, exe, old.game.api, route, neuralProvider);
  await journal.capture(gameDir, file);
  await journal.atomicJson(file, { version: 1, files });
}

function loadProfile(config) {
  const file = profileFile(config.gameDir, config.exePath, config.api, config.route, config.neuralProvider || 'renodx');
  if (!fs.existsSync(file)) return {};

  const profile = JSON.parse(fs.readFileSync(file, 'utf8'));
  if (profile.version !== 1 || !profile.files || typeof profile.files !== 'object') {
    throw new Error('Invalid backend profile');
  }

  for (const [rel, text] of Object.entries(profile.files)) {
    journal.safePath(config.gameDir, rel);
    if (!/\.(ini|cfg)$/i.test(rel) || typeof text !== 'string' || text.length > 4 * 1024 * 1024 || rel.toLowerCase().includes('_dlss5_backup')) {
      throw new Error('Invalid backend profile');
    }
  }

  return profile.files;
}

// For No Man's Sky, TRUE Upstream on the Feeder/Vulkan route needs the
// special eval-clock patched build. Keep the game-side installed filename
// normal ("nvngx.dll.addon64"), but swap the payload source to the special
// NMS build whenever:
//   - provider = upstream3
//   - executable = NMS.exe
//   - payload file "nvngx.dll.nms.addon64" exists beside the normal build
function applySpecialNmsUpstream(config) {
  const provider = config && config.neuralProvider ? config.neuralProvider : 'renodx';
  if (provider !== 'upstream3') return config;
  if (!config || !config.exePath) return config;

  const exeName = path.basename(config.exePath).toLowerCase();
  if (exeName !== 'nms.exe') return config;

  const upstream = config.source && config.source.upstream3;
  if (!upstream || !upstream.addon64) return config;

  const specialAddon = path.join(path.dirname(upstream.addon64), 'nvngx.dll.nms.addon64');
  if (!fs.existsSync(specialAddon)) return config;

  return {
    ...config,
    source: {
      ...config.source,
      upstream3: {
        ...upstream,
        addon64: specialAddon
      }
    }
  };
}

async function install(config, log = () => {}) {
  config = applySpecialNmsUpstream(config);
  config = prepareNmsDfcConfig(config);

  compatibility.assertSafeTarget(config.gameDir, config.exePath);
  compatibility.assertAntiCheatConsent(config.gameDir, config.exePath, config.antiCheatAcknowledged);

  const old = readManifest(config.gameDir);
  const oldProvider = old && (old.neuralProvider || 'renodx');
  const newProvider = config.neuralProvider || 'renodx';

  const providerChanged = Boolean(
    old &&
    config.route === old.route &&
    (config.route === 'feeder' || config.route === 'native') &&
    oldProvider !== newProvider
  );

  const structuralChanged = Boolean(
    old &&
    (
      old.route !== config.route ||
      old.game.api !== config.api ||
      old.game.exe.toLowerCase() !== path.relative(config.gameDir, config.exePath).toLowerCase()
    )
  );

  const changed = structuralChanged || providerChanged;

  // Global Vulkan registration has shared ownership. A provider-only Feeder
  // switch does not change that registration, so allow RenoDX <-> Chicken /
  // TRUE Upstream without treating it as a structural Vulkan route switch.
  if (structuralChanged && (old.game.api === 'vulkan' || config.api === 'vulkan')) {
    throw Object.assign(new Error('errBackendVulkanSwitch'), { code: 'errBackendVulkanSwitch' });
  }

  // ReShade's global Vulkan registration is not a game-local transaction.
  // Keep its existing recoverable partial manifest on failure instead of
  // rolling that manifest away while leaving a shared layer registered.
  if (config.api === 'vulkan' && config.route === 'feeder') {
    compatibility.assertLoaderCompatible(config, providerChanged ? null : old);

    if (providerChanged) {
      log({
        code: 'backendSwitching',
        params: {
          from: `${old.route}/${oldProvider}`,
          to: `${config.route}/${newProvider}`
        }
      });

      await saveProfile(config.gameDir, old);
      await core.restoreFiles(config.gameDir, old, log);
    }

    const profile = (!old || providerChanged) ? loadProfile(config) : {};

    if (Object.keys(profile).length) {
      const manifest = core.beginManifest(config.gameDir, config.exePath, config.api);
      manifest.route = config.route;

      for (const [rel, text] of Object.entries(profile)) {
        await core.writeTracked(
          manifest,
          config.gameDir,
          journal.safePath(config.gameDir, rel),
          text,
          { kind: 'config' }
        );
      }
    }

    const manifest = await core.applySwap(config, log);
    return enforceNmsDfcNormalScan(config, manifest);
  }

  return journal.transaction(config.gameDir, async () => {
    if (!old && config.route !== 'optiscaler') {
      // Native ReShade may already have an untouched custom preset. Capture
      // its original bytes before the first managed session, while saving
      // subsequent user tuning separately for round-trip backend switches.
      const initial = core.beginManifest(config.gameDir, config.exePath, config.api);

      for (const file of configPaths(config.gameDir, config.exePath, config.route, newProvider, config.bitness || 64)) {
        if (fs.existsSync(file)) {
          await core.trackBeforeWrite(initial, config.gameDir, file, { kind: 'config' });
        }
      }

      await core.saveActiveManifest(config.gameDir, initial);
    }

    if (changed) {
      log({
        code: 'backendSwitching',
        params: {
          from: (old.route === 'feeder' || old.route === 'native') ? `${old.route}/${oldProvider}` : old.route,
          to: (config.route === 'feeder' || config.route === 'native') ? `${config.route}/${newProvider}` : config.route
        }
      });

      await saveProfile(config.gameDir, old);
      await core.restoreFiles(config.gameDir, old, log);
    }

    if (config.route !== 'optiscaler') {
      compatibility.assertLoaderCompatible(config, changed ? null : old);
    }

    const profile = changed || !old ? loadProfile(config) : {};

    if (config.route !== 'optiscaler' && Object.keys(profile).length) {
      const manifest = core.beginManifest(config.gameDir, config.exePath, config.api);

      for (const [rel, text] of Object.entries(profile)) {
        await core.writeTracked(
          manifest,
          config.gameDir,
          journal.safePath(config.gameDir, rel),
          text,
          { kind: 'config' }
        );
      }
    }

    let manifest;

    if (config.route === 'optiscaler') {
      manifest = await optiscaler.install({ ...config, profile }, log);
    } else {
      manifest = await core.applySwap(config, log);
      manifest = await enforceNmsDfcNormalScan(config, manifest);
    }

    for (const companion of config.route === 'native' ? (config.companions || []) : []) {
      const dest = path.join(path.dirname(config.exePath), path.basename(companion));

      await core.copyTracked(
        manifest,
        config.gameDir,
        companion,
        dest,
        { kind: 'addon' }
      );

      await core.enableAddonInIni(
        path.dirname(config.exePath),
        path.basename(companion),
        (code, params) => log({ code, params }),
        config.gameDir,
        manifest
      );
    }

    if (config.route === 'optiscaler' && old && old.route !== 'optiscaler') {
      manifest.previousReShadeRoute = old.route;
    }

    await core.saveActiveManifest(config.gameDir, manifest);
    return manifest;
  });
}

async function restore(gameDir, log = () => {}) {
  const recovered = await journal.recover(gameDir);
  if (recovered) log({ code: 'backendRecovered', params: {} });

  const old = readManifest(gameDir);
  if (!old) return recovered;

  // Keep the last tuning even when the user chooses a complete uninstall.
  try {
    await saveProfile(gameDir, old);
  } catch (error) {
    log({ code: 'restoreProfileWarning', params: { error: error.message } });
  }

  await core.restore(gameDir, log);
  return true;
}

module.exports = { install, restore, readManifest, saveProfile, loadProfile };