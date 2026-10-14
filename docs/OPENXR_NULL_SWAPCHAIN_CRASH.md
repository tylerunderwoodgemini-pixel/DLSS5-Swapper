# Road to Yvhalon null swapchain cleanup

The 2026-10-13 10:47 run used the dispatch-repaired menu addon
`8df9a8a681931fb736de0c61074be08482c5450962cfffb96bf5af8e3c0cc582`.
It completed OpenXR session creation and submitted eye images, then crashed
in the modified ReShade owner's xrDestroySwapchain handler. The final log
entry was `xrDestroySwapchain(swapchain = 0)`. The Feeder dump shows a read
from 0xC8 at dxgi.dll +0x1d6861 after the handle table returned a null entry.

The null lookup matches the upstream 6.8.0 implementation's unconditional
`s_openxr_swapchains.at(swapchain)` dispatch-table access:
https://github.com/crosire/reshade/blob/v6.8.0/source/openxr/openxr_hooks_swapchain.cpp

The preceding color swapchain request was 3080x3272, two array layers,
format 91 and logged usage flags 0x79. The old logs do not record its return
value or returned handle, so they do not establish why Unity destroyed zero.
No format, usage or resolution fallback is justified by that evidence yet.

The outer adapter now rejects null cleanup with XR_ERROR_HANDLE_INVALID
before entering ReShade. Its creation wrapper records the actual result and
handle and rejects a success result without a valid handle. It preserves
valid creation and destruction and does not turn failed creation into success.

Dashboard tests exercise valid creation/destruction, failed creation, null
cleanup and success-with-null contracts. The prior dispatch-recursion and
focus tests, graphics/controller tests, six VR install suites and payload
verification pass. Both local portable payloads and prebuilt fingerprints
are updated. Installed game files remain unchanged until reinstall.

Current addon SHA-256:
`aaa308293535607fee50c226b4d1ef18f047917ecdd65b53f06fb5a0be807c7a`.

The immediate null-dereference path is guarded. Full game functionality and
the reason for the null cleanup still require a new game run and its logs.

## 11:00 follow-up: duplicate destruction on different threads

The new creation diagnostics show both stereo swapchains returned XR_SUCCESS
and nonzero handles. The resized swapchain is 3080x3272. The old handle
`1a48a6006e0` is destroyed by thread 9384 at 11:00:25.753 and again by thread
14760 at 11:00:25.759. The second destruction produces the same ReShade
missing-table-entry dereference at dxgi +0x1d6861. The first guard caught
null handles but could not reject a nonzero handle already destroyed.

The adapter now records successful game swapchain handles and claims/removes
each handle under a mutex before forwarding its destruction. A second thread
receives XR_ERROR_HANDLE_INVALID instead of entering ReShade. No owner API
call occurs while the registry mutex is held. Failed destruction reinstates
the claim for retry; session destruction removes its remaining entries.

Tests cover sequential and concurrent duplicate destruction, null handles,
session removal and handle reuse. Only one native destruction is observed
for two concurrent cleanup requests. Existing controller, focus, graphics,
VR install and payload tests pass. Local source and portable payload copies
are updated, with addon SHA-256
`bbf55bfe14126bd8e86caac21b89569f8f321a5d1cfe3e17adf12493e59b2c39`.
The game needs a reinstall and test with this newer build.
