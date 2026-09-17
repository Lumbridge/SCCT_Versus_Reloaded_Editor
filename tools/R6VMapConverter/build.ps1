param(
    [string]$Output,
    [Parameter(Mandatory=$true)][string]$ScctRoot,
    [Parameter(Mandatory=$true)][string]$Umodel,
    [string]$PythonRoot
)
$ErrorActionPreference = 'Stop'
if (!$Output) { $Output = Join-Path $PSScriptRoot '../../build/R6VMapConverter' }
$out = [IO.Path]::GetFullPath($Output)
$null = New-Item -ItemType Directory -Force -Path $out,(Join-Path $PSScriptRoot 'Native/bin')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x86 build tools are required.' }
$native = Join-Path $PSScriptRoot 'Native'
$script = Join-Path $native 'bin/compile.cmd'
@"
@call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
@if errorlevel 1 exit /b 1
@cl /nologo /EHsc /std:c++20 /MT /O2 "$native\ImportDriver.cpp" /Fe:"$native\bin\ImportDriver.exe" /Fo:"$native\bin\ImportDriver.obj" /link user32.lib
@if errorlevel 1 exit /b 1
@cl /nologo /EHsc /std:c++20 /MT /O2 /LD "$native\ImportWorker.cpp" /Fe:"$native\bin\ImportWorker.dll" /Fo:"$native\bin\ImportWorker.obj" /link user32.lib gdi32.lib comdlg32.lib
@exit /b %errorlevel%
"@ | Set-Content -LiteralPath $script -Encoding ascii
& cmd.exe /d /c $script
if ($LASTEXITCODE) { throw 'Native worker build failed.' }
& dotnet publish "$PSScriptRoot/App/R6VMapConverter.csproj" -c Release -r win-x64 --self-contained true -o $out --nologo
if ($LASTEXITCODE) { throw '.NET publish failed.' }
foreach ($name in 'ImportDriver.exe','ImportWorker.dll') { Copy-Item -LiteralPath (Join-Path $native "bin/$name") -Destination (Join-Path $out 'Native') -Force }
# Reject unverified builds instead of blessing arbitrary engine addresses.
$hashes = Get-Content -LiteralPath (Join-Path $native 'compatibility.json') -Raw | ConvertFrom-Json
foreach ($property in $hashes.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $ScctRoot "System/$($property.Name)") -Algorithm SHA256).Hash -ne $property.Value) { throw "Unsupported editor: $($property.Name)" }
}
$null = New-Item -ItemType Directory -Force -Path (Join-Path $out 'Dependencies')
Copy-Item -LiteralPath $Umodel -Destination (Join-Path $out 'Dependencies/umodel-research.exe') -Force
$license = Join-Path (Split-Path -Parent $Umodel) 'LICENSE.txt'
if (Test-Path -LiteralPath $license) { Copy-Item -LiteralPath $license -Destination (Join-Path $out 'Dependencies/UEViewer-LICENSE.txt') -Force }
if ($PythonRoot) {
    $pyout = Join-Path $out 'Python'
    $null = New-Item -ItemType Directory -Force -Path $pyout,(Join-Path $pyout 'Lib/site-packages')
    foreach ($pattern in 'python.exe','pythonw.exe','python*.dll','vcruntime*.dll','LICENSE.txt') {
        Get-ChildItem -LiteralPath $PythonRoot -Filter $pattern -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $pyout -Force }
    }
    Copy-Item -LiteralPath (Join-Path $PythonRoot 'DLLs') -Destination $pyout -Recurse -Force
    Get-ChildItem -LiteralPath (Join-Path $PythonRoot 'Lib') | Where-Object { $_.Name -notin 'site-packages','__pycache__','test','idlelib','ensurepip','tkinter' } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $pyout 'Lib') -Recurse -Force }
    Get-ChildItem -LiteralPath (Join-Path $PythonRoot 'Lib/site-packages') | Where-Object { $_.Name -eq 'PIL' -or $_.Name -like 'pillow*' } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $pyout 'Lib/site-packages') -Recurse -Force }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $out -Force
Write-Output "Published to $out"
