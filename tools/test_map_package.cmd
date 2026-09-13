@echo off
setlocal
pushd "%~dp0.."
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /I vcpkg_installed\x86-windows-static\include tests\MapPackageTests.cpp Reloaded.Editor\MapPackage.cpp Reloaded.Editor\RecoveredAssetPackage.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\MapPackageTests.exe" /link /LIBPATH:vcpkg_installed\x86-windows-static\lib zlib.lib
if errorlevel 1 goto failed
"%TEMP%\MapPackageTests.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
