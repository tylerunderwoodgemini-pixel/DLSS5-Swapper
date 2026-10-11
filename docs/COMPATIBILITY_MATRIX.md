# VR compatibility matrix

## Rendering paths

| Path | OpenXR / OpenVR / mod injection | Foveation | Evidence |
|---|---|---|---|
| Native DLSS with D3D12 NR | Active headset required; late injection supported | Per-eye cropped NR; SR remains full frame | NVIDIA runtime with raw/modified ReShade devices, separate/packed images; user Starfield confirmation |
| Feeder D3D11/12 | Submitted headset image, pose/depth helpers | Existing center crop/composite route | Installation suites and user reports below; no fresh end-to-end headset certification in vr.3 |
| Vulkan Feeder + DFC | Selected OpenXR/OpenVR routing; explicit matching stereo layout | Private D3D12 NR crop bridge | Vulkan installer and routing tests; NMS visual/performance testing remains outstanding |
| Native Vulkan / native D3D11 NR | Runtime may work through ordinary provider | Native crop bridge unavailable | Installer reports limitation; no claimed native cropping |
| DX9 / DX10 / OpenGL / 32-bit | Existing flat routes retain their own compatibility | VR foveation not certified | Flat-install regression coverage only |
| Array/multiview/AFR/multi-adapter or unknown guides | Contract-dependent | Normal path retained where rejected | No universal compatibility claim |

## User evidence from this development session

| Game/path | Reported result | Remaining coverage |
|---|---|---|
| Starfield native + VR mod | Working crop/feather/performance; pause/loading recovery confirmed | New vr.3 diagnostics/retry additions need fresh headset observation |
| Battlemarked Feeder | Major flickering and head-motion ghosting improved | Complete runtime/API, version, GPU and headset certification not recorded |
| Palia + UEVR | UI, foveation and lighting fixes reported working | Fresh vr.3 end-to-end test |
| Supraland + UEVR | Last reported test worked fine | Fresh vr.3 end-to-end test |
| NMS VR | Last visual test showed effect but no foveation and poor performance | GPU performance and stereo crop confirmation still needed |
| RetroRewind + UEVR | No visible effect reported | Still unverified; do not label supported by name |
| Nivalis Nights Feeder | DLSS5 visible; foveation absent reported | Still unverified; determine live image/layout contract |

Game support must be attached to a route, graphics API, runtime, game version and test result. Successful injection or an increasing historical counter is not visual certification.

## Repeatable acceptance procedure

1. Install with the actual graphics API and selected provider, start gameplay, and initialize the headset/mod. Confirm the intended route in dlss5-compatibility.json.
2. In the native foveation panel, verify recent cropping features, the intended eye count, region dimensions and fallback reasons. Save compatibility report if diagnosing a failure.
3. Compare effect off/on while still, leaning near objects, and turning the head. Check both eyes, current outer pixels, boundary fade, texture stability and actual frame time.
4. Repeat at least five pause/loading transitions; change resolution/render scale; stop and restart VR where supported. Verify automatic recovery without F8 toggles.
5. Try Small/Balanced and a custom size, release each slider before comparing. Repeat with the alternate native/Feeder route if available.
6. Preserve logs and a diagnostic report alongside API, route, headset/runtime, GPU/driver and mod settings. Mark failed or untested cases explicitly.

The automated suites exercise contract, state, pixels, bindings and lifecycle behavior. They cannot reproduce arbitrary game scene selection or every headset runtime. Passing the procedure above is the basis for adding a game to tested compatibility.
