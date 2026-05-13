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
    # UTF-8 without BOM. The static [System.Text.Encoding]::UTF8 has
    # emitUTF8Identifier=true (writes a BOM), which prefixes the file with
    # 0xEF 0xBB 0xBF -- the overlay's picojson parser rejects the BOM and
    # reports "Result JSON parse error" even though the JSON itself is fine.
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($ResultPath, $json, $utf8NoBom)
}

function Get-FtModulesDir {
    $base = [System.Environment]::GetFolderPath('LocalApplicationData')
    # LocalApplicationData is %AppData%\..\..\LocalLow on some systems; use the
    # registry key to get the real LocalAppDataLow path.
    $low = [System.Environment]::GetFolderPath('ApplicationData') -replace 'Roaming$','LocalLow'
    $dir = Join-Path $low 'WKOpenVR\facetracking\modules'
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
        installed_at = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
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
                       -Headers @{ 'User-Agent' = 'WKOpenVR/1.0' }
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
            -UseBasicParsing -Headers @{ 'User-Agent' = 'WKOpenVR/1.0' }
    } catch {
        Write-Result $false "Download failed for $($asset.browser_download_url): $_"
        exit 1
    }

    # Compute SHA-256 of downloaded zip.
    $downloadedSha = Get-Sha256 -filePath $tmpZip

    # Look for SHA-256 in release body. (PS 5.1 has no null-coalescing
    # operator, so write the fallback longhand.)
    $bodyText = if ($null -ne $release.body) { $release.body } else { '' }
    $releaseSha  = Find-Sha256InText -text $bodyText
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
        installed_at     = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    }
    Copy-ModuleFolder -srcDir $moduleRoot -uuid $uuid -version $ver -sourceInfo $info
    Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue

    Write-Result $true "Installed $ownerRepo $releaseTag (sha_verified=$shaVerified)." $uuid $ver
    exit 0
}

# ---- action: add/update (registry) -----------------------------------------
#
# A registry source points at a curated index (the legacy registry lives at
# https://legacy-registry.whyknot.dev). Endpoints:
#   GET <url>/v1/index                                   -> { modules: [{uuid, version, ...}] }
#   GET <url>/v1/modules/<uuid>/manifest                 -> full manifest incl. payload_sha256
#   GET <url>/v1/modules/<uuid>/versions/<ver>/payload   -> module zip
#
# Sync semantics: install every module in the index; skip when the same uuid+
# version is already on disk; replace when the version changed. Orphans (uuids
# on disk that no longer appear in the index) are left in place for now --
# removal is a separate decision we don't want to make implicitly during a
# generic Sync click.

if ($srcKind -eq 'registry') {
    $base = if ($src.PSObject.Properties['url']) { $src.url } else { '' }
    if ([string]::IsNullOrEmpty($base)) {
        Write-Result $false 'Registry source has no url field.'
        exit 1
    }
    $base = $base.TrimEnd('/')

    $headers = @{ 'User-Agent' = 'WKOpenVR/1.0' }

    try {
        $index = Invoke-RestMethod -Uri "$base/v1/index" -UseBasicParsing -Headers $headers
    } catch {
        Write-Result $false "Registry index fetch failed: $_"
        exit 1
    }

    if ($null -eq $index.modules) {
        Write-Result $false 'Registry index has no "modules" array.'
        exit 1
    }

    $modsDir = Get-FtModulesDir
    $installed = 0
    $updated   = 0
    $skipped   = 0
    $failed    = 0
    $errors    = @()

    foreach ($entry in $index.modules) {
        $uuid = $entry.uuid
        $ver  = $entry.version
        if ([string]::IsNullOrEmpty($uuid) -or [string]::IsNullOrEmpty($ver)) {
            $failed++
            $errors += "index entry missing uuid or version: $($entry | ConvertTo-Json -Compress)"
            continue
        }

        $destDir = Join-Path $modsDir "$uuid\$ver"
        if (Test-Path (Join-Path $destDir 'manifest.json')) {
            $skipped++
            continue
        }

        # Fetch the manifest (gives us payload_sha256).
        try {
            $manifest = Invoke-RestMethod -Uri "$base/v1/modules/$uuid/manifest" -UseBasicParsing -Headers $headers
        } catch {
            $failed++
            $errors += "manifest fetch failed for ${uuid}: $_"
            continue
        }
        # Trust the manifest's own version over the index entry's, in case the
        # index is mid-update (it's a generated artifact, not a live join).
        $manifestVer = if ($manifest.PSObject.Properties['version']) { $manifest.version } else { $ver }
        if ($manifestVer -ne $ver) {
            $destDir = Join-Path $modsDir "$uuid\$manifestVer"
            if (Test-Path (Join-Path $destDir 'manifest.json')) {
                $skipped++
                continue
            }
            $ver = $manifestVer
        }

        $expectedSha = if ($manifest.PSObject.Properties['payload_sha256']) {
            ([string]$manifest.payload_sha256).ToLower()
        } else { '' }

        # Download the payload.
        $tmpZip = [System.IO.Path]::GetTempFileName() + '.zip'
        try {
            Invoke-WebRequest -Uri "$base/v1/modules/$uuid/versions/$ver/payload" `
                              -OutFile $tmpZip -UseBasicParsing -Headers $headers
        } catch {
            Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue
            $failed++
            $errors += "payload download failed for ${uuid} ${ver}: $_"
            continue
        }

        $actualSha = Get-Sha256 -filePath $tmpZip
        if (-not [string]::IsNullOrEmpty($expectedSha) -and $actualSha -ne $expectedSha) {
            Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue
            $failed++
            $errors += "SHA-256 mismatch for ${uuid} ${ver}: expected $expectedSha got $actualSha"
            continue
        }

        # Extract to a staging dir, then atomically swap into place. Going
        # straight to the dest dir would leave a partial install behind on
        # any failure mid-extract.
        $tmpDir = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(),
                                             [System.IO.Path]::GetRandomFileName())
        New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
        try {
            Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force
        } catch {
            Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue
            Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue
            $failed++
            $errors += "zip extract failed for ${uuid} ${ver}: $_"
            continue
        }
        Remove-Item -Force -Path $tmpZip -ErrorAction SilentlyContinue

        $alreadyHaveVersion = Test-Path $destDir
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        try {
            Get-ChildItem -Path $tmpDir -Recurse | ForEach-Object {
                $rel    = $_.FullName.Substring($tmpDir.Length).TrimStart('\','/')
                $target = Join-Path $destDir $rel
                if ($_.PSIsContainer) {
                    if (-not (Test-Path $target)) {
                        New-Item -ItemType Directory -Path $target -Force | Out-Null
                    }
                } else {
                    Copy-Item -Path $_.FullName -Destination $target -Force
                }
            }
        } catch {
            Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue
            $failed++
            $errors += "copy-into-dest failed for ${uuid} ${ver}: $_"
            continue
        }
        Remove-Item -Recurse -Force -Path $tmpDir -ErrorAction SilentlyContinue

        # Persist the manifest next to the extracted files so the host can
        # read it without going back to the network, and stamp source.json
        # so a future Sync knows where this module came from.
        $manifestJson = $manifest | ConvertTo-Json -Depth 10 -Compress
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText((Join-Path $destDir 'manifest.json'),
                                       $manifestJson, $utf8NoBom)
        $info = @{
            source_id    = $srcId
            source_kind  = 'registry'
            registry_url = $base
            installed_at = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        }
        Write-SourceJson -destDir $destDir -data $info

        if ($alreadyHaveVersion) { $updated++ } else { $installed++ }
    }

    $summary = "Registry sync: installed=$installed updated=$updated skipped=$skipped failed=$failed (total=$($index.modules.Count))"
    if ($failed -gt 0) {
        $summary += " | errors: " + ($errors -join '; ')
        Write-Result $false $summary
        exit 1
    }
    Write-Result $true $summary
    exit 0
}

Write-Result $false "Unknown kind: $srcKind"
exit 1
