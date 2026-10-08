Swapper universal runtime package, feeder v26.28 / application 2.2.3

Install with the Swapper. Leave VR mode on Auto; use Force OpenXR/OpenVR if needed.
64-bit Direct3D 11/12 installations receive the repaired feeder and current pose,
stereo-depth, projection, current-input and compatibility helpers, including flat
Unreal games before a VR mod is injected. The desktop UI bridge handles D3D11/12.
The same modified ReShade 6.8.0.2 V19 is retained. OpenXR uses one stable router
registration and selects each game's local ReShade and compatibility adapter.
No executable-name restriction is used. Existing UEVR profiles are recognized.

The offline prebuilt package is verified and copied into the installer cache.
Builder, D3D12 patch/header and helper binary changes invalidate older caches.
Both 32-bit flat-game client/host verification and existing Vulkan paths remain.
New DFC configurations use neutral 1.000 detail/tone strengths. Existing DFC
settings are preserved. Foveation can be enabled/sized in Swapper or the Feed UI;
it applies only to a compatible headset runtime, leaving desktop rendering intact.

VR foveation requires packed side-by-side 2D D3D11/12 headset buffers. Array,
multisampled, unsupported shapes and D3D12 HDR10 use the existing full-frame path.
The package does not establish compatibility for every engine, graphics API or
VR injector version. UEVR engine-hook compatibility still depends on the injector.
Reinstall/repair previously installed games to update their local files. Restart
Swapper first so it loads the updated installer modules.

Depth bridge v5: D3D11 stereo depth can use observed per-eye draw viewports to
crop offsets and gaps from larger allocations. Exact and eight-pixel aligned
SBS/two-layer layouts remain supported. Mapping changes rebuild the packing
shader. Rendering contexts track their depth bindings and viewports separately.
The projection bridge shares aligned-allocation eligibility. Native D3D12 depth
still requires the existing matching SBS guide. AFR/single-eye association and
arbitrary dynamic-resolution resampling are not implemented by this update.

Application 2.2.3 install stability policy: 64-bit Direct3D feeder installs and
repairs restore temporal reuse (reset_every=0, warmup_rebuild=0) and pixel-unit
motion guide scales (1.000). DFC per-pass motion scales are restored to 1.000
and depth-convention overrides to Auto so the synthetic contract controls them.
All duplicate safety keys are normalized. Appearance controls, layer count and
selected foveation dimensions remain user-selected. The active backup manifest
records verified config hashes and changed values. Native-DLSS, Vulkan and
32-bit install paths do not receive this policy. Restart Swapper to load it.
These checks cannot stop a user from changing settings after installation.
