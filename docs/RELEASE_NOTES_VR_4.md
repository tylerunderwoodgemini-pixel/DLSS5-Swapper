# CreatorBase v2.2.3-vr.4

NMS installs with Deep Fried Chicken now automatically use the verified native
Vulkan DLSS mirror instead of synthetic Feeder evaluation. This packages the
same native-only configuration the user reported working perfectly, backed by
13,800+ delivered real-DLSS frames and successful DFC neural evaluations.
Enable DLSS or DLAA in NMS itself. No separate test script is needed after an
install or repair. Other games/providers retain their existing routes.

Also includes the fixes developed since vr.3:

- Read actual submitted OpenXR D3D11 eye depth and normalize its convention.
- Supply matching physical per-eye inverse-depth projection coefficients.
- Reset Feeder history after VR focus/tracking interruptions even with valid depth,
  then recover through current native output and a bounded current-frame fade.
- Restore prior depth bindings after effects; do not retain stale bridge guides.
- Repair conflicting duplicate configuration keys without erasing unrelated tuning.
- Include whole-view spatial AA as a limited fallback; it is not engine-integrated
  DLAA, and Nivalis testing found it insufficient for the remaining aliasing.

Validation: 96 application tests, six VR/install suites, actual NMS fresh/repeated
install and exact restore checks, OpenXR layer/pose tests, deterministic Feeder
history-reset tests, and WARP separate-eye depth/binding/pixel regressions.
Portable archive CRC, application version, critical runtime hashes and checksum
are verified. These tests do not certify every game or GPU/headset combination.

This is an experimental prerelease. Unknown layouts, missing guides and unsupported
APIs retain documented limitations. NMS's automatic configuration has installer
coverage; other games still need route-specific headset testing.
