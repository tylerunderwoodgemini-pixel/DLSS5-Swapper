#include <windows.h>
#include <cstdint>
#include <cstring>
extern "C" {
__declspec(dllexport) const char *ReShadeVersion = "router test";
__declspec(dllexport) bool ReShadeRegisterAddon(void *, uint32_t) { return true; }
__declspec(dllexport) int32_t xrNegotiateLoaderApiLayerInterface(const void *loader, const char *name, void *request) {
    return name && !strcmp(name,"XR_APILAYER_reshade") && loader == reinterpret_cast<const void *>(1) && request == reinterpret_cast<void *>(2) ? TEST_RESULT : -999;
}
}
