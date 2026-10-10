# CreatorBase fork: additions, alterations and upstream comparison

Audit date: **October 8, 2026**. Runtime/application audited from fork commit **030d43e**, portable **2.2.3-vr.2**. Original base: upstream **v2.2.0**. Current upstream comparison: **v2.2.9**, commit **9fb0b7c**. This documents the shipped fork, not every superseded experiment as an active feature.

[Usage guide](USER_GUIDE.md) · [Machine-readable file audit](FORK_AUDIT.json) · [Original project](https://github.com/rakanki911/DLSS5-Swapper) · [Upstream comparison snapshot](https://github.com/rakanki911/DLSS5-Swapper/tree/9fb0b7c)

## What this fork adds

The main additions are **Deep Fried Chicken integration**, **TRUE Neural Upstream with chained pre-SR passes**, and the **modified Feeder/ReShade/native helper pipeline for VR and UEVR**. VR compatibility is one part of the fork, not its entire purpose. Component authors retain credit for their neural engines and controls; this fork integrates, configures and, where described, patches them.

### 1. Three selectable neural providers

- Added **Neural provider** to the game detail panel: **RenoDX DLSS 5**, **TRUE Upstream (pre-DLSS SR)** and **Deep Fried Chicken**. The selector works with native DLSS and Feeder installation routes.
- Records the actual provider in the game manifest, scanner and **Installed backend** display; restores the selected provider when reopening the detail panel.
- Provider changes trigger **Apply backend change**, restore the previously managed provider, and install the new one. They are not merely a renamed UI choice with RenoDX silently left underneath.
- Provider-specific configuration profiles keep separate tuning for native/Feeder, executable, API and provider. DFC configuration is saved/restored in the appropriate game or `host64` directory.
- Added provider availability checks and clear missing-payload failures. Native and Feeder paths use the selected provider's actual binary.
- Feeder installs the 64-bit provider into `host64` for 32-bit games. This placement is implemented; it does not establish that every provider works on every 32-bit transport.
- Added conflict checks for DFC versus RenoDX and its NGX companion. Provider-only switches on Vulkan avoid treating the switch as a structural rendering-layer migration; structural Vulkan/backend/API changes still require restore.

Evidence: `src/renderer/renderer.js`, `src/core/scan.js`, `src/core/apply.js`, `src/core/backend-manager.js`, `main.js`.

### 2. Deep Fried Chicken integration

- Bundles and identifies **DFC 1.4.8-alpha**, its `deep-fried-chicken-nvngx.dll` companion and configuration; checks the primary add-on/companion against pinned SHA-256 values.
- Installs DFC on the native DLSS route or as Feeder's neural consumer, with the necessary NR/SR runtime placement. Preserves an existing DFC artistic config rather than always replacing it with the package default.
- Adds early-loading configuration support, including the compatibility export used by older installer call sites, while retaining other early-load entries. No Man's Sky uses a separate normal-scan exception below.
- Adds the DFC archive validation/import/cache module. This module is present in source; the current GUI uses the bundled payload rather than offering a separate archive-import screen.
- Makes DFC's own dedicated ReShade tab available: live **Enabled**, startup **Arm**, **Passes 1–30**, per-pass preset/style, intensity, tone/structure/skin strength, automatic mask, UI correction, depth convention and motion scales.
- Includes DFC's native tone/color restoration, shared paper-white/HDR/color controls, Texture Boost, Clean Fry, refresh/reset controls and per-game preset export/load. These are supplied DFC capabilities, not claimed as engines authored by the Swapper fork.
- Ships a conservative one-pass appearance configuration: Default preset/style; intensity, local tone, local structure and skin structure at **1.0**; Texture Boost and Clean Fry off. These differ from the 2.0 strengths described in older standalone DFC docs. Preserved game configs may have other artistic values.
- Installs **DFC Universal Lifecycle v20** on the eligible path, retires older numbered helpers, and re-enables the helper if an old INI disabled it. Binary status strings describe repeated teardown/re-entry recovery, a soft master gate and recovery re-arming. The helper's complete modified source is not present in this checkout; this is not a source-level proof of every recovery case.
- The package also contains the native-DLSS DX11 bridge and older lifecycle binaries. Their presence is recorded in the file audit; the current installer selects lifecycle v20 and does not automatically copy the standalone native-DLSS DX11 bridge in `apply.js`.

### 3. TRUE Neural Upstream: pre-SR chained neural passes

- Bundles a customized `nvngx.dll.addon64` under `payload/upstream3`; the scanner's historical version label is `0.3.0-3eval`.
- Native topology: **game render-resolution color/guides → chained NR passes → original game DLSS SR → output**. Feeder topology: **Feeder work-resolution color/guides → chained NR passes → Feeder SR → native output**.
- The latest preserved patch record adds **1–4 chained passes**, default **3**, with alternating output resources and resource barriers so the next pass consumes the preceding result. This goes beyond the earliest experiment that simply repeated evaluation.
- The packaged binary contains **TRUE chained multipass**, **Number of NR passes**, per-pass controls and `MultipassCount`. The GUI hint saying “three evaluations” describes the default and is stale as a statement of the configurable limit.
- Per-pass controls include **Render preset hint**, **NR Style**, **Intensity**, **Local tone**, **Local structure**, **Skin structure**, **Automatic mask**, and **Copy Pass 1 to every pass**. Config keys live in ReShade's `NRPreUpscale` section.
- Preserved patch records describe inZOI/Feeder fixes: reuse the active NGX core, use actual color-resource dimensions, prebind the NR output, and use ping-pong/barrier handling between evaluations. These records and matching binary markers are evidence of the modifications, not a fresh inZOI compatibility test.
- Feeder installation explicitly applies **work_resolution=67**, **work_upscale=2** and **warmup_rebuild=0** for this provider, placing the neural work before SR.
- Missing TRUE Upstream payload stops installation; it is not replaced with RenoDX or the older AIO/present-stage experiment.
- Includes a separate No Man's Sky upstream binary described below. The complete transformed Neural Upstream source/build environment is not tracked in this checkout; the currently published add-on is a supplied customized binary.

### 4. No Man's Sky provider variants

- **DFC:** selects `deep-fried-chicken-nms.addon64`, verifies its specific pinned hash, and installs it under the ordinary game-side DFC filename. Metadata identifies the special normal-scan variant; it is distinct from the normal 1.4.8-alpha binary.
- Removes DFC from `LoadFromDllMain` for the NMS normal-scan path while keeping unrelated entries. Prevents the ordinary early-load behavior from silently returning during installation.
- **TRUE Upstream:** selects `nvngx.dll.nms.addon64` for `NMS.exe` when available, while retaining the ordinary installed filename. The preserved work describes an evaluation-clock variant.
- Adds NMS payload-path/existence/hash diagnostics to the installer and records the selected DFC binary in the manifest.
- Archived NMS Vulkan True-SR, Generic Depth and synchronous-copy experiments exist. The current installer does **not** select the separately bundled `dlss5-feed-vulkan-sr-v10.addon64` by filename. Those experiments are not advertised as a validated automatic Vulkan VR path.

### 5. Game detection, selection and launch

- Expands per-executable API choices from direct imports, renderer-specific sibling modules, curated engine knowledge and filename evidence.
- Restricts generic engine/string sweeps so UnityPlayer/GameAssembly do not manufacture a list of every backend compiled into an engine. Unity's no-evidence Windows fallback is DX11; other string detection remains a single fallback.
- Adds provider metadata to scans and bumps scan rules so stale results can be refreshed.
- Adds **Launch Game** and its IPC/preload support to start the selected actual executable. A game's external launcher may still be needed.
- Shows provider-specific installed-backend text, VR mode/foveation choices and their hints in the game detail panel. Clears dependent selections when the executable changes.
- Adds payload-location and DFC hash diagnostics; prefers the valid payload beside `main.js`, then Electron's resource payload.

These alter the inherited library/install flow. Game artwork, store discovery, general search/filtering, languages, optional custom add-ons, backups and History were already present in the v2.2.0 base and are not claimed as new CreatorBase features.

### 6. Automatic VR setup and modified ReShade

- Added **VR mode**: Auto detect, Off, Force OpenXR and Force OpenVR. Detection examines VR libraries/import markers and UEVR profile presence, preferring OpenXR for hybrids.
- Bundles the user's modified **ReShade 6.8.0.2 V19** OpenXR early-load/reuse build and installer, rather than substituting the stock upstream ReShade build on the repaired path.
- Uses **OpenXR Router v2**: reuses a ReShade owner already loaded in the process, can load the verified local ReShade proxy during negotiation when XR starts early, and otherwise uses the shared fallback. Negotiation work is kept outside router `DllMain`.
- Copies the shared router/runtime, configures the layer through the modified installer, and verifies registration, manifest paths and DLL hashes. Checks conflicting XR override environment variables before setup.
- Installs a per-user **OpenXR Pose v2** implicit layer and the per-game helper set. Skips rewriting an identical shared DLL so another running game need not release it.
- Keeps separate headset/mirror configurations (`ReShadeVR.ini`, `ReShadeDesktopUI.ini`) and helper enable state. Does not globally turn off ordinary flat rendering merely because UEVR might be injected later.
- Retires the previous per-game `palia-xr-queue` adapter; keeps tracked backups/manifest entries for managed changes. Duplicate-proxy retirement support is present in `vr-auto.js`; it is not a promise that all unknown loaders can be safely replaced.
- Supports explicit first-install VR choices when a flat game's folder does not yet reveal that UEVR will be injected.

The modified ReShade binary source is unavailable here. Upstream ReShade SDK headers and router/helper source in Git are not a full source reproduction of that patched binary.

### 7. Fixed VR foveation

- Replaces the 64-bit Feeder with the customized **1.16.0-beta.4-vr-universal-foveated26.28** build; it remains the selected 64-bit Feeder even when foveation is off.
- Adds install-time **Off / Small / Balanced / Wide / Large** selections and live ReShade controls: **Enable VR foveation**, **VR foveation size**, and Custom width/height from 35–90%.
- Preset center regions per eye: 50×45, 60×50, 70×50 and 75×60 percent, corresponding to 22.5%, 30%, 35% and 45% of eye pixels.
- Implements supported packed side-by-side headset-center packing/compositing in **D3D11 and D3D12**. Neural work covers the center; outside pixels remain the current native image, with feathered transitions.
- Uses bounded layout validation and headset/no-window runtime gating; changing a crop can rebuild/invalidate affected work. This is fixed center foveation, not eye-tracked foveation or a guaranteed FPS gain.

#### Native DLSS foveation bridge (2.2.3-vr.2)

- Adds `native-vr-foveation-nvngx.dll.addon64` and a separate tracked `dlss5-native-foveation.ini`, installed on native Direct3D routes when VR support is enabled. The UI offers native presets for DXGI/D3D12 selection. Live controls appear in **Add-ons → Native DLSS VR foveation**.
- Intercepts D3D12 evaluations in the NR DLL, leaving the game's native SR DLL/evaluations untouched. Creates smaller Feature 18 jobs through the existing DFC companion or initialized NGX core; never directly reinitializes the snippet or adds a second CreateFeature detour.
- Applies matching valid subrectangles to color, output, depth, motion vectors and optional NR guides, including engine viewport offsets and differently sized guide allocations. Preserves the provider's parameter object and motion scales.
- Copies current color into the full output, evaluates only the center and feathers its border. Maintains separate histories by original feature, resource pair, crop size/layout and packed eye; resets crop/full-frame history when switching paths.
- Supports separate-eye/mono native features and explicitly selected packed side-by-side buffers. Unsupported arrays, aliasing, formats, layouts or resource states keep the full-frame native route with status/counters. Native D3D11/Vulkan and ambiguous alternating-eye identity are not established.
- Adds native-addon/config preflight, copy verification, manifest tracking, provider-profile settings retention and build-time payload verification. Builds from supplied NGX SDK headers, included ReShade/ImGui headers and Detours.
- Validation: inherited tests, VR installation/cache tests, native install test and WARP GPU test with a fake NR backend. Actual NVIDIA-model evaluation and headset visual validation are pending; this is an experimental native path.

### 8. Pose, stereo depth and projection guides

- **Pose bridge:** OpenVR and OpenXR rotation, translation and per-eye projection data; OpenXR frame/session/reference-space tracking and pose freshness validation.
- **Submitted-frame synchronization:** publishes the poses attached to the submitted stereo image before ReShade XR effects, avoiding use of a different head-pose sample for that image.
- **D3D11 stereo depth bridge:** captures both eyes, including supported layered and side-by-side layouts, multisampled depth, actual viewport offsets/gaps and small allocation padding. Uses per-command-list draw state, freshness/clear tracking and resets packed resources when layouts change.
- **D3D11 projection bridge:** validates bound stereo perspective camera constants and derives hardware-depth-to-inverse-Z coefficients; nonblocking readback avoids introducing a blocking GPU read for each draw.
- **Feed shader:** uses pose/depth reprojection and camera-motion modeling, with rotation-only fallback when depth is flat; optical-flow validation and motion/history relief reduce reliance on stale or implausible vectors.
- Accounts for actual crop geometry and motion-vector scaling so the packed center uses the correct coordinate domain rather than treating an entire side-by-side texture as one eye.

These are bounded supported paths. Arbitrary AFR eye association, every dynamic-resolution layout, and equivalent Vulkan VR depth/projection support are not implemented by these repairs.

### 9. Temporal image stability and UEVR desktop UI

- Changes the repaired VR presentation to composite into the **current frame**, removing the old asynchronous stale-result carry from the active current-frame path. Historical residual-compositor code still exists in the transformation script; its presence is not an active async-output feature.
- **Current-input bridge:** can bypass preliminary DLAA accumulation for the exact qualified Feeder VR resources feeding DFC, preserving the native SR call when ownership/parameters/callbacks do not qualify. Intended to prevent stacking preliminary temporal accumulation underneath the neural stage.
- **History guard:** watches pose/session timing and discontinuities, resets affected history and uses native fallback/recovery rather than blindly presenting an invalid neural frame.
- Adds **Headset output** status for native fallback, neural recovery and visible neural output, distinguishing processing activity from visible output.
- **D3D12 compatibility adapter:** maps and validates ReShade queue/device proxies for UEVR/OpenXR sessions, checks device identity, and preserves unknown bindings rather than guessing offsets.
- Adds desktop presentation/UI bridging and runtime ownership/reload/destroy protection so UEVR injection and mirror activity do not silently replace the headset processing owner.
- Includes missing **Agility SDK 618 D3D12Core.dll** deployment when the executable explicitly exports that SDK version/path, only within its executable directory and only if the file is absent. Verifies version and copied bytes.
- Adds install-time stable guide/history normalization: Feeder reset-every-frame off, warm-up rebuild 0, motion scales 1; DFC per-pass motion scales 1 and NGX depth convention. Normalizes duplicate keys, retains artistic settings, writes tracked backups and records verification/hashes in `manifest.installStability`.

These address the reported lighting shake, smearing/trailing and texture-motion problems; they are not a guarantee that the problems cannot occur in another game or after changing presets.

### 10. Packaging, source, tests and documentation

- Ships fingerprinted offline prebuilt VR helpers and SHA-256 checks, with source/config fingerprint invalidation of stale caches. Normal installs can use these without a compiler or NVIDIA SDK archive.
- Keeps a source-transform/build fallback for development, with portable MSVC discovery; rebuilding the Feeder/current-input components needs separately supplied SDK/source dependencies.
- Overrides packaging prebuild with fork-payload verification so the upstream collector does not overwrite customized binaries. Adds a manifest-refresh script and `test:vr` regression command.
- Includes native pose, projection, depth, current-frame copy, temporal and router test fixtures, portable test build scripts, Detours and the required ReShade/OpenXR SDK header notices.
- Published checks: 84 inherited tests, the five VR install/cache regression scripts, the D3D11 WARP depth test, and release ZIP/prebuilt hash checks passed. Inclusion of additional fixtures is not a claim that every fixture was run for this release.
- Adds the usage guide, expanded comparison/file audit, fork/component notices, portable prerelease and checksum. The root cleanup moves superseded repairs/builds/logs to a recovery archive; that archive is not part of the distributable.
- Publishes to Tyler's actual GitHub fork, with the VR branch as default; the portable app update configuration points to that fork. This ZIP is a manual portable release, not a claim that an automatic-update installer was built.

## Current upstream v2.2.9 versus this fork

Checked against upstream source and its [v2.2.9 release](https://github.com/rakanki911/DLSS5-Swapper/releases/tag/v2.2.9). Our fork is not v2.2.9 plus these modifications; it is the older v2.2.0 foundation with CreatorBase additions.

| Area | Upstream v2.2.9 | CreatorBase 2.2.3-vr.2 |
| --- | --- | --- |
| Provider choice | RenoDX/Feeder and newer separate multipass/OptiScaler routes | Explicit RenoDX / DFC / TRUE Upstream choice for native and Feeder |
| DFC | No dedicated DFC integration module/provider selector in the compared source | DFC installation, configs, lifecycle helper and NMS variant |
| Multipass | Separate route with up to ten passes; newer DLSS Tool/OptiScaler component work | DFC 1–30 post-SR passes and customized TRUE Upstream 1–4 chained pre-SR passes; these are different engines/topologies |
| VR/UEVR | Does not contain this fork's VR router, pose/depth/projection/current-input/compatibility modules | Modified ReShade/OpenXR plus the helper/foveation/stability work above |
| ReShade | Stock 6.8.0 component in release notes | Modified 6.8.0.2 V19 on the repaired OpenXR path; separate legacy Vulkan binaries retained |
| DLSS runtime | 310.9.1 | Packaged 310.8.0; Streamline DLL resources report 2.13.0 |
| 64-bit Feeder | 1.17.0 | Customized 1.16.0-beta.4 / v26.28; separate original/Vulkan experiment binaries retained |
| 32-bit Feeder/host | Current 1.17.0 component set | Inherited 0.12.0 component set/hash checks, not upgraded to the VR 64-bit build |
| RenoDX | 6.5.3, with older 4.7 handling also present | Retained earlier payload (file resource 0.2026.0828.0517); do not confuse its PE resource with an upstream marketing version |
| OptiScaler | Newer per-game standard/pre-SR build selection and updated older-card behavior | Inherited v2.2.0 optional backend and its older hardware checks; not migrated to the newer upstream behavior |
| dgVoodoo2 | 2.87.5 | Pinned inherited 2.87.4 download |
| App overlay | Separate F8 app overlay and themes for its supported RenoDX/Feeder controls | Uses ReShade Home/Add-ons and provider tabs; no upstream F8 app-overlay system |
| Community/chat | Opt-in community reports, chat and notifications | Not present |
| UI | New second theme and later library/API-override changes | Earlier UI plus provider/VR/foveation/launch changes |
| Diagnostics | Later save-diagnostics workflow | Copy-log and component log files; no later upstream diagnostics-bundle module |
| Recovery fixes | New retired-manifest restore discovery and later fixes | Earlier recovery plus fork tracked helper/config changes; not all newer restore fixes |
| Setup packaging | Updated electron-builder 26.15.3 and setup crash fix | Older 25.1.8 source build dependency; published portable folder ZIP, not a new NSIS setup |
| ReShade hook choice | Added explicit ReShade-file choice including wrapped DX8/9 cases | Older hook selection; no corresponding newer GUI selector |

“Multipass,” native/Feeder installs, cross-API routes, game libraries, backup/history, custom add-ons and OptiScaler are therefore not all unique concepts invented by this fork. The distinguishing changes are the specific integrations, patched components, controls and behavior listed above. Newer upstream features absent here were not intentionally removed from v2.2.9; this branch has not merged those releases.

## Earlier experiments and currently inactive support

- `runtime-components.js` includes **Neural Upstream v0.3.0** and **DLSS5-ReShade-AIO v2.1.1** download/architecture helpers. No current `main.js` install path calls those helpers. The AIO experiment is not the current TRUE Upstream provider.
- Original Feeder, Vulkan SR v10, DFC's native DX11 bridge and older lifecycle binaries remain in the payload. Availability is not automatic selection or equivalent tested VR support.
- Archived work includes early AIO multipass, repeated-evaluate upstream, later chained/per-pass upstream patches, DFC Vulkan late-arm changes, NMS Vulkan SR/depth experiments and per-game repair builds. Active behaviors were summarized above only where connected to current code/binary evidence.
- Complete build provenance/source for customized DFC lifecycle and Neural Upstream binaries is incomplete in the published checkout. SDK headers and archived patch descriptions do not establish a reproducible build. Component names are credited; this document does not relicense third-party binaries.

## File-level evidence and audit coverage

`FORK_AUDIT.json` includes every tracked path differing from the v2.2.0 base and v2.2.9 snapshot at the audited runtime commit, plus every file in the active runtime payload with size and SHA-256. This captures headers, licences, test fixtures, packaging changes, binary variants and documentation that do not each warrant a feature claim. It contains relative paths, not local game logs or private installation records.

Application source changes from the v2.2.0 base are in `main.js`, `preload.js`, `src/core/agility-runtime.js`, `apply.js`, `backend-manager.js`, `deep-fried-chicken.js`, `feeder-config.js`, `feeder-release.js`, `install-stability.js`, `openxr-pose-install.js`, `runtime-components.js`, `scan.js`, `vr-auto.js`, `vr-foveation.js`, and `src/renderer/renderer.js`. Remaining changes are packaging/docs, tests, native helper/build source, binaries and third-party dependencies/notices. A large line diff in `main.js`/`apply.js` also includes reformatting and is not counted as thousands of separate features.

This audit records implemented/shipped differences and observed metadata. It does not replace game testing, resolve every older UI description, or establish universal API/GPU/game compatibility.
