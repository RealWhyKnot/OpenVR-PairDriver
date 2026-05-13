#Requires -Version 5.1
# face-module-sync.ps1 -- install / update / remove face-tracking modules
# from folder or GitHub sources.  Runs without elevation; all target
# directories are under %LocalAppDataLow%.
#
# Parameters:
#   -Action     add | update | remove
#   -Kind       folder | github          (required for add/update)
#   -SourceData '<JSON string>'          (required for add/update; source descriptor)
#   -SourceId   '<hex id>'               (required for remove and update)
#   -ResultPath '<file path>'            (required; result JSON is written here)
#
# Result JSON written to -ResultPath:
#   { "ok": true|false, "message": "...",
#     "installed_uuid": "...", "installed_version": "..." }

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Action,
    [string] $Kind       = '',
    [string] $SourceData = '',
    [string] $SourceId   = '',
    [Parameter(Mandatory)][string] $ResultPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---- helpers ---------------------------------------------------------------

function Write-Result([bool]$ok, [string]$msg, [string]$uuid = '', [string]$ver = '') {
    $obj = [ordered]@{
        ok                = $ok
        message           = $msg
        installed_uuid    = $uuid
        installed_version = $ver
    }
    $json = $obj | ConvertTo-Json -Compress
    [System.IO.File]::WriteAllText($ResultPath, $json, [System.Text.Encoding]::UTF8)
}

function Get-FtModulesDir {
    $base = [System.Environment]::GetFolderPath('LocalApplicationData')
    # LocalApplicationData is %AppData%\..\..\LocalLow on some systems; use the
    # registry key to get the real LocalAppDataLow path.
    $low = [System.Environment]::GetFolderPath('ApplicationData') -replace 'Roaming$','LocalLow'
    $dir = Join-Path $low 'OpenVR-Pair\facetracking\modules'
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    return $dir
}

function Read-Manifest([string]$folder) {
    $path = Join-Path $folder 'manifest.json'
    if (-not (Test-Path $path)) { return $null }
    return Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-SourceJson([string]$destDir, [hashtable]$data) {
    $json = $data | ConvertTo-Json -Compress
    [System.IO.File]::WriteAllText((Join-Path $destDir 'source.json'), $json,
        [System.Text.Encoding]::UTF8)
}

function Copy-ModuleFolder([string]$srcDir, [string]$uuid, [string]$version,
                           [hashtable]$sourceInfo) {
    $modsDir = Get-FtModulesDir
    $destDir = Join-Path $modsDir "$uuid\$version"
    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }

    # Copy everything from the source folder.
    Get-ChildItem -Path $srcDir -Recurse | ForEach-Object {
        $rel     = $_.FullName.Substring($srcDir.Length).TrimStart('\','/')
        $target  = Join-Path $destDir $rel
        if ($_.PSIsContainer) {
            if (-not (Test-Path $target)) { New-Item -ItemType Directory -Path $target -Force | Out-Null }
        } else {
            Copy-Item -Path $_.FullName -Destination $target -Force
        }
    }
    Write-SourceJson -destDir $destDir -data $sourceInfo
}

function Remove-SourceModules([string]$srcId) {
    $modsDir = Get-FtModulesDir
    if (-not (Test-Path $modsDir)) { return }
    foreach ($uuidDir in Get-ChildItem -Path $modsDir -Directory) {
        foreach ($verDir in Get-ChildItem -Path $uuidDir.FullName -Directory) {
            $sourceFile = Join-Path $verDir.FullName 'source.json'
            if (Test-Path $sourceFile) {
                $s = Get-Content $sourceFile -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($s.source_id -eq $srcId) {
                    Remove-Item -Recurse -Force -Path $verDir.FullName
                }
            }
        }
        # Clean up empty uuid dir.
        $remaining = Get-ChildItem -Path $uuidDir.FullName -Directory
        if ($null -eq $remaining -or @($remaining).Count -eq 0) {
            Remove-Item -Recurse -Force -Path $uuidDir.FullName -ErrorAction SilentlyContinue
        }
    }
}

function Get-Sha256([string]$filePath) {
    $hash = Get-FileHash -Path $filePath -Algorithm SHA256
    return $hash.Hash.ToLower()
}

function Find-Sha256InText([string]$text) {
    # Match "SHA-256: <64 hex>" or "SHA256=<64 hex>" etc., case-insensitive.
    $m = [regex]::Match($text, '(?i)SHA-?256[:=]?\s*([a-f0-9]{64})')
    if ($m.Success) { return $m.Groups[1].Value.ToLower() }
    return $null
}

# ---- action: remove --------------------------------------------------------

if ($Action -eq 'remove') {
    if ([string]::IsNullOrEmpty($SourceId)) {
        Write-Result $false 'SourceId required for remove.'
        exit 1
    }
    Remove-SourceModules -srcId $SourceId
    Write-Result $true "Removed modules for source $SourceId."
    exit 0
}

# ---- parse SourceData -------------------------------------------------------

if ([string]::IsNullOrEmpty($SourceData)) {
    Write-Result $false 'SourceData required for add/update.'
    exit 1
}
try {
    $src = $SourceData | ConvertFrom-Json
} catch {
    Write-Result $false "SourceData JSON parse error: $_"
    exit 1
}

$srcId   = if ($src.PSObject.Properties['id'])         { $src.id }         else { $SourceId }
$srcKind = if ($src.PSObject.Properties['kind'])       { $src.kind }       else { $Kind }

# ---- action: add/update (folder) -------------------------------------------

if ($srcKind -eq 'folder') {
    $folderPath = if ($src.PSObject.Properties['path']) { $src.path } else { '' }
    if ([string]::IsNullOrEmpty($folderPath) -or -not (Test-Path $folderPath)) {
        Write-Result $false "Folder not found: $folderPath"
        exit 1
    }
    $manifest = Read-Manifest -folder $folderPath
    if ($null -eq $manifest) {
        Write-Result $false "No manifest.json found in $folderPath"
        exit 1
    }
    $uuid = $manifest.uuid
    $ver  = $manifest.version
    if ([string]::IsNullOrEmpty($uuid) -or [string]::IsNullOrEmpty($ver)) {
        Write-Result $false 'manifest.json must have uuid and version fields.'
        exit 1
    }

    $info = @{
        source_id    = $srcId
        source_kind  = 'folder'
        installed_at = (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssZ' -AsUTC)
    }
    Copy-ModuleFolder -srcDir $folderPath -uuid $uuid -version $ver -sourceInfo $info
    Write-Result $true "Installed from folder." $uuid $ver
    exit 0
}

# ---- action: add/update (github) -------------------------------------------

if ($srcKind -eq 'github') {
    $ownerRepo = if ($src.PSObject.Properties['owner_repo']) { $src.owner_repo } else { '' }
    if ([string]::IsNullOrEmpty($ownerRepo)) {
        Write-Result $false 'owner_repo required for github source.'
        exit 1
    }

    $apiUrl = "https://api.github.com/repos/$ownerRepo/releases/latest"
    try {
        $release = Invoke-RestMethod -Uri $apiUrl -UseBasicParsing `
                       -Headers @{ 'User-Agent' = 'OpenVR-Pair/1.0' }
    } catch {
        Write-Result $false "GitHub API error for ${ownerRepo}: $_"
        exit 1
    }

    $releaseTag = $release.tag_name

    # For update: skip if tag unchanged.
    if ($Action -eq 'update') {
        $lastTag = if ($src.PSObject.Properties['last_release_tag']) { $src.last_release_tag } else { '' }
        if ($lastTag -eq $releaseTag) {
            Write-Result $true "Already up to date ($releaseTag)."
            exit 0
        }
    }

    # Find the first .zip asset.
    $asset = $release.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
    if ($null -eq $asset) {
        Write-Result $false "No .zip asset found in release $releaseTag for $ownerRepo"
        exit 1
    }

    # Download the zip to a temp file.
    $tmpZip = [System.IO.Path]::GetTempFileName() + '.zip'
    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmpZip `
            -UseBasicParsing -Headers @{ 'User-Agent' = 'OpenVR-Pair/1.0' }
    } catch {
        Write-Result $false "Download failed for $($asset.browser_download_url): $_"
        exit 1
    }

    # Compute SHA-256 of downloaded zip.
    $downloadedSha = Get-Sha256 -filePath $tmpZip

    # Look for SHA-256 in release body.
    $releaseSha  = Find-Sha256InText -text ($release.body ?? '')
    $shaVerified = ($null -ne $releaseSha -and $releaseSha -eq $downloadedSha)

    # Extract to temp dir.
    $tmpDir = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(),
                                         [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
    try {
        Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force
    } catch {
        Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue
        Write-Result $false "Zip extraction failed: $_"
        exit 1
    }
    Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue

    # Find the manifest.json (may be at root or one level deep).
    $manifestFile = Get-ChildItem -Path $tmpDir -Filter 'manifest.json' -Recurse |
                    Select-Object -First 1
    if ($null -eq $manifestFile) {
        Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue
        Write-Result $false "No manifest.json found in release zip for $ownerRepo"
        exit 1
    }

    $manifest = Get-Content $manifestFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $uuid = $manifest.uuid
    $ver  = $manifest.version
    if ([string]::IsNullOrEmpty($uuid) -or [string]::IsNullOrEmpty($ver)) {
        Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue
        Write-Result $false 'manifest.json must have uuid and version fields.'
        exit 1
    }

    # The module root is the directory containing manifest.json.
    $moduleRoot = $manifestFile.DirectoryName

    $info = @{
        source_id        = $srcId
        source_kind      = 'github'
        release_tag      = $releaseTag
        release_sha256   = $releaseSha
        verified_sha256  = $shaVerified
        installed_at     = (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssZ' -AsUTC)
    }
    Copy-ModuleFolder -srcDir $moduleRoot -uuid $uuid -version $ver -sourceInfo $info
    Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue

    Write-Result $true "Installed $ownerRepo $releaseTag (sha_verified=$shaVerified)." $uuid $ver
    exit 0
}

Write-Result $false "Unknown kind: $srcKind"
exit 1
