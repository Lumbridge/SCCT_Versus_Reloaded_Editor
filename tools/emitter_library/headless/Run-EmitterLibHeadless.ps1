# Headless job test of a feature-branch Reloaded.Editor.dll, adapted from
# SCCT_Versus_Reloaded_Editor_mapjson/out/mapjson_v2_test/Run-Headless.ps1 and SCCT_Versus_UE5_Bridge/tools/ue2build/Build-Ue2Map.ps1.
#
#   pwsh -NoProfile -ExecutionPolicy Bypass -File Run-EmitterLibHeadless.ps1 -EditorDll <worktree>\out\bin\Reloaded.Editor.dll -Job job_emitterlib.txt
#
# A disposable copy of the editor is made under %TEMP%\scct-emitterlib-<guid>: System files are copies, Packages\Maps and
# MapsEd are empty real folders, and the read-only asset folders are junctions to the UE Upgrade TEST copy (never the live
# "...4.0 Public Beta"). -CopyTextures makes Packages\Textures a real copy (use it if any step saves a texture package).
# Every top-level editor window is kept at -30000,-30000 by the probe, so nothing appears over other programs.
# Job placeholders: {MAPSED} disposable MapsEd\EmitterLibTest.sdc, {ROOT} disposable root, {OUT} OutDir (forward slashes),
# {SOURCE} the -SourceMap copy inside the disposable MapsEd.
param(
    [Parameter(Mandatory = $true)][string]$EditorDll,
    [Parameter(Mandatory = $true)][string]$Job,
    [string]$GameRoot = 'C:\Users\ryans\Desktop\Enhanced SCCT Versus 4.0 Public Beta - UE Upgrade',
    [string]$EditorRepo = 'C:\Users\ryans\source\repos\SCCT_Versus_Reloaded_Editor',
    [string]$Probe = "$PSScriptRoot\probe_emitterpacks.cpp",
    [string]$OutDir = "$PSScriptRoot\..\..\..\out\tools\run_headless",
    [string]$SourceMap,
    [int]$TimeoutSeconds = 600,
    [switch]$CopyTextures,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$live = 'C:\Users\ryans\Desktop\Enhanced SCCT Versus 4.0 Public Beta'
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path.TrimEnd('\')
if ($GameRoot -ieq $live) { throw 'Refusing to use the live installation; use the UE Upgrade test copy.' }
$EditorDll = (Resolve-Path -LiteralPath $EditorDll).Path
$null = New-Item -ItemType Directory -Force -Path $OutDir
Get-ChildItem -LiteralPath $OutDir -File | Remove-Item -Force
$installedSystem = Join-Path $GameRoot 'System'
$root = Join-Path $env:TEMP ('scct-emitterlib-' + [Guid]::NewGuid().ToString('N'))
$system = Join-Path $root 'System'
$packages = Join-Path $root 'Packages'
$null = New-Item -ItemType Directory -Path $system, $packages
$junctions = [System.Collections.Generic.List[string]]::new()
$editorId = 0
try {
    Get-ChildItem -LiteralPath $installedSystem -File | Where-Object {
        $_.Extension -in '.exe', '.dll', '.ini', '.int', '.dat', '.config', '.bmp', '.u'
    } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $system }
    foreach ($name in 'EditorRes', '_PC_') {
        $source = Join-Path $installedSystem $name
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $system -Recurse }
    }
    $linked = @('Animations', 'Sounds', 'StaticMeshes')
    if ($CopyTextures) { Copy-Item -LiteralPath (Join-Path $GameRoot 'Packages\Textures') -Destination $packages -Recurse }
    else { $linked += 'Textures' }
    foreach ($name in $linked) {
        $link = Join-Path $packages $name
        $null = New-Item -ItemType Junction -Path $link -Target (Join-Path (Join-Path $GameRoot 'Packages') $name)
        $junctions.Add($link)
    }
    if (Test-Path -LiteralPath (Join-Path $GameRoot 'Menus')) {
        $link = Join-Path $root 'Menus'
        $null = New-Item -ItemType Junction -Path $link -Target (Join-Path $GameRoot 'Menus')
        $junctions.Add($link)
    }
    $null = New-Item -ItemType Directory -Path (Join-Path $packages 'Maps'), (Join-Path $packages 'MapsEd')
    $mapsEd = Join-Path $packages 'MapsEd\EmitterLibTest.sdc'
    $sourceCopy = ''
    if ($SourceMap) {
        $sourceCopy = Join-Path $packages ('MapsEd\' + [IO.Path]::GetFileName($SourceMap))
        Copy-Item -LiteralPath (Resolve-Path -LiteralPath $SourceMap).Path -Destination $sourceCopy
    }
    # The DLL under test replaces the copied one, in the disposable folder only.
    $editorDllCopy = Join-Path $system 'Reloaded.Editor.dll'
    Copy-Item -LiteralPath $EditorDll -Destination $editorDllCopy -Force
    $outSlash = (Resolve-Path $OutDir).Path -replace '\\', '/'
    $jobText = (Get-Content -LiteralPath $Job -Raw).Replace('{MAPSED}', $mapsEd).Replace('{ROOT}', $root).Replace('{OUT}', $outSlash).Replace('{SOURCE}', $sourceCopy)
    Set-Content -LiteralPath (Join-Path $system 'ue2build_job.txt') -Value $jobText -Encoding ascii
    "[job]`r`neditor_dll=$editorDllCopy`r`n" | Set-Content -LiteralPath (Join-Path $system 'ue2build.ini') -Encoding ascii

    $visualStudio = 'C:\Program Files\Microsoft Visual Studio\18\Insiders'
    $include = Join-Path $EditorRepo 'Reloaded.Editor\Include'
    $compile = Join-Path $system 'compile.cmd'
    @"
    @call "$visualStudio\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
    @cl /nologo /W3 /EHsc /std:c++20 /MT /O2 "$EditorRepo\tests\NativeMapRecoveryDriver.cpp" /Fe:"$system\ue2build_driver.exe" /Fo:"$system\ue2build_driver.obj" /link user32.lib
    @if errorlevel 1 exit /b 1
    @cl /nologo /W3 /EHsc /std:c++20 /MT /O2 /LD /I"$include" "$Probe" /Fe:"$system\ue2build_probe.dll" /Fo:"$system\ue2build_probe.obj" /link user32.lib gdi32.lib
    @exit /b %errorlevel%
"@ | Set-Content -LiteralPath $compile -Encoding ascii
    & cmd.exe /d /c $compile
    if ($LASTEXITCODE) { throw "Probe build failed ($root)" }

    $editor = Join-Path $system 'ChaosTheory_Editor.exe'
    $report = Join-Path $system 'ue2build_report.txt'
    $launched = & (Join-Path $system 'ue2build_driver.exe') $editor $editorDllCopy (Join-Path $system 'ue2build_probe.dll')
    if ($LASTEXITCODE) { throw "Editor launch failed ($root)" }
    $editorId = [int]($launched | Select-Object -Last 1)
    Write-Output "Root=$root EditorPid=$editorId"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $streamed = 0
    $result = $null
    try {
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $report) {
                $lines = @(Get-Content -LiteralPath $report)
                for ($i = $streamed; $i -lt $lines.Count; $i++) {
                    $line = $lines[$i]
                    Write-Output $(if ($line.Length -gt 400) { $line.Substring(0, 400) + ' ...' } else { $line })
                }
                $streamed = $lines.Count
                $hit = $lines | Where-Object { $_ -match '^(PASS|FAIL) ' } | Select-Object -Last 1
                if ($hit) { $result = $hit; break }
            }
            if (!(Get-Process -Id $editorId -ErrorAction SilentlyContinue)) {
                Start-Sleep -Milliseconds 500
                if (Test-Path -LiteralPath $report) { $result = (Get-Content -LiteralPath $report | Where-Object { $_ -match '^(PASS|FAIL) ' } | Select-Object -Last 1) }
                if (!$result) { $result = 'FAIL editor exited before finishing' }
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (!$result) { $result = 'FAIL timed out' }
    } finally {
        $remaining = Get-Process -Id $editorId -ErrorAction SilentlyContinue
        if ($remaining -and $remaining.Path -eq $editor) { Stop-Process -Id $editorId -Force; $remaining.WaitForExit(10000) | Out-Null }
    }
    Copy-Item -LiteralPath $report -Destination (Join-Path $OutDir 'report.txt') -Force -ErrorAction SilentlyContinue
    $log = Get-ChildItem -LiteralPath $system -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($log) { Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $OutDir 'editor.log') -Force }
    $reloaded = Join-Path $system 'ReloadedEditor'
    if (Test-Path -LiteralPath $reloaded) { Copy-Item -LiteralPath $reloaded -Destination (Join-Path $OutDir 'ReloadedEditor') -Recurse -Force }
    foreach ($f in $mapsEd, (Join-Path $packages 'Maps\EmitterLibTest.sdc')) {
        if (Test-Path -LiteralPath $f) { Write-Output ("output {0} {1} bytes" -f $f, (Get-Item $f).Length) }
    }
    $crash = Get-ChildItem -LiteralPath (Join-Path $system 'Diagnostics') -Filter 'EditorCrash_*.log' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($crash) { Write-Output "crash log: $($crash.FullName)"; Get-Content -LiteralPath $crash.FullName -TotalCount 10 }
    Write-Output "RESULT $result"
} finally {
    # Unlink the junctions (the links only, never their targets) before anything could recurse into them.
    foreach ($link in $junctions) { if (Test-Path -LiteralPath $link) { [IO.Directory]::Delete($link, $false) } }
    if ($Clean -and $editorId -and !(Get-Process -Id $editorId -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        Write-Output "Disposable root removed: $root"
    } else { Write-Output "Disposable root kept (junctions removed): $root" }
}
