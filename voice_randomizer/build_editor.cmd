@echo off
setlocal
call "%~dp0vc_env.cmd"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate_config.ps1"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
if not exist bin mkdir bin
pushd build
rc /nologo /fo config_editor.res "..\src\config_editor.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE "..\src\config_editor.cpp" config_editor.res /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"..\src\config_editor.manifest" /OUT:"..\bin\KloseVoiceSettings.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE "..\src\editor_config_tests.cpp" /link /OUT:"..\bin\editor_config_tests.exe"
if errorlevel 1 exit /b 1
popd
popd
exit /b 0
