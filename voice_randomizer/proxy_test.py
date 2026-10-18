"""Compare the proxy with the real system XInput DLL, without touching a controller."""
import ctypes as C
from pathlib import Path
k=C.WinDLL('kernel32')
path=C.create_unicode_buffer(32768)
assert k.GetSystemDirectoryW(path,len(path))
system=C.WinDLL(str(Path(path.value)/'xinput1_4.dll'))
proxy=C.WinDLL(str(Path(__file__).parent/'bin/xinput1_4.dll'))
for name,types,args in [
    ('XInputGetState',[C.c_uint32,C.c_void_p],[999,C.create_string_buffer(64)]),
    ('XInputGetCapabilities',[C.c_uint32,C.c_uint32,C.c_void_p],[999,0,C.create_string_buffer(128)]),
    ('XInputGetBatteryInformation',[C.c_uint32,C.c_ubyte,C.c_void_p],[999,0,C.create_string_buffer(32)]),
]:
    original=getattr(system,name); forwarded=getattr(proxy,name)
    original.argtypes=forwarded.argtypes=types
    original.restype=forwarded.restype=C.c_uint32
    expected=original(*args); actual=forwarded(*args)
    assert actual==expected,(name,expected,actual)
    print('PASS:',name,'matches system DLL for invalid controller index:',actual)
