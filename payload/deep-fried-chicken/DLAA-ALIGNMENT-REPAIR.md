# DFC DLAA alignment repair v1

This fork patches the exact DFC 1.4.8-alpha binary. It is not an upstream release.
The late DLAA dimension-equality check now accepts nonzero input dimensions
aligned to eight pixels when each output dimension is greater than or equal
to input and exceeds it by at most seven pixels. Exact-size DLAA and all
non-DLAA behavior remain unchanged. Prior resource, guide, and codec checks
remain intact; no game NGX parameters or textures are changed.

This addresses Starfield VR's 3824x3904 to 3826x3904 DLAA contract. The existing
DFC output-resolution post-pass and lower-resolution guide handling do the work.
It does not force input guides to the output allocation size.

Reproduction: scripts/patch-dfc-dlaa-padding.py ORIGINAL OUTPUT (Python with
pefile and keystone-engine). The script refuses other input hashes and checks
the original guard and executable padding before patching. Tests execute the
actual patched guard with Unicorn, including its relocated entry branch,
and check rejection boundaries, preserved registers/stack and input data.

Original SHA256: 106143de0d74853b966a7141c19c2bb0fb46e7ce32c7f67d830d2dc5cddf80ec
Patched SHA256: 1c88c0ffddb46ceb6b97ddff60e96ed3dd7bbe478546b38a8d67ce9b0d31047a

Automated checks validate this gate and payload/install integrity. Actual
NVIDIA neural output, headset appearance, and desktop UI require a live run.

The shipped addon also includes the 2026-10-11 queue identity repair for native pause/loading recovery. Its combined SHA-256 is `7076f85df1c883800d714ed66c36461170a13dabadf7b3d77824587b941cd972`. The alignment-only hash above remains the reproducible intermediate build.

The 2026-10-12 local payload additionally includes the [Unity loader-stack repair](LOADER-STACK-REPAIR.md). Its current generic SHA-256 is `469c86ec9b1df32387e0f8afe2170f0ca1b1475164cd4b206df9105fa32e5b0b`; the earlier queue-only hash remains the reproducible input to that repair.
