@echo off
setlocal
pushd "%~dp0.."
rem An older checkout's vcpkg install is vcpkg_installed\<triplet>; a fresh
rem one nests the triplet twice.
set ZLIB=vcpkg_installed\x86-windows-static
if not exist %ZLIB%\include\zlib.h set ZLIB=vcpkg_installed\x86-windows-static\x86-windows-static
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /I %ZLIB%\include tests\MapPackageTests.cpp Reloaded.Editor\MapPackage.cpp Reloaded.Editor\RecoveredAssetPackage.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\MapPackageTests.exe" /link /LIBPATH:%ZLIB%\lib zlib.lib
if errorlevel 1 goto failed
"%TEMP%\MapPackageTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /I %ZLIB%\include tests\MapUsagesFileTests.cpp Reloaded.Editor\MapUsages.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\MapUsagesFileTests.exe" /link /LIBPATH:%ZLIB%\lib zlib.lib
if errorlevel 1 goto failed
"%TEMP%\MapUsagesFileTests.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
