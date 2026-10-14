# Road to Yvhalon settings synchronization crash

The 2026-10-13 11:32 run passed swapchain recreation, started Feeder
evaluation and opened the F8 menu. The new Windows dump
RoadToYvhalon.exe.20560.dmp contains repeating modified-ReShade frames:
dxgi +0x33e57, +0xa3e14, +0x9a145 and +0x9a9fe,
with the repeating broadcast call at RVA 0x33e51. The confirmed callback
entry is RVA 0x33c90. It copies a shader uniform to every other runtime;
each destination's setter emits the same callback, rebroadcasting back to
the original runtime. Stack exhaustion also prevents Feeder's exception
logger from completing, so this run's feed log contains no crash record.

The new runtime addon detours the four exact verified callbacks in our
modified 6.8.0 owner: uniform 0x33c90, effects 0x33ef0, technique 0x33f80
and preset 0x34130. A thread-local broadcast scope lets the outer callback
forward the update, while nested callbacks return without forwarding.
Uniform/technique/effects callbacks return false so setters still apply
the value. The owner binary is unchanged on disk. Image size and callback
instruction prefixes are validated before installing the detours.

Our own VR menu bridge also excludes its source runtime, rejects nested
forwarding, deduplicates tracked runtimes, and skips native broadcast
notifications before acquiring its runtime lock.

tests/reshade-settings-sync.cpp executes the actual modified ReShade DLL's
callback against two test runtimes. The original callback reproduces the
loop; the guarded callback updates both runtimes once. Uniforms, effects,
techniques and preset propagation are checked. Actual D3D11/D3D12 full menu
tests, dashboard lifecycle/controller tests, focus tests, six VR install
suites and payload verification also pass.

The local source and portable payloads are updated. Addon SHA-256:
`a3f37549e5ce7e80fb280fee12bd1e1020a1d17e2e23bd0962c8f7a6ac6abe31`.
The installed game still needs the newer addon via reinstall; full game
confirmation remains pending.
