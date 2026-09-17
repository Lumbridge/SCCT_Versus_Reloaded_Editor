param([Parameter(Mandatory=$true)][string]$SourceRoot,[Parameter(Mandatory=$true)][string]$WorkerRoot)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$worker = [IO.Path]::GetFullPath($WorkerRoot)
if (Test-Path -LiteralPath $worker) { throw 'Worker directory must be new.' }
$system = Join-Path $worker 'System'
$packages = Join-Path $worker 'Packages'
$null = New-Item -ItemType Directory -Path $system,$packages
Get-ChildItem -LiteralPath (Join-Path $source 'System') -File | Where-Object {
    $_.Extension -in '.exe','.dll','.ini','.int','.dat','.config','.bmp'
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $system }
foreach ($name in 'EditorRes','_PC_') {
    $path = Join-Path (Join-Path $source 'System') $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $system -Recurse }
}
foreach ($name in 'Animations','Sounds','Textures','StaticMeshes') {
    $path = Join-Path (Join-Path $source 'Packages') $name
    if (Test-Path -LiteralPath $path) {
        $null = New-Item -ItemType Junction -Path (Join-Path $packages $name) -Target $path
    }
}
if (Test-Path -LiteralPath (Join-Path $source 'Menus')) {
    $null = New-Item -ItemType Junction -Path (Join-Path $worker 'Menus') -Target (Join-Path $source 'Menus')
}
$null = New-Item -ItemType Directory -Path (Join-Path $packages 'Maps'),(Join-Path $packages 'MapsEd')
# Generated packages and maps are saved to explicit paths in the new run directory.
Write-Output 'Isolated editor prepared.'
