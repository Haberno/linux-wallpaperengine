@echo off
rem Requires existing x64 Native Tools prompt, Windows SDK, and Python 3.
rem Compilation and local manifest creation only; never executes the probe.
cd /d "%~dp0"
python build_manifest.py begin build --recipe msvc
if errorlevel 1 exit /b 1
cl /nologo /TC /std:c11 /O1 /W4 /D_CRT_SECURE_NO_WARNINGS mediaengine_readback.c /Fobuild\mediaengine_readback.obj /Febuild\mediaengine_readback.exe /link d3d11.lib dxgi.lib dxguid.lib mfplat.lib mfuuid.lib uuid.lib ole32.lib oleaut32.lib user32.lib shell32.lib > build\build.log 2>&1
if errorlevel 1 exit /b 1
python build_manifest.py finish build --recipe msvc
exit /b %errorlevel%
