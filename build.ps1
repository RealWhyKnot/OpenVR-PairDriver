param(
	# When set, overrides the auto-derived YYYY.M.D.N-XXXX stamp. Release CI
	# passes the git tag (with leading "v" stripped) so the published
	# release's tag, zip filename, and embedded version are all the same string.
	[string]$Version = "",

	# Skip the CMake configure step (rerun MSBuild only). Useful when iterating
	# on a single source file.
	[switch]$SkipConfigure
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

# Configure (skippable for incremental edits).
if (-not $SkipConfigure) {
	& cmake -S . -B build -A x64
	if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build Release.
& cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Verify the artifact lands where we expect.
$dllPath = "build/driver_openvrpair/bin/win64/driver_openvrpair.dll"
if (-not (Test-Path $dllPath)) {
	throw "Expected driver DLL not found at $dllPath"
}
$dll = Get-Item $dllPath
Write-Host ""
Write-Host ("Built {0} ({1:N0} bytes, {2})" -f $dll.Name, $dll.Length, $dll.LastWriteTime)
Write-Host ("  -> {0}" -f $dll.FullName)
