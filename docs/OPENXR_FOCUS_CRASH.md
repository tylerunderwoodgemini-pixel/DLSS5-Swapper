# Road to Yvhalon OpenXR focus crash

The 2026-10-12 22:44 run loaded the repaired DFC SHA-256
`469c86ec9b1df32387e0f8afe2170f0ca1b1475164cd4b206df9105fa32e5b0b`.
Its new crash was a C++ `std::system_error` with "resource deadlock would
occur", originating in `DLSS5MenuFocus` in the OpenXR menu addon. The
Feeder recorded the exception before processing any frames. Both its dump
and `RoadToYvhalon.exe.47876.dmp` show this same exception. The menu mutex
owner in the dump is the current thread (0x9c78), with acquisition count 1.

The old focus callback unconditionally acquired the panel mutex. Session
setup and rendering hold that same mutex while calling other XR functions,
so reentrant focus notification attempted to lock it twice on one thread.

Focus events now use a separate, bounded queue protected only for short
memory updates. The callback attempts the panel mutex without waiting. If
already held, the current owner applies the saved event after setup or
rendering. Uncontended focus changes apply immediately. Focus loss closes
the menu and releases its input capture; pending events are removed when
sessions are destroyed. No XR calls occur while the event queue is locked.

`tests/openxr-focus-reentry.cpp` calls the actual addon exports and simulates
a focus callback during xrGetSystemProperties inside session attachment.
The old binary reproduces the exact system_error. The rebuilt binary passes
that case and 100 subsequent loss/focus cycles. Dashboard tests verify
deferred state, menu/input release and destroyed-session cleanup, alongside
existing graphics, controller and failure-path tests. The capture dispatch
test, six VR install suites and payload verifier also pass.

Rebuilt addon SHA-256:
`6ebecc9dd9d04d2fc5f524c81265f708045ab0c8d1cc095e1f55f465d57e6fa1`.

Both local swapper payloads and their prebuilt fingerprints are updated.
Installed game files and shared OpenXR registration were not changed.
A game test after reinstall is still required.

## 2026-10-13 follow-up: controller dispatch recursion

The 09:00 run used the rebuilt focus-safe addon, but Unity's Player.log
recorded StackOverflowException in Internal_CreateSessionIfNeeded. There
was no new Windows crash dump for that run. The source contains a circular
dispatch path: the inner capture layer wraps input calls, ReShade wraps
those hooks, and the outer adapter wraps them again while replacing the
inner raw function pointer with ReShade's wrapper. The resulting hook ->
ReShade wrapper -> hook recursion can exhaust the stack during menu action
creation. An initialization fallback could independently overwrite the raw
suggest/attach/sync target published while GIPA was resolving that function.

The adapter now leaves input wrapping to capture when capture participates
in the chain. Initialization rechecks the raw target after calling GIPA,
preserving any target installed during dispatch. Legacy configurations
without capture retain adapter input hooks. The dashboard regression test
reproduces the old two-layer cycle with a bounded mock wrapper, then verifies
layered initialization preserves raw targets and repeated action creation
does not recurse. Existing focus, graphics, controller and six VR install
tests pass. Both local payload copies and prebuilt manifests were updated
again; the earlier SHA-256 in this document describes the focus-only build.

The Player.log supplies the stack-overflow symptom; identifying this dispatch
cycle relies on source inspection and reproduction, since the latest run
did not produce a native crash stack. Confirmation in the game is pending.
