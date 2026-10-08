'use strict';

const fs = require('fs');
const path = require('path');

function sectionBounds(lines, section) {
  if (section === '') {
    const end = lines.findIndex(line => /^\s*\[.+\]\s*$/.test(line));
    return { start: -1, end: end === -1 ? lines.length : end };
  }
  const header = `[${section}]`.toLowerCase();
  const start = lines.findIndex((line) => line.trim().toLowerCase() === header);
  if (start === -1) return null;
  let end = lines.length;
  for (let i = start + 1; i < lines.length; i++) {
    if (/^\s*\[.+\]\s*$/.test(lines[i])) { end = i; break; }
  }
  return { start, end };
}

function getIni(text, section, key) {
  const lines = String(text || '').replace(/^\uFEFF/, '').split(/\r?\n/);
  const bounds = sectionBounds(lines, section);
  if (!bounds) return null;
  const wanted = key.toLowerCase();
  for (let i = bounds.start + 1; i < bounds.end; i++) {
    const match = lines[i].match(/^\s*([^;#][^=]*?)\s*=\s*(.*)$/);
    if (match && match[1].trim().toLowerCase() === wanted) return match[2].trim();
  }
  return null;
}

function setIni(text, section, key, value) {
  const newline = String(text || '').includes('\r\n') ? '\r\n' : '\n';
  const lines = String(text || '').replace(/^\uFEFF/, '').split(/\r?\n/);
  let bounds = sectionBounds(lines, section);
  if (!bounds) {
    if (lines.length && lines[lines.length - 1] !== '') lines.push('');
    lines.push(`[${section}]`, `${key}=${value}`);
  } else {
    const wanted = key.toLowerCase();
    let changed = false;
    for (let i = bounds.start + 1; i < bounds.end; i++) {
      const match = lines[i].match(/^\s*([^;#][^=]*?)\s*=/);
      if (match && match[1].trim().toLowerCase() === wanted) {
        const spacing = (lines[i].match(/^(\s*[^=]+?\s*=\s*)/) || [])[1] || `${key}=`;
        lines[i] = spacing + value;
        changed = true;
        break;
      }
    }
    if (!changed) lines.splice(bounds.end, 0, `${key}=${value}`);
  }
  return lines.join(newline).replace(/(?:\r?\n)*$/, newline);
}

function mergeNamedList(value, required, nameOf = (item) => item) {
  const current = String(value || '').split(',').map((item) => item.trim()).filter(Boolean);
  const requiredNames = new Set(required.map((item) => nameOf(item).toLowerCase()));
  const kept = current.filter((item) => !requiredNames.has(nameOf(item).toLowerCase()));
  return [...required, ...kept].join(',');
}

// ReShade Setup 6.8 may seed recursive paths as `Shaders\**\**`. Windows
// rejects the unresolved wildcard directory with ERROR_INVALID_NAME (123), so
// the runtime sees no effects even though every .fx file is present. Always
// put our canonical recursive path first and collapse/remove malformed copies,
// while retaining unrelated custom search locations.
function configureSearchPath(value, required) {
  const canonical = (item) => String(item || '')
    .trim()
    .replace(/\//g, '\\')
    .replace(/(?:\\\*\*){2,}$/g, '\\**');
  const base = (item) => canonical(item).replace(/\\\*\*$/g, '').replace(/[\\]+$/g, '').toLowerCase();
  const requiredBase = base(required);
  const kept = String(value || '').split(',').map(canonical).filter(Boolean)
    .filter((item) => base(item) !== requiredBase);
  return [required, ...kept].join(',');
}

function configureGameReShade(text, provider = 2) {
  let out = String(text || '');
  out = setIni(out, 'GENERAL', 'EffectSearchPaths', configureSearchPath(
    getIni(out, 'GENERAL', 'EffectSearchPaths'), '.\\reshade-shaders\\Shaders\\**'
  ));
  out = setIni(out, 'GENERAL', 'TextureSearchPaths', configureSearchPath(
    getIni(out, 'GENERAL', 'TextureSearchPaths'), '.\\reshade-shaders\\Textures\\**'
  ));
  out = setIni(out, 'GENERAL', 'PresetPath', getIni(out, 'GENERAL', 'PresetPath') || '.\\ReShadePreset.ini');
  out = setIni(out, 'GENERAL', 'StartupPresetPath', '');
  out = setIni(out, 'GENERAL', 'NoReloadOnInit', '0');
  const definitions = mergeNamedList(
    getIni(out, 'GENERAL', 'PreprocessorDefinitions'),
    [`DLSS5_MV_PROVIDER=${provider}`],
    (item) => item.split('=')[0].trim()
  );
  out = setIni(out, 'GENERAL', 'PreprocessorDefinitions', definitions);
  out = setIni(out, 'ADDON', 'AddonPath', '.\\');
  return out;
}

function configureHostReShade(text) {
  return configureConsumer(setIni(String(text || ''), 'ADDON', 'AddonPath', '.\\'), { host: true });
}

function configureConsumer(text, { host = false, xenia = false } = {}) {
  let out = String(text || '');
  for (const [key, value] of Object.entries({ EnableHooks: '2', NeuralUplift: '1', NREnableUpscaling: '0' })) {
    out = setIni(out, 'RenoDX.DLSS5', key, value);
  }
  if (host) out = setIni(out, 'RenoDX.DLSS5', 'NRToggleKey', '0');
  if (getIni(out, 'RenoDX.DLSS5', 'NRStyle') === '2') out = setIni(out, 'RenoDX.DLSS5', 'NRStyle', '0');
  if (xenia) {
    out = setIni(out, 'RenoDX.DLSS5', 'NRAutoMask', '1');
    out = setIni(out, 'RenoDX.DLSS5', 'NRUICorrection', '1');
  }
  return out;
}

function configurePreset(text, provider = 2, { xenia = false } = {}) {
  let out = String(text || '');
  const providerTechnique = provider === 3
    ? 'Lumenite_Kernel@lumenite_Kernel.fx'
    : 'vort_MotionEffects@vort_Motion.fx';
  const required = [providerTechnique, 'DLSS5_Feed@DLSS5_Feed.fx'];
  const techniqueName = (item) => item.split('@')[0].trim();
  const feederTechniques = new Set([
    'Lumenite_Kernel', 'Lumenite_QuantMotion', 'vort_MotionEffects', 'DLSS5_Feed', 'Launchpad'
  ].map((name) => name.toLowerCase()));
  for (const key of ['Techniques', 'TechniqueSorting']) {
    const current = getIni(out, '', key);
    // ReShade's preset keys live before any section. Handle that root area
    // directly, while preserving every unrelated setting and technique.
    const kept = String(current || '').split(',').map((item) => item.trim()).filter(Boolean)
      .filter((item) => !feederTechniques.has(techniqueName(item).toLowerCase()));
    const next = [...required, ...kept].join(',');
    out = setIni(out, '', key, next);
  }
  const definitions = mergeNamedList(
    getIni(out, '', 'PreprocessorDefinitions'),
    [`DLSS5_MV_PROVIDER=${provider}`],
    (item) => item.split('=')[0].trim()
  );
  out = setIni(out, '', 'PreprocessorDefinitions', definitions);
  // Per-effect definitions override the preset/global list in ReShade.
  // Repair the stale provider=0 reported in SWTOR at every applicable level.
  out = setIni(out, 'DLSS5_Feed.fx', 'PreprocessorDefinitions', mergeNamedList(
    getIni(out, 'DLSS5_Feed.fx', 'PreprocessorDefinitions'),
    [`DLSS5_MV_PROVIDER=${provider}`], item => item.split('=')[0].trim()
  ));
  if (xenia) {
    for (const [key, value] of Object.entries({ GEOM_ENABLE: '0', MV_VALIDATE: '1', VALIDATE_STATIC: '1', VALIDATE_LUMA: '1', VALIDATE_DEPTH: '1', VALIDATE_MV: '1', MASK_STRENGTH: '1.0' })) {
      out = setIni(out, 'DLSS5_Feed.fx', key, value);
    }
  }
  return out.replace(/\r?\n/g, '\r\n').replace(/(?:\r\n)*$/, '\r\n');
}

const FEED_DEFAULTS = {
  enabled: '1', mode: '2', hdr: '-1', depth_inverted: '-1', flags: '-1',
  reset_every: '0', warmup_rebuild: '180', rebuild: '0', log_frames: '3',
  create_delay: '60', preset: '0', work_resolution: '100',
  mv_scale_x: '1.000', mv_scale_y: '1.000', host_window: '0', async_home: '1',
  vr_foveation: '0', vr_foveation_preset: '2', vr_foveation_width: '60', vr_foveation_height: '50'
};

function configureFeed(text, overrides = {}) {
  const values = { ...FEED_DEFAULTS, ...overrides };
  const lines = String(text || '').split(/\r?\n/);
  const seen = new Set();
  const out = lines.map((line) => {
    const match = line.match(/^\s*([^#;=]+?)\s*=\s*(.*)$/);
    if (!match) return line;
    const key = match[1].trim().toLowerCase();
    if (!(key in values)) return line;
    seen.add(key);
    return Object.prototype.hasOwnProperty.call(overrides, key) ? `${key}=${values[key]}` : `${key}=${match[2].trim()}`;
  }).filter((line, index, all) => !(line === '' && index === all.length - 1));
  for (const [key, value] of Object.entries(values)) {
    if (!seen.has(key)) out.push(`${key}=${value}`);
  }
  return out.join('\r\n') + '\r\n';
}

function configureDgVoodoo(text) {
  let out = String(text || '');
  out = setIni(out, 'General', 'OutputAPI', 'd3d11_fl11_0');
  out = setIni(out, 'General', 'CaptureMouse', 'false');
  out = setIni(out, 'DirectX', 'DisableAndPassThru', 'false');
  out = setIni(out, 'DirectX', 'VideoCard', 'internal3D');
  out = setIni(out, 'DirectX', 'VRAM', '1024');
  out = setIni(out, 'DirectX', 'dgVoodooWatermark', 'false');
  return out;
}

function presetPath(exeDir, reshadeIniText) {
  const value = getIni(reshadeIniText, 'GENERAL', 'PresetPath') || '.\\ReShadePreset.ini';
  return path.resolve(exeDir, value.replace(/^\.\\/, ''));
}

function readText(file) {
  try {
    const bytes = fs.readFileSync(file);
    return bytes[0] === 0xff && bytes[1] === 0xfe ? bytes.subarray(2).toString('utf16le') : bytes.toString('utf8').replace(/^\uFEFF/, '');
  } catch { return ''; }
}

module.exports = {
  getIni, setIni, configureGameReShade, configureHostReShade,
  configurePreset, configureFeed, configureDgVoodoo, presetPath, readText,
  configureSearchPath, configureConsumer
};

// ---- DFC compatibility export v0.7.5 --------------------------------------
// Added by DLSS5 Swapper DFC Export Fix. Kept self-contained so callers can
// use feederConfig.configureChickenReShade() regardless of older module API.
if (typeof module !== 'undefined' && module.exports &&
    typeof module.exports.configureChickenReShade !== 'function') {
  module.exports.configureChickenReShade = function configureChickenReShade(text, options = {}) {
    const earlyLoad = options.earlyLoad !== false;
    let src = String(text || '').replace(/\r\n/g, '\n');

    const esc = (s) => String(s).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

    function ensureSection(section) {
      const re = new RegExp('^\\[' + esc(section) + '\\]\\s*$', 'mi');
      if (!re.test(src)) {
        if (src.length && !src.endsWith('\n')) src += '\n';
        src += '[' + section + ']\n';
      }
    }

    function getValue(section, key) {
      const re = new RegExp(
        '^\\[' + esc(section) + '\\]\\s*$([\\s\\S]*?)(?=^\\[|\\s*$)',
        'mi'
      );
      const m = src.match(re);
      if (!m) return '';
      const km = m[1].match(new RegExp('^\\s*' + esc(key) + '\\s*=\\s*(.*?)\\s*$', 'mi'));
      return km ? km[1].trim() : '';
    }

    function setValue(section, key, value) {
      ensureSection(section);
      const re = new RegExp(
        '(^\\[' + esc(section) + '\\]\\s*$)([\\s\\S]*?)(?=^\\[|\\s*$)',
        'mi'
      );
      src = src.replace(re, function (_all, header, body) {
        const kre = new RegExp('(^\\s*' + esc(key) + '\\s*=\\s*).*?$', 'mi');
        if (kre.test(body)) {
          body = body.replace(kre, '$1' + value);
        } else {
          if (body.length && !body.endsWith('\n')) body += '\n';
          body += key + '=' + value + '\n';
        }
        return header + '\n' + body;
      });
    }

    setValue('ADDON', 'AddonPath', '.\\');

    if (earlyLoad) {
      const values = getValue('ADDON', 'LoadFromDllMain')
        .split(',')
        .map(v => v.trim())
        .filter(Boolean);
      if (!values.some(v => v.toLowerCase() === 'deep-fried-chicken.addon64')) {
        values.push('deep-fried-chicken.addon64');
      }
      setValue('ADDON', 'LoadFromDllMain', values.join(','));
    }

    return src.replace(/\n/g, '\r\n');
  };
}
// ---- end DFC compatibility export -----------------------------------------