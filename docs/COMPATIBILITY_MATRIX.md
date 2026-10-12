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
| NMS VR native Vulkan mirror + DFC | User reports working perfectly after disabling Feeder and setting bridge unwrap=0; logs confirm 13,800+ real-DLSS mirror/neural frames | Existing bridge plus manual helper; not an automatic native Vulkan install or independent foveation/performance certification |
| RetroRewind + UEVR | No visible effect reported | Still unverified; do not label supported by name |
| Nivalis Nights Feeder | Submitted-depth repair substantially reduced flickering; residual flashes reported on monitor/VR transitions; baseline aliasing remains | Transition recovery build awaits headset confirmation; spatial AA was reported insufficient |

Game support must be attached to a route, graphics API, runtime, game version and test result. Successful injection or an increasing historical counter is not visual certification.

## Repeatable acceptance procedure

1. Install with the actual graphics API and selected provider, start gameplay, and initialize the headset/mod. Confirm the intended route in dlss5-compatibility.json.
2. In the native foveation panel, verify recent cropping features, the intended eye count, region dimensions and fallback reasons. Save compatibility report if diagnosing a failure.
3. Compare effect off/on while still, leaning near objects, and turning the head. Check both eyes, current outer pixels, boundary fade, texture stability and actual frame time.
4. Repeat at least five pause/loading transitions; change resolution/render scale; stop and restart VR where supported. Verify automatic recovery without F8 toggles.
5. Try Small/Balanced and a custom size, release each slider before comparing. Repeat with the alternate native/Feeder route if available.
6. Preserve logs and a diagnostic report alongside API, route, headset/runtime, GPU/driver and mod settings. Mark failed or untested cases explicitly.

The automated suites exercise contract, state, pixels, bindings and lifecycle behavior. They cannot reproduce arbitrary game scene selection or every headset runtime. Passing the procedure above is the basis for adding a game to tested compatibility.

Current source includes fixes newer than release v2.2.3-vr.3. Updating this branch does not update the downloadable release archive. The NMS native-only helper is a separately applied configuration test; reinstalling the Feeder route can re-enable Feeder. See [NMS native mirror setup](NMS-NATIVE-MIRROR-DIAGNOSIS.txt).
