#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Forward the documented XInput 1.4 ABI, preserving the system DLL's ordinals.
struct State;
struct Vibration;
struct Capabilities;
struct Battery;
struct Keystroke;
namespace {
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
HMODULE real=nullptr;
BOOL CALLBACK load(PINIT_ONCE,PVOID,PVOID*) {
    wchar_t path[MAX_PATH]{};
    if (!GetSystemDirectoryW(path,MAX_PATH)) return FALSE;
    wcscat_s(path,L"\\xinput1_4.dll");
    real=LoadLibraryW(path);
    return real!=nullptr;
}
template<class F> F get(const char* name) {
    InitOnceExecuteOnce(&once,load,nullptr,nullptr);
    return real?reinterpret_cast<F>(GetProcAddress(real,name)):nullptr;
}
}
extern "C" {
void WINAPI ProxyEnable(BOOL b) { auto f=get<void(WINAPI*)(BOOL)>("XInputEnable"); if(f) f(b); }
DWORD WINAPI ProxyGetState(DWORD i,State* p) { auto f=get<DWORD(WINAPI*)(DWORD,State*)>("XInputGetState"); return f?f(i,p):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxySetState(DWORD i,Vibration* p) { auto f=get<DWORD(WINAPI*)(DWORD,Vibration*)>("XInputSetState"); return f?f(i,p):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyCapabilities(DWORD i,DWORD flags,Capabilities* p) { auto f=get<DWORD(WINAPI*)(DWORD,DWORD,Capabilities*)>("XInputGetCapabilities"); return f?f(i,flags,p):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyBattery(DWORD i,BYTE type,Battery* p) { auto f=get<DWORD(WINAPI*)(DWORD,BYTE,Battery*)>("XInputGetBatteryInformation"); return f?f(i,type,p):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyKeystroke(DWORD i,DWORD flags,Keystroke* p) { auto f=get<DWORD(WINAPI*)(DWORD,DWORD,Keystroke*)>("XInputGetKeystroke"); return f?f(i,flags,p):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyAudio(DWORD i,LPWSTR render,UINT* render_count,LPWSTR capture,UINT* capture_count) { auto f=get<DWORD(WINAPI*)(DWORD,LPWSTR,UINT*,LPWSTR,UINT*)>("XInputGetAudioDeviceIds"); return f?f(i,render,render_count,capture,capture_count):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyDSound(DWORD i,GUID* render,GUID* capture) { auto f=get<DWORD(WINAPI*)(DWORD,GUID*,GUID*)>("XInputGetDSoundAudioDeviceGuids"); return f?f(i,render,capture):ERROR_DEVICE_NOT_CONNECTED; }
DWORD WINAPI ProxyStateEx(DWORD i,State* p) { auto f=get<DWORD(WINAPI*)(DWORD,State*)>(MAKEINTRESOURCEA(100)); return f?f(i,p):ProxyGetState(i,p); }
}
