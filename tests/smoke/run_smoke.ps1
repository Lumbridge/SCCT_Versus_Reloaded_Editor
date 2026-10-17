param(
    [Parameter(Mandatory = $true)][string]$TestRoot,
    [string]$Map = '',
    [string]$EditorDll = '',
    [int]$TimeoutSeconds = 900,
    [hashtable]$Extra = @{},
    # Replace the test install's links into the real install with copies (once per test install).
    [switch]$Isolate,
    # Built by build_probe.cmd.
    [string]$ProbeDll = (Join-Path $env:TEMP 'scct-smoke\SmokeProbe.dll')
)
# Runs SmokeProbe.dll in the disposable editor under $TestRoot and prints its report.
$ErrorActionPreference = 'Stop'
$system = Join-Path $TestRoot 'System'
if ($Isolate) {
    foreach ($relative in 'Packages\Textures', 'Packages\Sounds', 'Packages\Animations', 'Menus') {
        $link = Join-Path $TestRoot $relative
        $item = Get-Item -LiteralPath $link -ErrorAction SilentlyContinue
        if (!$item -or $item.LinkType -ne 'Junction') { continue }
        $target = @($item.Target)[0]
        # Removes only the link, never what it points at.
        [IO.Directory]::Delete($link, $false)
        Copy-Item -LiteralPath $target -Destination $link -Recurse
        Write-Output "isolated $relative"
    }
}
# -PrepareOnly links Packages\Textures, Sounds, Animations and Menus to the real install, and
# saving a map writes its <Map>-i.utc through that link. Copy them first (see tests/smoke/README.md).
$links = Get-ChildItem -LiteralPath $TestRoot -Recurse -Attributes ReparsePoint -ErrorAction SilentlyContinue
if ($links) { throw "The test install still links into a real install ($($links[0].FullName)); run with -Isolate first." }
if (!(Test-Path -LiteralPath (Join-Path $system 'NativeMapRecoveryDriver.exe'))) { throw "No driver in $system" }
if ($EditorDll) { Copy-Item -LiteralPath $EditorDll -Destination (Join-Path $system 'Reloaded.Editor.dll') -Force }
Copy-Item -LiteralPath $ProbeDll -Destination (Join-Path $system 'SmokeProbe.dll') -Force
$mapPath = ''
if ($Map) { $mapPath = Join-Path (Join-Path $TestRoot 'Packages\MapsEd') $Map }
$ini = @("[smoke]", "map=$mapPath")
# Open Recent lists these maps, most recent first.
$recent = @()
if ($mapPath) { $recent += $mapPath }
if ($Extra.ContainsKey('map2')) { $recent += $Extra['map2'] }
if ($recent.Count) {
    $libraryDir = Join-Path $system 'ReloadedEditor'
    $null = New-Item -ItemType Directory -Force -Path $libraryDir
    ConvertTo-Json -InputObject @($recent) | Set-Content -LiteralPath (Join-Path $libraryDir 'recent_maps.json') -Encoding utf8
    $ini += 'open=recent'
}
foreach ($k in $Extra.Keys) { $ini += "$k=$($Extra[$k])" }
$ini | Set-Content -LiteralPath (Join-Path $system 'smoke.ini') -Encoding ascii
Remove-Item -LiteralPath (Join-Path $system 'smoke_report.txt') -ErrorAction SilentlyContinue
$editorExe = Join-Path $system 'ChaosTheory_Editor.exe'
$out = & (Join-Path $system 'NativeMapRecoveryDriver.exe') $editorExe (Join-Path $system 'Reloaded.Editor.dll') (Join-Path $system 'SmokeProbe.dll')
$editorPid = [int]($out | Select-Object -First 1)
Write-Output "editor pid $editorPid"
$process = Get-Process -Id $editorPid -ErrorAction SilentlyContinue
if ($process) {
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-Output "TIMEOUT after $TimeoutSeconds s; ending the disposable editor"
        Stop-Process -Id $editorPid -Force
    }
}
$reportPath = Join-Path $system 'smoke_report.txt'
if (Test-Path -LiteralPath $reportPath) { Get-Content -LiteralPath $reportPath } else { Write-Output 'NO REPORT' }
