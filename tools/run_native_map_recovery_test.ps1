param(
    [Parameter(Mandatory = $true)][string]$EditorDll,
    [string]$SourceMap,
    [string]$GameSystem = $env:SCCT,
    [string]$StartupConfigSystem,
    [int]$TimeoutSeconds = 300,
    [switch]$PrepareOnly,
    [switch]$GenerateFixture,
    [switch]$ImportBaseline,
    [switch]$ReopenOnly,
    [switch]$RootOutside,
    [switch]$ExpectRecoveryFailure,
    [switch]$EditGeometry,
    [switch]$ImportTextOnly,
    [switch]$CompactPoints,
    [string[]]$ExtraAssetPackage = @()
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$nativeDll = (Resolve-Path -LiteralPath $EditorDll).Path
if ($ImportBaseline -and !$GenerateFixture) { throw 'ImportBaseline requires GenerateFixture.' }
if ($ImportTextOnly -and ($GenerateFixture -or $ReopenOnly -or $ExpectRecoveryFailure -or $EditGeometry)) { throw 'ImportTextOnly requires a standalone T3D input.' }
if ($ReopenOnly -and $GenerateFixture) { throw 'ReopenOnly requires an existing source map.' }
if ($EditGeometry -and ($ReopenOnly -or $ImportBaseline -or $ExpectRecoveryFailure)) { throw 'EditGeometry requires a successful recovery test.' }
if ($RootOutside -and !$GenerateFixture) { throw 'RootOutside requires GenerateFixture.' }
if ($ExpectRecoveryFailure -and ($ReopenOnly -or $ImportBaseline)) { throw 'ExpectRecoveryFailure requires a recovery test.' }
if ($ExpectRecoveryFailure -and !$GenerateFixture) { throw 'ExpectRecoveryFailure requires GenerateFixture for the normal-load follow-up.' }
if (!$GenerateFixture -and !$SourceMap) { throw 'Supply SourceMap or GenerateFixture.' }
$nativeSource = if ($GenerateFixture) { $null } else { (Resolve-Path -LiteralPath $SourceMap).Path }
$installedSystem = (Resolve-Path -LiteralPath $GameSystem).Path
$installedRoot = Split-Path -Parent $installedSystem
$testRoot = Join-Path $env:TEMP ('scct-source-recovery-' + [Guid]::NewGuid().ToString('N'))
$testSystem = Join-Path $testRoot 'System'
$testPackages = Join-Path $testRoot 'Packages'
$null = New-Item -ItemType Directory -Path $testSystem, $testPackages

# Every executable, DLL, configuration, source map and output map is a copy.
# Only immutable asset directories are shared; no editor command targets them.
Get-ChildItem -LiteralPath $installedSystem -File | Where-Object {
    $_.Extension -in '.exe', '.dll', '.ini', '.int', '.dat', '.config', '.bmp'
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $testSystem }
if ($StartupConfigSystem) {
    Get-ChildItem -LiteralPath (Resolve-Path -LiteralPath $StartupConfigSystem).Path -File -Filter '*.ini' |
        Where-Object { $_.Name -ne 'native_recovery_test.ini' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $testSystem -Force }
}
foreach ($name in 'EditorRes', '_PC_') {
    if (Test-Path -LiteralPath (Join-Path $installedSystem $name)) {
        Copy-Item -LiteralPath (Join-Path $installedSystem $name) -Destination $testSystem -Recurse
    }
}
foreach ($name in 'Animations', 'Sounds', 'Textures') {
    $assetSource = Join-Path (Join-Path $installedRoot 'Packages') $name
    if (Test-Path -LiteralPath $assetSource) {
        $null = New-Item -ItemType Junction -Path (Join-Path $testPackages $name) -Target $assetSource
    }
}
# Recovery may emit a new external mesh package. Give that directory its own
# files so a test can neither replace nor add anything to the user's assets.
$meshSource = Join-Path (Join-Path $installedRoot 'Packages') 'StaticMeshes'
if (Test-Path -LiteralPath $meshSource) {
    Copy-Item -LiteralPath $meshSource -Destination $testPackages -Recurse
}
foreach ($assetPackage in $ExtraAssetPackage) {
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $assetPackage).Path -Destination (Join-Path $testPackages 'StaticMeshes')
}
if (Test-Path -LiteralPath (Join-Path $installedRoot 'Menus')) {
    $null = New-Item -ItemType Junction -Path (Join-Path $testRoot 'Menus') -Target (Join-Path $installedRoot 'Menus')
}
$inputMaps = Join-Path $testPackages 'Maps'
$outputMaps = Join-Path $testPackages 'MapsEd'
$null = New-Item -ItemType Directory -Path $inputMaps, $outputMaps
$inputMap = Join-Path $(if ($ReopenOnly) { $outputMaps } else { $inputMaps }) $(if ($GenerateFixture) { 'NativeFixture.sdc' } else { [IO.Path]::GetFileName($nativeSource) })
$outputMap = Join-Path $outputMaps 'NativeRecovered_Source.sdc'
if ($ExpectRecoveryFailure) {
    # Fail geometry interchange writing after cooked loading/reconstruction.
    # The input package itself remains a valid native map.
    $null = New-Item -ItemType Directory -Path (Join-Path $outputMaps 'Recovery\NativeRecovered_Source\Geometry.t3d') -Force
}
if (!$GenerateFixture) { Copy-Item -LiteralPath $nativeSource -Destination $inputMap }
$injectedDll = Join-Path $testSystem 'Reloaded.Editor.dll'
Copy-Item -LiteralPath $nativeDll -Destination $injectedDll -Force

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) { throw 'Visual Studio C++ tools are required for the native test.' }
$devCommand = Join-Path $visualStudio 'Common7/Tools/VsDevCmd.bat'
$compileScript = Join-Path $testSystem 'compile_test.cmd'
@"
@call "$devCommand" -arch=x86 -host_arch=x64 >nul
@if errorlevel 1 exit /b 1
@cl /nologo /W4 /EHsc /std:c++20 /MT /O2 "$repository\tests\NativeMapRecoveryDriver.cpp" /Fe:"$testSystem\NativeMapRecoveryDriver.exe" /Fo:"$testSystem\NativeMapRecoveryDriver.obj" /link user32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /W4 /EHsc /std:c++20 /MT /O2 /LD "$repository\tests\NativeMapRecoveryProbe.cpp" /Fe:"$testSystem\NativeMapRecoveryProbe.dll" /Fo:"$testSystem\NativeMapRecoveryProbe.obj" /link user32.lib
@exit /b %errorlevel%
"@ | Set-Content -LiteralPath $compileScript -Encoding ascii
& cmd.exe /d /c $compileScript
if ($LASTEXITCODE) { throw "Native test build failed; files retained at $testRoot" }
@"
[test]
source=$inputMap
destination=$outputMap
editor_dll=$injectedDll
generate_fixture=$([int][bool]$GenerateFixture)
import_baseline=$([int][bool]$ImportBaseline)
reopen_only=$([int][bool]$ReopenOnly)
root_outside=$([int][bool]$RootOutside)
expect_recovery_failure=$([int][bool]$ExpectRecoveryFailure)
edit_geometry=$([int][bool]$EditGeometry)
import_text_only=$([int][bool]$ImportTextOnly)
compact_points=$([int][bool]$CompactPoints)
"@ | Set-Content -LiteralPath (Join-Path $testSystem 'native_recovery_test.ini') -Encoding ascii
Write-Output "TestRoot=$testRoot"
if ($PrepareOnly) { return }

$testExecutable = Join-Path $testSystem 'ChaosTheory_Editor.exe'
$driver = Join-Path $testSystem 'NativeMapRecoveryDriver.exe'
$probe = Join-Path $testSystem 'NativeMapRecoveryProbe.dll'
$launchedId = & $driver $testExecutable $injectedDll $probe
if ($LASTEXITCODE) { throw "Isolated editor launch failed; files retained at $testRoot" }
$editorProcessId = [int]($launchedId | Select-Object -Last 1)
$report = Join-Path $testSystem 'native_recovery_report.txt'
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $report) {
            $result = Get-Content -LiteralPath $report -Raw
            if ($result -match '(?m)^(PASS|FAIL) ') {
                Write-Output $result
                if ($result -match '(?m)^FAIL ') { throw 'Native map recovery test failed.' }
                return
            }
        }
        if (!(Get-Process -Id $editorProcessId -ErrorAction SilentlyContinue)) {
            throw 'Isolated editor exited before finishing; inspect retained diagnostics.'
        }
        $crash = Get-ChildItem -LiteralPath (Join-Path $testSystem 'Diagnostics') -Filter 'EditorCrash_*.log' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($crash) {
            # The report is created before MiniDumpWriteDump runs. Allow the
            # diagnostic writer to finish before terminating this process.
            $dumpDeadline = [DateTime]::UtcNow.AddSeconds(30)
            while ([DateTime]::UtcNow -lt $dumpDeadline) {
                if ((Get-Content -LiteralPath $crash.FullName -Raw) -match '(?m)^MiniDump=') { break }
                if (!(Get-Process -Id $editorProcessId -ErrorAction SilentlyContinue)) { break }
                Start-Sleep -Milliseconds 500
            }
            Get-Content -LiteralPath $crash.FullName -TotalCount 10
            throw 'Isolated editor crashed; native report and dump retained.'
        }
        Start-Sleep -Milliseconds 500
    }
    if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report }
    throw 'Native map recovery test timed out; inspect retained diagnostics.'
} finally {
    $remaining = Get-Process -Id $editorProcessId -ErrorAction SilentlyContinue
    # The process is disposable. Verify its actual executable before stopping it;
    # this never closes an already-running user editor or flushes its config.
    if ($remaining -and $remaining.Path -eq $testExecutable) {
        Stop-Process -Id $editorProcessId -Force
    }
}
