# CreatorBase 2.2.3-vr.3

This release includes the working Starfield native foveation and pause/loading recovery fixes, the full stationary OpenXR F8 ReShade menu, and compatibility diagnostics for new games.

- Native D3D12 NR uses aligned per-eye crops, reduced center resolution, feathered edges, preserved compute bindings, and fenced history retirement. Size changes apply when sliders are released. Default center quality is 85%, with 10% edge fade.
- DFC normalizes modified ReShade queue wrappers before its process-lifetime hook checks. This fixes the observed permanent neural disable after Starfield pause/loading; the user confirmed recovery.
- Bounded native recovery retries an evaluation failure only after its recording and GPU work finish, with a five-second delay and at most two attempts per owning feature. Creation timeouts and unknown resource states retain the normal path.
- Native image guides reject non-finite motion/jitter values. Existing viewport, format, resource-state, device, and stereo checks remain required.
- The native headset panel reports recent activity per NR feature, eye count and image/center/work dimensions. Historical counters no longer imply current activity. Save compatibility report writes a diagnostic JSON next to the game executable; reporting adds no continuous disk writes.
- Each managed install writes a restorable dlss5-compatibility.json with the selected route, common preset geometry, delayed-VR readiness, capabilities and limits. Auto mode prepares supported 64-bit DirectX installs for VR injection after launch.
- Includes the previous unpublished OpenXR UI/controller, native crop, NMS Vulkan routing/device identity, Starfield scanning, and modified ReShade integration fixes since vr.2.

Validation: 91 application tests; six VR/install suites; NMS Vulkan installer; WARP pixel readback and GPU validation including 12 VR interruptions, bad guide input and fenced recovery; DFC executable patch tests. Actual NVIDIA NR tests cover separate/packed images, native/modified ReShade devices, 22 changing crop sizes, three NR features, producer-list rotation, binding restoration and GPU completion. These tests do not certify every game or GPU/headset combination.

Extract the portable ZIP fully, restart the swapper, then reinstall/repair existing game installs. It does not update a game while it is running. Keep the selected graphics API accurate. F8 opens the stationary full OpenXR ReShade menu; use the right controller to point/select, with gameplay input blocked while open.

Native D3D12 NR cropping is supported by qualified contracts. Native Vulkan and native D3D11 NR cropping are not implemented. Feeder D3D11/12 foveation uses its existing VR pipeline; Vulkan Feeder with DFC uses the private D3D12 crop bridge. Arrays, multiview, AFR, multi-adapter and unqualified buffer contracts are not claimed compatible. Native SR and DFC full-image conversion remain full frame. See COMPATIBILITY_MATRIX.md for tested evidence and remaining coverage.

This is a prerelease based on upstream v2.2.0, with fork additions; it is not an upstream v2.2.9 merge. All component credits and redistribution notices remain included. Complete reproducible source for the modified ReShade and inherited binary-modified engines remains unavailable; original-authored bridges and exact-build repair scripts are included in the source repository.
