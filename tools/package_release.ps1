param(
    [string]$BinaryDirectory = (Join-Path $PSScriptRoot '../bin'),
    [string]$ArchivePath = (Join-Path $PSScriptRoot '../bin/Reloaded_Editor.zip'),
    [string]$LauncherPath
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$launcherHash = '4CEC8AA84BCEE940A2661F4277E51EA7F5803BD4CFB0BD5953AA356C451D8400'
$upstreamZipHash = '57A53D6DCB3CB7430903039F0693C3193DABDDC6260B9078D2ABAC3B1E2673AF'
$upstreamUrl = 'https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor/releases/download/v1.2/Reloaded_Editor_1.2.zip'
$staging = $null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
try {
    # Keep the upstream launcher unchanged. The maintained editor code is in
    # the DLL; rebuilding the identical launcher source triggered AV detections.
    if (!$LauncherPath) {
        $staging = Join-Path ([IO.Path]::GetTempPath()) ('scct-release-' + [guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path $staging
        $download = Join-Path $staging 'Reloaded_Editor_1.2.zip'
        Invoke-WebRequest -Uri $upstreamUrl -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $upstreamZipHash) {
            throw 'Upstream release archive checksum mismatch.'
        }
        $LauncherPath = Join-Path $staging 'Reloaded_Editor.exe'
        $upstream = [IO.Compression.ZipFile]::OpenRead($download)
        try {
            $entry = $upstream.GetEntry('Reloaded_Editor.exe')
            if (!$entry) { throw 'Upstream release is missing its launcher.' }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $LauncherPath)
        } finally { $upstream.Dispose() }
    }
    if ((Get-FileHash -LiteralPath $LauncherPath -Algorithm SHA256).Hash -ne $launcherHash) {
        throw 'Launcher checksum mismatch. Use the unchanged AllyPal v1.2 launcher.'
    }
    $files = [ordered]@{
        'Reloaded_Editor.exe' = $LauncherPath
        'Reloaded.Editor.dll' = Join-Path $BinaryDirectory 'Reloaded.Editor.dll'
        'README.md' = Join-Path $repository 'README.md'
    }
    foreach ($source in $files.Values) {
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required release file is missing: $source" }
    }
    $archiveFullPath = [IO.Path]::GetFullPath($ArchivePath)
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archiveFullPath)
    # Create refuses to overwrite an existing release archive.
    $archive = [IO.Compression.ZipFile]::Open($archiveFullPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $files.GetEnumerator()) {
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, [IO.Path]::GetFullPath($entry.Value), $entry.Key,
                [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
    Write-Output "Release archive: $archiveFullPath"
    Write-Output "Launcher: unchanged AllyPal v1.2 ($launcherHash)"
} finally {
    if ($staging) {
        # Only these two files in our uniquely owned staging directory are removed.
        foreach ($name in @('Reloaded_Editor_1.2.zip', 'Reloaded_Editor.exe')) {
            $temporaryFile = Join-Path $staging $name
            if (Test-Path -LiteralPath $temporaryFile) { Remove-Item -LiteralPath $temporaryFile }
        }
        Remove-Item -LiteralPath $staging
    }
}
