# CreatorBase 2.2.3-vr.1 prerelease

**[Usage guide](https://github.com/tylerunderwoodgemini-pixel/DLSS5-Swapper/blob/vr-stability-v2.2.3/docs/USER_GUIDE.md)** — also included as `USER_GUIDE.md` in the portable ZIP and as a separate download. Covers game selection, install options, flat/native VR/UEVR, ReShade controls, foveation, appearance and troubleshooting.

**[Complete additions and upstream comparison](https://github.com/tylerunderwoodgemini-pixel/DLSS5-Swapper/blob/vr-stability-v2.2.3/docs/FORK_DIFFERENCES.md)** · **[File/payload audit](https://github.com/tylerunderwoodgemini-pixel/DLSS5-Swapper/blob/vr-stability-v2.2.3/docs/FORK_AUDIT.json)**. Both are included in the portable ZIP and available separately below.

This fork adds more than VR support:

- **Deep Fried Chicken:** selectable native/Feeder provider, companion runtime/config installation, its 1–30 pass controls/presets, conservative shipped appearance settings, lifecycle v20 helper and No Man's Sky variant.
- **TRUE Neural Upstream:** customized pre-SR chained 1–4 pass add-on (default 3), independent per-pass network controls, render-resolution ping-pong/barrier patches, Feeder SR configuration and separate NMS variant. The older GUI hint saying three evaluations describes the default.
- **Provider management:** RenoDX/DFC/TRUE Upstream selector, actual installed-provider tracking, provider-specific config profiles, conflicts and managed switching.
- **Installer/UI alterations:** evidence-based API choices, conservative Unity detection, actual-executable launch, payload/hash diagnostics and VR/foveation options.
- **VR/UEVR:** modified ReShade 6.8.0.2 V19, OpenXR router/pose v2, submitted-image pose synchronization, stereo depth/projection/current-input helpers, D3D11/12 center foveation, current-frame compositing and headset native fallback/recovery status.
- **Stability/compatibility:** UEVR desktop UI and validated D3D12 queue/device adaptation, matching missing Agility runtime deployment, temporal history/motion/depth settings repair with tracked backups and verification.
- **Distribution:** offline fingerprinted v26.28 prebuilt helpers, native fixtures/regression scripts, licences/notices, usage guide and file audit. 84 inherited tests, the VR install/cache regression suite, WARP depth test and ZIP/hash checks passed.

Component authors retain credit for DFC/Neural Upstream engines and controls. Older AIO download helpers and Vulkan SR/NMS experiments are documented separately from active installation features.

Based on upstream **v2.2.0**, compared against **v2.2.9** on October 8, 2026. Current upstream also offers multipass; it has newer components, its F8 overlay, community/chat, themes and fixes not merged here. This release is not a v2.2.9 build with all those features.

Extract the ZIP fully and launch DLSS 5 Swapper.exe. Reinstall/repair older games. Universal compatibility is not claimed. AFR, Vulkan VR and several layouts remain unvalidated or unsupported. Modified ReShade and customized Neural Upstream/DFC lifecycle binaries do not have complete reproducible source provenance in this checkout. See the comparison for component versions and practical limits.
