@echo off
setlocal
rem Run from an x86 Native Tools Command Prompt for Visual Studio.
pushd "%~dp0.."
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\BrushGridSnapTests.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\BrushGridSnapTests.exe"
if errorlevel 1 goto failed
"%TEMP%\BrushGridSnapTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\MapAuthoringModelTests.cpp Reloaded.Editor\WorkflowModel.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\MapAuthoringModelTests.exe"
if errorlevel 1 goto failed
"%TEMP%\MapAuthoringModelTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\CameraNetworkModelTests.cpp Reloaded.Editor\WorkflowModel.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\CameraNetworkModelTests.exe"
if errorlevel 1 goto failed
"%TEMP%\CameraNetworkModelTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\MagicEventModelTests.cpp Reloaded.Editor\WorkflowModel.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\MagicEventModelTests.exe"
if errorlevel 1 goto failed
"%TEMP%\MagicEventModelTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\WorkflowModelTests.cpp Reloaded.Editor\WorkflowModel.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\WorkflowModelTests.exe"
if errorlevel 1 goto failed
"%TEMP%\WorkflowModelTests.exe"
if errorlevel 1 goto failed
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 tests\WorkflowGraphTests.cpp Reloaded.Editor\WorkflowModel.cpp /Fo"%TEMP%\\" /Fe"%TEMP%\WorkflowGraphTests.exe"
if errorlevel 1 goto failed
"%TEMP%\WorkflowGraphTests.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
