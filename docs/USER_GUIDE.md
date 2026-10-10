# Using the CreatorBase DLSS 5 Swapper fork

For the **2.2.3-vr.2 prerelease**, with feeder **v26.28**. These instructions use the English UI labels. The Swapper selects and installs files; ReShade provides the controls while the game is running.

## Quick start: VR or UEVR without native DLSS

1. Extract the entire portable ZIP, then run **DLSS 5 Swapper.exe**. Keep its `resources` and `locales` folders beside it.
2. Close the game. In **Games**, click its cover. If it is missing, use **Add a game** and choose its installation folder.
3. Select the actual game **Executable**, usually inside `GameName/Binaries/Win64` for Unreal games. Choose the rendering API the game actually uses.
4. Set **Rendering backend** to **ReShade (default)**, **Installation route** to **DLSS5-Feeder (games without DLSS)**, and **Neural provider** to **Deep Fried Chicken**.
5. For native VR, start with **VR mode → Auto detect**. For a first UEVR/OpenXR install, use **Force OpenXR**. Select **VR foveation → Balanced 60x50 (30%)** explicitly.
6. Scroll down and click **Install DLSS 5**. Wait for the operation to finish successfully. Launch normally; for UEVR, inject into the running game using your compatible UEVR injector.
7. Focus the desktop game window and press **Home** to open ReShade. Check the Feeder controls under **Add-ons**, and the **Deep Fried Chicken** tab. Start with **Enabled** on and **Passes = 1**.
8. Test in an actual scene while looking still, turning, and leaning. Compare with Deep Fried Chicken's **Enabled** off, then on. Check the status as well as the picture.

The application does **not** automatically choose Deep Fried Chicken on a fresh game. Its provider default is RenoDX. Choose the provider deliberately.

## Finding and selecting a game

**Home** offers **Browse Folder** and a **Drop game folder here** area. **Games** shows the library, a search box, and filters for **Rendering API**, **DLSS status**, and **Add-on**. **Rescan** refreshes the library. Clear filters if a game seems to be missing.

- **Add a game** adds one game directory. You can select a game root containing nested executables.
- **Add a folder** adds a library/search directory containing multiple games.
- Click a game cover to open its detail panel. The install buttons are farther down the panel; scroll inside it.
- When several executables are found, use **Executable** to choose the rendering process. Select the game's shipping/game executable rather than its launcher, crash reporter, installer, or VR runtime. A SteamVR library card is not the game you want to modify.

An Unreal executable often ends in `-Win64-Shipping.exe`, although demos can use `-Win64-DebugGame.exe`. The detail panel shows its relative path, detected API and bitness. A DLSS version number or green library badge alone does not prove that neural rendering is visible in the headset.

## Choosing installation options

| Control | How to choose |
| --- | --- |
| **Rendering API** | Match the API selected in the game's launcher/settings. For example, choose DirectX 11 when launching with DX11, or DirectX 12 when launching with DX12. The picker appears only when multiple choices are available. |
| **Rendering backend** | Use **ReShade (default)** for the VR/UEVR path described here. **OptiScaler DLSS-NR** is a separate optional backend with its own hardware requirements and controls. |
| **Installation route** | Use **Native DLSS (RenoDX)** when you want to use the game's existing DLSS pipeline. Use **DLSS5-Feeder (games without DLSS)** for a game without native DLSS, or for this fork's configurable Feeder foveation path. |
| **Neural provider** | **Deep Fried Chicken** is the provider used in the reported VR/UEVR repairs. **RenoDX DLSS 5** is the default alternative. **TRUE Upstream (pre-DLSS SR)** chains neural passes before SR and has its own controls described below. |
| **VR mode** | **Off** for flat play. **Auto detect** for native VR. **Force OpenXR** or **Force OpenVR** when you know the runtime and automatic detection misses it. |
| **VR foveation** | Offered for the 64-bit Feeder route and D3D12 native DLSS route. Start with **Balanced 60x50 (30%)** for VR, or **Off (full frame)** for flat play. |

The native route's label still says “RenoDX” even when you choose another neural provider. Read **Neural provider** and **Installed backend** to identify the actual selection. Native foveation has a dedicated bridge and settings panel; it does not install Feeder.

For native DLSS, enable **DLSS or DLAA in the game's graphics settings** after installation; an idle DLSS pipeline cannot provide frames. Feeder games do not need a native DLSS option. Do not load RenoDX DLSS, Deep Fried Chicken, and another neural cascade together. The Swapper switches its managed provider files when you apply a provider change; independently installed mods can still conflict.

### Flat games

Choose the appropriate native/Feeder route, set **VR mode → Off**, and **VR foveation → Off (full frame)** if offered. For native DLSS, enable DLSS/DLAA in the game. Feeder's motion-provider and Feed effects must remain enabled for the Feeder route.

### Native OpenXR or OpenVR games

Start with **Auto detect**. If the game's runtime is known but missed, select **Force OpenXR** or **Force OpenVR**, then apply/install with the game closed. SteamVR can run both OpenXR and OpenVR applications; using SteamVR by itself does not determine which option to choose.

### Flat games using UEVR

The Swapper does not install UEVR or select its injector. Use a UEVR build/profile compatible with that game's Unreal version. For the tested OpenXR path, select **Force OpenXR** and a foveation size before installing. A first-time flat-game folder may not contain anything Auto detect can identify as VR; an existing UEVR profile can change detection later.

Launch the game, inject UEVR, then open ReShade from the desktop game window. UEVR's own overlay and ReShade are separate menus. Stereo method and rendering layout affect compatibility; the current foveation implementation requires a supported packed side-by-side headset output. A visible desktop mirror alone does not validate the stereo path.

## Installing, updating and restoring

1. Close the game before installing, reinstalling, changing providers or restoring.
2. Make your choices in the detail panel. Changing dropdowns only selects options; it does not apply them yet.
3. Click **Install DLSS 5**, or **Apply backend change** when switching an existing managed backend/provider. Follow the operation log until it reports success. If it fails, use **Copy log**.
4. Check **Installed backend**, **DLSS 5 add-on** and **ReShade** in the panel. Launch normally through the game's launcher if it needs one; **Launch Game** directly starts the selected executable.
5. If a first-load message asks for a full restart, exit the game process and start it again. Closing only an overlay is not a restart.

To update an older installation to these helpers, select the game and install again with the desired choices. Keep `_DLSS5_Backup` intact: **Restore originals** uses it to restore tracked original files and remove managed additions. Do not manually delete that backup to uninstall. **History** shows previous operations. This fork does not automatically update every already-installed game when you download a newer Swapper.

## Opening ReShade in game

Focus the desktop game window and press **Home**. On keyboards sharing keys, this may require **Fn + Home**. If you already changed ReShade's shortcut, use that shortcut instead. ReShade's **Settings** tab lets you change its overlay key. The key is saved as `KeyOverlay` under `[INPUT]` in the active `ReShade.ini`.

On first launch, complete or skip ReShade's tutorial. In VR/UEVR, use the desktop game window to operate the menu; visibility in the headset depends on the game's runtime and output. The menu may only become available after injection has completed and the game is rendering a scene. Press the overlay key again to close it.

- **Home** in ReShade controls shader techniques and their settings.
- **Add-ons** contains the Feeder's status and transport/foveation controls.
- **Deep Fried Chicken** is a dedicated provider tab with its own enable switch, passes, appearance controls and presets.
- If you selected RenoDX instead, use its provider controls in **Add-ons**; the Deep Fried Chicken tab is not expected to be loaded.

### Feeder shader setup

The installer enables and orders the motion provider before `DLSS5_Feed`. Leave both enabled. The provider is normally **VORT** (`vort_MotionEffects`), or **Lumenite Kernel** (`Lumenite_Kernel`) when that payload is available. The Feed effect's display label starts with **DLSS 5 Feed - VR Pose MV**; its shader file is `DLSS5_Feed.fx`.

If you change presets, verify that the motion-provider technique stays above Feed. The installer also selects the matching `DLSS5_MV_PROVIDER` definition. Do not change it without changing the matching provider. Leave **DLSS 5 Feed - debug view** off during normal play.

On the **native DLSS route**, “No effects found” is not itself a failure: native neural rendering uses add-on hooks rather than these Feeder shader techniques.

## Native DLSS foveation

For a 64-bit **DirectX 12** game with native DLSS, keep the **Native DLSS** installation route, select your neural provider and **VR foveation → Balanced**, then install. Enable DLSS/DLAA in the game. Reinstall an existing native installation to receive the new bridge. Native SR remains full frame; only the neural-rendering center is cropped. DFC runs this after SR; TRUE Upstream runs it before SR.

Open **ReShade → Add-ons → Native DLSS VR foveation**. Its preset selector changes Off/Small/Balanced/Wide/Large live. **Native NR stereo layout** defaults to **Separate eye / mono features**. Choose **Packed side-by-side** if the native NR color buffer contains both eyes in one texture. Each original feature/resource pair and each packed eye receives its own crop history. The outer image is the current input and the border is feathered.

Confirm **Native foveation active** and an increasing **Cropped NR calls** counter. Full-frame fallback states explain an unsupported resource format, missing guide, unobserved state, cache limit or rejected cropped feature. A selected preset alone does not prove it is active. The bridge does not reinitialize NGX or replace the game's native DLSS call; crop creation uses DFC's existing companion or an initialized NGX core. Providers without either path retain full-frame rendering.

This new native bridge is experimental. Installer/regression tests and a WARP GPU test using a fake NR backend passed; proprietary NVIDIA-model execution and headset image quality still need game testing. Array textures, in-place color/output, mismatched color/output viewports and unsupported/unknown resource states retain the normal native path. Native D3D11/Vulkan foveation is not implemented by this bridge. Sharing the same original handle and same buffers between alternating separate eyes cannot establish eye identity; use a supported per-eye or packed path.

## Adjusting Feeder VR foveation live

In **ReShade → Add-ons → DLSS 5 Feed**, find **VR foveation**:

- **Enable VR foveation** turns the crop on/off.
- **VR foveation size** chooses **Small**, **Balanced**, **Wide**, **Large**, or **Custom**.
- **Custom** reveals **VR fovea width (%)** and **VR fovea height (%)**, each from 35 to 90.

| Preset | Center width × height per eye | Approximate center pixel area |
| --- | --- | --- |
| Small | 50% × 45% | 22.5% |
| Balanced | 60% × 50% | 30% |
| Wide | 70% × 50% | 35% |
| Large | 75% × 60% | 45% |

These percentages describe the neural center region, not a guaranteed FPS improvement. Larger regions cover more of the view and cost more work. The outer region retains the current native frame. This is **fixed foveation**, centered per eye; it does not follow eye tracking. It applies to supported headset buffers, so changing it may not alter a separate desktop mirror.

Changes save to `dlss5-feed.cfg`. A size change can briefly rebuild the feature. The **Headset output** status distinguishes **neural result visible**, **neural recovery**, and **native fallback (neural result hidden)**. “Enabled” alone does not mean the processed result is being displayed. Persistent fallback requires diagnosis rather than raising effect strength.

## Adjusting appearance without losing the stable baseline

Start in **Deep Fried Chicken** with **Enabled** on, **Passes = 1**, and **NR Preset / NR Style = Default**. This fork's packaged config uses **1.0** for Pass 1's NR intensity, local tone, local structure and skin structure. Older standalone component docs describe a different 2.0 baseline; retain this fork's shipped values for the first comparison. Existing per-game artistic settings may be preserved during reinstall.

Increase one control at a time and compare the same scene. **NR Intensity**, **Local Tone Strength**, **Local Structure Strength** and **Skin Structure Strength** affect the appearance. Higher values or more passes can exaggerate fine detail and cost substantial GPU time. Leave **Texture Boost** and **Clean Fry** off for the first test. If edges look oversharpened, check both the game's sharpening and any extra ReShade sharpening before increasing structure/detail controls. Feeder's **Sharpness** control applies when its FSR expand-back path is enabled.

Keep these guide/history controls at the stable baseline for the repaired 64-bit Direct3D Feeder path:

- Feeder **Reset every frame (diagnostic)** off, **Warm-up rebuild (frames)** 0, and **MV scale X/Y** 1.0.
- Deep Fried Chicken **Motion Scale X/Y Multiplier** 1.0 on each used pass, and **Depth Convention** using the supplied/game NGX flag.
- Keep the shipped Feed shader's pose, camera-motion, validation and history-relief settings initially. Pose rows/validity are bridge data, not ordinary artistic sliders.

Use Deep Fried Chicken's **Enabled** switch for an A/B comparison. **Arm feature-1 interception on startup** is a different startup setting and needs a full restart. **Refresh neural contract** can rebuild after a presentation change; do not repeatedly reset a stable scene. **Export <game>.cfg** and **Load <game>.cfg** manage provider presets. ReShade's shader preset is separate, and changing it can change which Feed techniques are active.

### Using TRUE Neural Upstream instead

With the game closed, select **TRUE Upstream (pre-DLSS SR)** as **Neural provider**, then apply/install. On the native route, enable the game's DLSS/DLAA. On Feeder, retain its provider/Feed techniques; the installer selects 67% work resolution and SR expand-back (`work_upscale=2`) for this provider.

In ReShade's **Add-ons**, find **NR Pre-Upscale / DLSS5 NR Pre-Upscale**. The bundled add-on contains **TRUE chained multipass** and **Number of NR passes**. The preserved latest patch specifies **1–4** passes, default **3**; start with **1** for a comparison and increase gradually. Each pass consumes the preceding result before the original SR upscale. The Swapper hint saying three evaluations describes the default, not the adjustable limit.

Expand **NR Pass 1**, then the other used passes. Each exposes **Render preset hint**, **NR Style**, **Intensity**, **Local tone**, **Local structure**, **Skin structure**, and **Automatic mask**. **Copy Pass 1 to every pass** copies the first pass's appearance settings. A skin-structure value of **-1** leaves that choice to the network. Settings use the `NRPreUpscale` section of ReShade's config; they are separate from DFC presets. This provider is not the older **Standalone DLSS-NR + SR** AIO experiment and does not use DFC's tab.

See [the complete fork comparison](FORK_DIFFERENCES.md) for provider variants, component versions and what remains experimental.

## Checking success and troubleshooting

Test in actual gameplay, not only a static menu. Watch nearby objects, distant edges, lights, HUD text and foliage while turning and leaning. Feeder's **Session**, **Feature** and increasing **Frames delivered** indicate transport activity. Check provider activity and **Headset output** as well: delivered frames do not by themselves prove a correct visible result.

| Symptom | First checks |
| --- | --- |
| Game missing | Clear library filters, **Rescan**, or **Add a game**. |
| Install disabled | Choose a rendering executable and supported API; read the panel's reason. Native DX10 is not an automatically supported Feeder route. |
| ReShade menu absent | Focus the game window, check the overlay shortcut and correct executable/API, wait until injection finishes, then check `ReShade.log`. Repair using this fork's bundled modified ReShade rather than replacing it with stock ReShade. |
| Menu works but no visual change | Check provider **Enabled**, native DLSS/DLAA when using the native route, Feed techniques when using Feeder, frames delivered, provider status and headset fallback status. Compare the headset, not just the mirror. |
| Lighting shakes or head motion smears | Return guide/history controls to the baseline above; start with one pass and remove competing neural providers through their installers. Record status/logs if it persists. |
| Foveation controls present but no crop | For Feeder, confirm 64-bit, VR mode and supported headset layout. For native, check Native DLSS VR foveation status, cropped-call counter, D3D12 and the correct native NR stereo layout. |
| FPS collapses | Return to one pass, disable optional detail/cleanup features, and try a smaller center region. |

For a report, include the game name, actual executable, API, GPU/driver, native VR or UEVR build/runtime/stereo method, selections in the Swapper, and whether the problem occurs in the headset or desktop. Collect **Copy log** plus the newest `ReShade.log`, `dlss5-feed.log`, `deep-fried-chicken.log` and `dlss5-vr-compat.log` beside the real game executable, where present. For UEVR also include its game-profile `log.txt`. Review logs for personal paths before posting them publicly.

The guide's Swapper screens were checked in the running app; in-game control names were checked against the packaged source/config and component docs. Every described in-game combination has not been visually validated. See the fork README for supported paths and remaining limitations.
