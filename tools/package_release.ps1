param(
    [string]$BinaryDirectory = (Join-Path $PSScriptRoot '../bin'),
    [string]$ArchivePath = (Join-Path $PSScriptRoot '../bin/Reloaded_Editor.zip')
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot

# Package only the shipping binaries, never local test executables or stale DLLs.
# Build Release|x86 before invoking this script.
$files = [ordered]@{
    'Reloaded_Editor.exe' = Join-Path $BinaryDirectory 'Reloaded_Editor.exe'
    'Reloaded.Editor.dll' = Join-Path $BinaryDirectory 'Reloaded.Editor.dll'
    'd3d8.dll' = Join-Path $BinaryDirectory 'd3d8.dll'
    'README.md' = Join-Path $repository 'README.md'
    'LICENSE' = Join-Path $repository 'LICENSE'
    'licenses/d3d8to9-LICENSE.md' = Join-Path $repository 'Reloaded.Editor/Include/d3d8to9/LICENSE.md'
    'tools/patch_reloaded_play_level.py' = Join-Path $repository 'tools/patch_reloaded_play_level.py'
}
foreach ($source in $files.Values) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release file is missing: $source"
    }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveFullPath = [IO.Path]::GetFullPath($ArchivePath)
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archiveFullPath)
# Refuse to overwrite an existing archive so each packaged build is preserved.
$archive = [IO.Compression.ZipFile]::Open($archiveFullPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $files.GetEnumerator()) {
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, [IO.Path]::GetFullPath($entry.Value), $entry.Key,
            [IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $archive.Dispose()
}
Write-Output "Release archive: $archiveFullPath"
