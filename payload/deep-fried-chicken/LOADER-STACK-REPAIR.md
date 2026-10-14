# Unity/Mono loader boundary repair

Road to Yvhalon (Unity 6000.0.24f1) crashed on 2026-10-12 before its first
Feeder frame. Dumps `RoadToYvhalon.exe.23364.dmp` and `.6816.dmp` show the
same LoadLibraryExW chain: Mono -> DFC +0x228e0 -> Steam overlay -> ReShade
-> Steam overlay -> GetModuleHandleW -> Windows loader. The aligned SIMD
store at `ntdll!RtlDosApplyFileIsolationRedirection_Ustr+0x192` targets an
address ending in 8, which violates the instruction's 16-byte alignment.

The actual saved return addresses establish that the Mono call enters DFC
with RSP ending in 0 instead of the Windows x64 ABI's required 8. DFC's
ordinary prologue carries that misalignment into the existing loader hooks.
The dumps establish the incoming misalignment; they do not contain enough
JIT code to identify why Unity/Mono produced it.

`scripts/patch-dfc-loader-stack.py` adds a verified boundary wrapper for
LoadLibraryExW. It remembers the original stack, aligns its private call
frame, calls the unchanged DFC hook, then restores the incoming stack.
All register arguments, nonvolatile registers, return value and last-error
survive. There are no stack arguments for this API. Windows unwind metadata
covers both the alignment wrapper and relocated original prologue.
Only six bytes in the original executable code are redirected; NGX,
resource handling, foveation, queue retirement and all original hook-body
instructions remain unchanged. This is a boundary workaround, not a change
to Unity's JIT or to Steam's overlay.

Generic SHA-256: `469c86ec9b1df32387e0f8afe2170f0ca1b1475164cd4b206df9105fa32e5b0b`

NMS SHA-256: `261bfbfb0848bfaba411c2f2866d2ac7549af4604492011f768e96d425582433`

Validation: `tests/run-dfc-loader-stack.cmd` exercises the actual hook in
Unicorn with both incoming alignments, preserved arguments/result/last-error
and unchanged unrelated PE sections. Native CPU tests run both payloads
against an aligned SIMD-store loader stub and check RtlVirtualUnwind with
both incoming stack alignments. Existing queue-identity and NMS-routing
tests pass, along with 97 application tests, all six VR install suites and
the payload verifier. A game launch after reinstall is still required.
