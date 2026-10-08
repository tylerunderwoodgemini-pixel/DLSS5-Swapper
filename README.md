# DLSS 5 Swapper: VR compatibility fork

Fork of [Rakan Alkhaldi's DLSS5-Swapper](https://github.com/rakanki911/DLSS5-Swapper), based on **v2.2.0**. This branch contains the CreatorBase modifications and VR/UEVR repairs. It does not incorporate all later upstream v2.2.9 changes.

## Use

Download the portable ZIP from this fork's Releases, extract it completely, and run `DLSS 5 Swapper.exe`. Use VR mode Auto and select the desired foveation size. Repair/reinstall older game installations to update their local helpers. This is an experimental **prerelease**, not a guarantee of compatibility with every game.

## Changes

- Modified ReShade 6.8.0.2 V19 OpenXR integration and a per-game runtime router.
- OpenXR/OpenVR pose, current-input, stereo depth and projection helpers.
- D3D11/12 side-by-side fixed foveation with native-frame periphery.
- D3D11 depth detection for layered, aligned, offset and gapped eye layouts.
- UEVR desktop UI and D3D12 queue/device compatibility helpers.
- Verified offline feeder v26.28 and cache integrity checks.
- Install-time temporal history and synthetic guide repair, with tracked backups.

## Compatibility

| Path | Status |
| --- | --- |
| 64-bit D3D11/12 native OpenXR/OpenVR and UEVR | Experimental repaired path; selected game tests |
| Flat 64-bit D3D11/12 feeder | Current feeder and installer stability checks |
| DX9, DX10, Vulkan, OpenGL and 32-bit | Existing routes retained; equivalent VR stability not established |
| AFR/single-eye association and arbitrary dynamic-resolution resampling | Not implemented by this depth repair |

Reported improvements: FreelandVR, Battlemarked, TMNT Empire City, Palia/UEVR, and Supraland Demo using official UEVR Nightly 01143. RetroRewind's latest depth repair still needs headset confirmation. These are user reports, not certification. The UE5.7-specific injector used for Palia failed in the older Supraland Demo; injector compatibility remains separate from Swapper installation.

Successful neural evaluation does not prove correctly aligned visible output. Reset-every-frame and motion-scale overrides can reintroduce instability if changed after installation.

## Source and build

```powershell
npm ci
npm run test:vr
npm test
```

Native WARP tests require Visual Studio C++ tools: `cmd /c tests\run-vr-depth-test.cmd`. Helper source and the feeder transformation script are in `payload/vr-foveated`; router source is in `payload/openxr`.

To package, extract this fork's portable release and copy its `resources/payload` into the checkout's `payload` directory. `npm run build` verifies the fork payload before packaging. Do not run upstream's payload collector over this customized payload; it can replace patched files with stock builds.

The feeder source transformation needs a separately supplied NVIDIA archive and pinned upstream feeder source. Current-input compilation also requires NGX SDK headers. ReShade V19 was supplied as a **binary patch**: its modified source and a source-reproducible ReShade build are not available here. Other original binaries retain their third-party licensing.

## Credits

Original application: **Rakan Alkhaldi**, MIT licence. Fork integration: **Tyler Underwood**. See `LICENSE`, `THIRD_PARTY_NOTICES.md`, and component notices. NVIDIA runtime DLLs and other third-party software are not relicensed under the application's MIT licence; no NVIDIA endorsement is claimed.

[Original v2.2.0 README](docs/UPSTREAM-README-v2.2.0.md).
