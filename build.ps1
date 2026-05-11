param(
	# When set, overrides the auto-derived YYYY.M.D.N-XXXX stamp. Release CI
	# passes the git tag (with leading "v" stripped) so the published
	# release's tag, zip filename, and embedded version are all the same string.
	[string]$Version = "",

	# Skip the CMake configure step (rerun MSBuild only). Useful when iterating
	# on a single source file.
	[switch]$SkipConfigure,

	# Produce a release zip + per-file manifest TSV under release/. Required
	# by .github/workflows/release.yml; set automatically by it. A local dev
	# build can pass this too if you want to test the packaging step.
	[switch]$Release
)

$ErrorActionPreference = "Stop"

# Pin the working directory to the script's own root so relative paths resolve
# consistently regardless of how the script is invoked.
Set-Location $PSScriptRoot

# Activate the repo's tracked git hooks the first time the build runs in a
# clone. Idempotent: only writes when the value would change.
$currentHooksPath = & git config --get core.hooksPath 2>$null
if ($currentHooksPath -ne ".githooks") {
	& git config core.hooksPath ".githooks"
	Write-Host "Activated .githooks/ via core.hooksPath"
}

# Stamp the build version. Release CI passes -Version; local builds derive the
# stamp from today's date + a per-day counter + a 4-hex GUID prefix.
if ($Version -eq "") {
	$today = Get-Date -Format "yyyy.M.d"
	$counterFile = "build/local_build_state.json"
	$counter = 0
	if (Test-Path $counterFile) {
		$state = Get-Content $counterFile -Raw | ConvertFrom-Json
		if ($state.date -eq $today) {
			$counter = [int]$state.counter + 1
		}
	}
	$uid = ([guid]::NewGuid().ToString("N").Substring(0, 4)).ToUpper()
	$Version = "$today.$counter-$uid"
	New-Item -ItemType Directory -Force -Path "build" | Out-Null
	@{ date = $today; counter = $counter } | ConvertTo-Json | Set-Content $counterFile
}
Set-Content -Path "version.txt" -Value $Version -NoNewline
Write-Host "Build version: $Version"

# Stamp the SpaceCalibrator feature plugin's BuildStamp.h with the same
# version. Without this, the standalone SC fallback ("0.0.0.0-DEV") shows up
# in the calibration UI's version line even though the umbrella binary
# itself reports the real stamp. The file is generated; the SC repo tracks a
# fallback that this overwrite replaces only for the umbrella build.
$ScBuildStamp = Join-Path $PSScriptRoot "features/OpenVR-WKSpaceCalibrator/src/overlay/BuildStamp.h"
if (Test-Path (Split-Path -Parent $ScBuildStamp)) {
	Set-Content -Path $ScBuildStamp -Value @"
// Overwritten by OpenVR-WKPairDriver/build.ps1 with the umbrella binary's
// per-build stamp so SC's version footer reads the same string the
// umbrella top header reports.
#pragma once

#define SPACECAL_BUILD_STAMP "$Version"
#define SPACECAL_BUILD_CHANNEL "dev"
"@
}

# Configure (skippable for incremental edits). The CMAKE_POLICY_VERSION_MINIMUM
# bump is needed because the minhook submodule pins cmake_minimum_required at
# 2.8 and current CMake versions reject anything below 3.5.
#
# The $ErrorActionPreference = 'Continue' wrap around each cmake call is
# the same shape SC's build.ps1 uses. PowerShell 5.1 wraps CMake's stdout
# message() lines as NativeCommandError ErrorRecords; under the script-
# wide 'Stop' default that wrap kills the script on the first message
# even when cmake exited 0. Localised 'Continue' lets the cmake run to
# completion; we still throw on a real non-zero exit code.
if (-not $SkipConfigure) {
	$PrevEap = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	try {
		& cmake -S . -B build -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
		if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
	} finally {
		$ErrorActionPreference = $PrevEap
	}
}

# Build Release.
$PrevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
	& cmake --build build --config Release --parallel
	if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} finally {
	$ErrorActionPreference = $PrevEap
}

# Verify the artifact lands where we expect.
$dllPath = "build/driver_openvrpair/bin/win64/driver_openvrpair.dll"
if (-not (Test-Path $dllPath)) {
	throw "Expected driver DLL not found at $dllPath"
}
$dll = Get-Item $dllPath
Write-Host ""
Write-Host ("Built {0} ({1:N0} bytes, {2})" -f $dll.Name, $dll.Length, $dll.LastWriteTime)
Write-Host ("  -> {0}" -f $dll.FullName)

if ($Release) {
	# Pack the deployable driver tree (manifest, resources, bin/win64/DLL) into
	# a zip plus a sibling manifest TSV. The release workflow consumes both --
	# the zip is the asset, the manifest feeds the File integrity section of
	# the release body.
	$driverTree = "build/driver_openvrpair"
	if (-not (Test-Path $driverTree)) {
		throw "Driver tree not found at $driverTree -- expected the CMake post-build copy step to populate it."
	}
	New-Item -ItemType Directory -Force -Path "release" | Out-Null
	$stageDir = Join-Path "release" "_stage_$Version"
	if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
	New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
	Copy-Item -Recurse -Path "$driverTree/*" -Destination $stageDir

	$stagedDriverBin = Join-Path $stageDir "bin/win64"
	$bareDriverDll = Join-Path $stagedDriverBin "driver_openvrpair.dll"
	$loaderDriverDll = Join-Path $stagedDriverBin "driver_01openvrpair.dll"
	if (Test-Path $bareDriverDll) {
		Move-Item -Force -Path $bareDriverDll -Destination $loaderDriverDll
	}
	if (-not (Test-Path $loaderDriverDll)) {
		throw "Staged shared driver DLL not found at $loaderDriverDll"
	}

	$zipName = "OpenVR-WKPairDriver-v$Version.zip"
	$zipPath = Join-Path "release" $zipName
	if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
	Compress-Archive -Path "$stageDir/*" -DestinationPath $zipPath -CompressionLevel Optimal
	$zipItem = Get-Item $zipPath

	$manifestName = "OpenVR-WKPairDriver-v$Version.manifest.tsv"
	$manifestPath = Join-Path "release" $manifestName
	$rootLength = (Resolve-Path $stageDir).Path.Length + 1
	$rows = Get-ChildItem $stageDir -Recurse -File | ForEach-Object {
		$rel = $_.FullName.Substring($rootLength).Replace('\', '/')
		$h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
		"{0}`t{1}`t{2}" -f $h, $_.Length, $rel
	}
	# WriteAllLines via System.IO.File with a no-BOM UTF8Encoding so the
	# downstream Generate-ReleaseNotes.ps1 manifest parser sees clean column-1
	# bytes rather than the UTF-8 BOM that Out-File -Encoding utf8 prepends on
	# Windows PowerShell 5.1.
	$enc = [System.Text.UTF8Encoding]::new($false)
	[System.IO.File]::WriteAllLines((Resolve-Path -LiteralPath (Split-Path $manifestPath)).Path + "\" + (Split-Path -Leaf $manifestPath), $rows, $enc)
	Remove-Item -Recurse -Force $stageDir

	Write-Host ""
	Write-Host ("Packaged release zip:      {0} ({1:N0} bytes)" -f $zipItem.Name, $zipItem.Length)
	Write-Host ("Packaged release manifest: {0}" -f $manifestName)
}
