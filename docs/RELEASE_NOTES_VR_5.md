# CreatorBase v2.2.3-vr.5

The full stationary OpenXR ReShade menu now supports DX11, DX12, Vulkan and
Windows desktop OpenGL, including VDXR and other OpenXR runtimes. The existing
SteamVR/OpenVR menu remains unchanged.

- Press F8 or hold both thumbstick clicks for three seconds to toggle.
  Vive wands use both trackpad clicks. Release both clicks before repeating.
- Right-controller pointing/trigger selection and stick/pad scrolling remain.
- The menu stays at its opening position and does not follow head movement.
- Game buttons/movement, keyboard, mouse and XInput are blocked while open;
  tracking poses remain active. Focus loss or menu failure restores input.
- Deployment uses the existing capture API layer and the game-local adapter,
  preserving graphics owners and avoiding a duplicate ReShade XR layer.
- Vulkan/OpenGL transfer only the independently rendered panel image, with
  queue/image/fence/context handling and correct channel/orientation conversion.
- Includes late-XR deployment, explicit VR-Off configuration, controller priority
  context handling, and original-sync fallback on input augmentation failure.

Repair/reinstall games from vr.5 to receive the new adapter and shared layer.
Close VR games before updating the shared layer. Keep the working DLSS/provider
settings; these menu changes do not add new foveation or engine-integrated AA.

Validation: 97 application tests, six VR/install suites, actual modified-ReShade
DX11/DX12 menu interaction/readback tests, real Vulkan/OpenGL upload/readback,
controller hold/priority/input tests, and actual capture DLL dispatch across
all four graphics bindings with exactly one appended quad. Archive version,
critical hashes, CRC and upload digest are verified before publication.

This is an experimental prerelease. New Vulkan/OpenGL and physical controller
shortcut paths still need end-to-end headset confirmation. Unsupported graphics
formats/profiles/layer capacity retain explicit fallback behavior.
