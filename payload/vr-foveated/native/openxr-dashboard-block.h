#pragma once
// Block game input while the headset panel owns interaction. Head tracking and
// frame submission continue. No system-wide keyboard/mouse capture is used.
#include <Xinput.h>
namespace vr_dashboard_block {
static std::atomic<bool> active{false};
static HMODULE module=nullptr;
static uintptr_t module_end=0;
static HWND window=nullptr;
static WNDPROC previous=nullptr;
using Async=SHORT (WINAPI *)(int);using Keys=BOOL (WINAPI *)(PBYTE);
static Async async_key=GetAsyncKeyState,key_state=GetKeyState;
static Keys keyboard=GetKeyboardState;
static bool hooked=false;
static bool Internal(void *caller){auto p=(uintptr_t)caller;return module&&p>=(uintptr_t)module&&p<module_end;}
static SHORT WINAPI AsyncKey(int key){if(active.load()&&!Internal(_ReturnAddress()))return 0;return async_key(key);}
static SHORT WINAPI Key(int key){if(active.load()&&!Internal(_ReturnAddress()))return 0;return key_state(key);}
static BOOL WINAPI Keyboard(PBYTE keys){if(keys&&active.load()&&!Internal(_ReturnAddress())){memset(keys,0,256);return TRUE;}return keyboard(keys);}
static LRESULT CALLBACK Window(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    bool input=(message>=WM_KEYFIRST&&message<=WM_KEYLAST)||(message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||message==WM_INPUT;
    if(active.load()&&input){if(message==WM_INPUT)return DefWindowProcW(hwnd,message,w,l);return 0;}
    auto next=previous;return next?CallWindowProcW(next,hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);
}
using Gamepad=DWORD (WINAPI *)(DWORD,XINPUT_STATE *);
static Gamepad gamepad[4]={};
template<int I>static DWORD WINAPI GamepadState(DWORD player,XINPUT_STATE *state){
    if(state&&active.load()&&!Internal(_ReturnAddress())){memset(state,0,sizeof(*state));return ERROR_SUCCESS;}
    return gamepad[I]?gamepad[I](player,state):ERROR_DEVICE_NOT_CONNECTED;
}
inline void Gamepads(){
    const wchar_t *names[]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll",L"xinput1_2.dll"};
    Gamepad hooks[]={GamepadState<0>,GamepadState<1>,GamepadState<2>,GamepadState<3>};
    for(int i=0;i<4;++i){if(gamepad[i])continue;auto dll=GetModuleHandleW(names[i]);auto fn=dll?(Gamepad)GetProcAddress(dll,"XInputGetState"):nullptr;if(!fn)continue;
        bool duplicate=false;for(int j=0;j<i;++j)if(gamepad[j]&&fn==(Gamepad)GetProcAddress(GetModuleHandleW(names[j]),"XInputGetState"))duplicate=true;if(duplicate)continue;
        gamepad[i]=fn;DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
        if(DetourAttach((PVOID *)&gamepad[i],(PVOID)hooks[i])!=NO_ERROR){DetourTransactionAbort();gamepad[i]=nullptr;}
        else if(DetourTransactionCommit()!=NO_ERROR)gamepad[i]=nullptr;
    }
}
inline void Init(HMODULE owner){
    module=owner;auto dos=(IMAGE_DOS_HEADER *)owner;auto nt=(IMAGE_NT_HEADERS *)((char *)owner+dos->e_lfanew);module_end=(uintptr_t)owner+nt->OptionalHeader.SizeOfImage;
    if(hooked)return;
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    if(DetourAttach((PVOID *)&async_key,(PVOID)AsyncKey)!=NO_ERROR||DetourAttach((PVOID *)&key_state,(PVOID)Key)!=NO_ERROR||DetourAttach((PVOID *)&keyboard,(PVOID)Keyboard)!=NO_ERROR){DetourTransactionAbort();return;}
    hooked=DetourTransactionCommit()==NO_ERROR;
}
inline void Set(bool enabled,HWND target){
    if(enabled&&target&&target!=window){
        if(window&&previous&&(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC)==Window)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous);
        window=target;SetLastError(0);auto old=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)Window);
        if(!old&&GetLastError()){window=nullptr;previous=nullptr;}else previous=old;
    }
    if(enabled)Gamepads();
    active=enabled;
}
inline void Clear(){
    active=false;
    if(window&&previous&&(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC)==Window)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous);
    window=nullptr;previous=nullptr;
    Gamepad hooks[]={GamepadState<0>,GamepadState<1>,GamepadState<2>,GamepadState<3>};
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    for(int i=0;i<4;++i)if(gamepad[i])DetourDetach((PVOID *)&gamepad[i],(PVOID)hooks[i]);
    DetourTransactionCommit();for(auto &fn:gamepad)fn=nullptr;
    if(!hooked)return;DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    DetourDetach((PVOID *)&async_key,(PVOID)AsyncKey);DetourDetach((PVOID *)&key_state,(PVOID)Key);DetourDetach((PVOID *)&keyboard,(PVOID)Keyboard);DetourTransactionCommit();hooked=false;
}
}
