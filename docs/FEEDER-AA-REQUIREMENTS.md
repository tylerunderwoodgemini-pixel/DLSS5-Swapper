# Feeder VR anti-aliasing integration requirements

Nivalis' latest run now has finite submitted depth, reversed-depth normalization
and a known per-eye physical inverse-Z conversion. The remaining reported
flickering and aliasing are not resolved by those checks.

The existing Feeder passes zero jitter offsets. Its foveated mode crops the
synthetic DLAA input as well as neural rendering, then restores the original
current image outside the crop. Thus it does not provide engine-integrated,
whole-view DLAA anti-aliasing. The engine's existing AA should remain enabled.

Proper integration must obtain actual per-eye camera projection jitter and the
matching rendered frame, depth, exposure and motion vectors. A synthetic jitter
sequence applied only to an already captured image is not a substitute. Jitter
must be applied during scene rendering and accurately reported to reconstruction.
Object motion cannot be represented by head pose alone.

The intended pipeline is full-view engine-quality AA/reconstruction, then
cropped neural enhancement, with the anti-aliased image as the feather/periphery
baseline. Each eye must have independent histories and matching jitter phases.
Reset on camera cuts, loading, resolution changes and VR session changes;
preserve histories across ordinary head movement. Capture HUD after AA where
the engine provides that separation.

For DLAA use matched input/output viewport resolution. DLSS Quality needs an
engine-rendered lower-resolution input, matching guide dimensions and physical
sampling jitter; downsampling a finished frame is not equivalent.

Nivalis integration currently needs access to its VR mod source or a documented
per-eye rendering interface. The local Alpha 1.1 package contains compiled
NivalisNightVR.dll, exporting NnvrBoot only; it provides no public reconstruction
handoff. This checkout should not claim DLAA-level quality without that input.

Reference: NVIDIA's Streamline integration guide explains that missing/incorrect
jitter can cause poor anti-aliasing and increased flickering:
https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline/

Acceptance: static thin edges and moving geometry in both eyes, nearby leaning,
specular/wet surfaces, non-camera object motion, crop boundaries and periphery,
pause/loading recovery, plus GPU frame time. An NGX success code alone is not
evidence of anti-aliasing quality.

## Available fallback without VR-mod source

The local swapper now includes an original-authored whole-view spatial edge AA
shader. Selected 64-bit Feeder foveation installations place
`DLSS5_SpatialAA` after `DLSS5_Feed`. It filters the final current-frame image,
including the periphery, without adding history, sharpening or artificial jitter.
Packed-eye samples are clamped to their own viewport. Strength defaults to 0.65
and existing explicit settings are preserved. This helps spatial jaggies; it is
not DLAA and does not establish that neural lighting flicker is fixed.

Reinstall through the local swapper; in ReShade, check **Whole-view edge AA
(spatial)** below **DLSS 5 Feed**. Toggle it for comparison or reduce its strength
if fine details soften. Keep the game's own AA enabled where the game/mod permits.
GPU tests verify diagonal edge smoothing, zero-strength identity, flat HDR and
alpha preservation, and no cross-eye boundary contamination.
