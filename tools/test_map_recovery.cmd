@echo off
setlocal
rem Run from an x86 Native Tools Command Prompt for Visual Studio.
pushd "%~dp0.."
cl /nologo /std:c++17 /W4 /WX /EHsc /O2 tests\RecoveredBspGeometryTests.cpp Reloaded.Editor\RecoveredBspGeometry.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\RecoveredBspGeometryTests.exe"
if errorlevel 1 goto failed
"%TEMP%\RecoveredBspGeometryTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++17 /W4 /WX /EHsc /O2 tests\RecoveredActorImportTests.cpp Reloaded.Editor\RecoveredActorImport.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\RecoveredActorImportTests.exe"
if errorlevel 1 goto failed
"%TEMP%\RecoveredActorImportTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++17 /W4 /WX /EHsc /O2 tests\RecoveredSurfacePartitionTests.cpp Reloaded.Editor\RecoveredSurfacePartition.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\RecoveredSurfacePartitionTests.exe"
if errorlevel 1 goto failed
"%TEMP%\RecoveredSurfacePartitionTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++17 /W4 /WX /EHsc /O2 /I vcpkg_installed\x86-windows-static\include tests\RecoveredAssetPackageTests.cpp Reloaded.Editor\RecoveredAssetPackage.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\RecoveredAssetPackageTests.exe" /link /LIBPATH:vcpkg_installed\x86-windows-static\lib zlib.lib
if errorlevel 1 goto failed
"%TEMP%\RecoveredAssetPackageTests.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
