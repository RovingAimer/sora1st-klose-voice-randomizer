@echo off
setlocal
call "%~dp0vc_env.cmd"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
if not exist bin mkdir bin
pushd build
cl /nologo /c /O2 /MT /W3 /I"..\third_party\minhook-1.3.4\include" /I"..\third_party\minhook-1.3.4\src" "..\third_party\minhook-1.3.4\src\buffer.c" "..\third_party\minhook-1.3.4\src\hook.c" "..\third_party\minhook-1.3.4\src\trampoline.c" "..\third_party\minhook-1.3.4\src\hde\hde64.c"
if errorlevel 1 exit /b 1
lib /nologo /OUT:minhook.lib buffer.obj hook.obj trampoline.obj hde64.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /LD /I"..\third_party\minhook-1.3.4\include" "..\src\plugin.cpp" "..\src\proxy.cpp" "..\src\overlay.cpp" "..\src\overlay_renderer.cpp" minhook.lib /link /DEF:"..\src\xinput.def" /OUT:"..\bin\xinput1_4.dll"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /I"..\third_party\minhook-1.3.4\include" "..\src\tests.cpp" "..\src\overlay.cpp" "..\src\overlay_renderer.cpp" minhook.lib /link /OUT:"..\bin\vr_tests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 "..\src\overlay_tests.cpp" "..\src\overlay.cpp" "..\src\overlay_renderer.cpp" /link /OUT:"..\bin\overlay_tests.exe"
if errorlevel 1 exit /b 1
popd
popd
call "%~dp0build_editor.cmd"
if errorlevel 1 exit /b 1
exit /b 0
