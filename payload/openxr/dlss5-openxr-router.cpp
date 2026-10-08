#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <vector>

// Transparent OpenXR negotiation forwarder. Do not load add-ons or ReShade
// from DllMain: choose the process's existing ReShade owner during negotiation.
static HMODULE self_module;
using Negotiate = int32_t (__cdecl *)(const void *, const char *, void *);

static void LogOwner(HMODULE owner, bool reused, int32_t result)
{
    wchar_t logfile[32768] = {};
    if (!GetModuleFileNameW(nullptr, logfile, 32768)) return;
    wchar_t *slash = wcsrchr(logfile, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';
    if (wcscat_s(logfile, L"dlss5-openxr-router.log") != 0) return;
    char module[32768] = {}, message[33000] = {};
    if (owner) GetModuleFileNameA(owner, module, sizeof(module));
    const int length = snprintf(message, sizeof(message), "OpenXR router v2: owner=%s source=%s negotiation=%d\r\n",
        owner ? module : "unavailable", reused ? "existing-process-module" : "shared-fallback", result);
    HANDLE file = CreateFileW(logfile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (length > 0 && length < sizeof(message)) WriteFile(file, message, length, &written, nullptr);
        CloseHandle(file);
    }
}

static HMODULE ExistingReShade()
{
    HMODULE modules[1024]; DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return nullptr;
    const DWORD count = (bytes < sizeof(modules) ? bytes : sizeof(modules)) / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i)
        if (modules[i] != self_module &&
            GetProcAddress(modules[i], "ReShadeVersion") &&
            GetProcAddress(modules[i], "ReShadeRegisterAddon") &&
            GetProcAddress(modules[i], "xrNegotiateLoaderApiLayerInterface"))
            return modules[i];
    return nullptr;
}

// Some engines create OpenXR before their first graphics device. Load their
// installed local ReShade during negotiation to keep one module owner.
static HMODULE EarlyLocalReShade()
{
    wchar_t local[32768] = {};
    if(!GetModuleFileNameW(nullptr,local,32768))return nullptr;
    wchar_t *slash=wcsrchr(local,L'\\');if(!slash)return nullptr;slash[1]=0;
    const size_t base=wcslen(local);
    for(const wchar_t *proxy : {L"dxgi.dll",L"d3d11.dll",L"d3d12.dll"}) {
        local[base]=0;if(wcscat_s(local,proxy)!=0)continue;
        DWORD ignored=0,size=GetFileVersionInfoSizeW(local,&ignored);if(!size)continue;
        std::vector<unsigned char> data(size);if(!GetFileVersionInfoW(local,0,size,data.data()))continue;
        struct Translation { WORD language,codepage; } *translations=nullptr;UINT bytes=0;
        if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",(void **)&translations,&bytes))continue;
        bool valid=false;
        for(UINT i=0;i<bytes/sizeof(Translation);++i){
            wchar_t query[100],*product=nullptr;UINT chars=0;
            swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\ProductName",translations[i].language,translations[i].codepage);
            if(VerQueryValueW(data.data(),query,(void **)&product,&chars)&&chars&&product&&!_wcsicmp(product,L"ReShade")){valid=true;break;}
        }
        if(!valid)continue;
        HMODULE module=LoadLibraryW(local);
        if(module&&GetProcAddress(module,"ReShadeRegisterAddon")&&GetProcAddress(module,"xrNegotiateLoaderApiLayerInterface"))return module;
        if(module)FreeLibrary(module);
    }
    return nullptr;
}

extern "C" __declspec(dllexport) int32_t __cdecl xrNegotiateLoaderApiLayerInterface(const void *loader, const char *name, void *request)
{
    HMODULE owner = ExistingReShade();
    if(!owner)owner=EarlyLocalReShade();
    const bool reused = owner != nullptr;
    if (owner) {
        HMODULE retained = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(GetProcAddress(owner, "xrNegotiateLoaderApiLayerInterface")), &retained))
            return -6;
        owner = retained;
    }
    if (!owner)
    {
        wchar_t fallback[32768] = {};
        const DWORD len = GetModuleFileNameW(self_module, fallback, 32768);
        if (len == 0 || len >= 32768) return -6; // XR_ERROR_INITIALIZATION_FAILED
        wchar_t *slash = wcsrchr(fallback, L'\\');
        if (!slash) return -6;
        *(slash + 1) = L'\0';
        if (wcscat_s(fallback, L"ReShade64.dll") != 0) return -6;
        owner = LoadLibraryW(fallback);
        // Keep the owner loaded for the lifetime of the layer's function table.
    }
    if (!owner || owner == self_module) { LogOwner(nullptr,reused,-6); return -6; }
    // Select the compatibility layer beside this game's ReShade owner. A shared
    // fallback does not acquire another game's add-ons.
    if (reused) {
        wchar_t local[32768] = {};
        if (GetModuleFileNameW(owner, local, 32768)) {
            wchar_t *slash = wcsrchr(local, L'\\');
            if (slash) {
                *(slash + 1) = 0;
                if (wcscat_s(local, L"dlss5-vr-compat.addon64") == 0 && GetFileAttributesW(local) != INVALID_FILE_ATTRIBUTES) {
                    HMODULE adapter = LoadLibraryW(local);
                    auto forward = adapter ? reinterpret_cast<Negotiate>(GetProcAddress(adapter, "xrNegotiateLoaderApiLayerInterface")) : nullptr;
                    if (forward) { const int32_t result = forward(loader, name, request); LogOwner(owner, true, result); return result; }
                }
            }
        }
    }
    auto negotiate = reinterpret_cast<Negotiate>(GetProcAddress(owner, "xrNegotiateLoaderApiLayerInterface"));
    const int32_t result = negotiate ? negotiate(loader, name, request) : -6;
    LogOwner(owner,reused,result);
    return result;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
