@echo off
rem Keep the compiler environment in the caller's setlocal scope.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    where cl.exe >nul 2>nul
    if not errorlevel 1 exit /b 0
)
set "VR_VS_INSTALL="
set "VR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VR_VSWHERE%" (
    echo Install Visual Studio 2022 or Build Tools with Desktop development with C++ and a Windows SDK.
    exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VR_VS_INSTALL=%%I"
if not defined VR_VS_INSTALL (
    echo No Visual Studio installation with the x64 C++ toolchain was found.
    exit /b 1
)
call "%VR_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
