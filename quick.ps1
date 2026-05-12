param(
	# Skip the build and only run the deploy + verify pass against whatever
	# is currently sitting in build/. Useful if you just want to redeploy.
	[switch]$SkipBuild,

	# Don't prompt before launching the elevated copy. Still pops UAC.
	[switch]$Yes,

	# Override the deployed install dir. Defaults to "C:\Program Files\OpenVR-Pair".
	[string]$InstallDir = "C:\Program Files\OpenVR-Pair",

	# Override the SteamVR driver dir. Defaults to the standard Steam install
	# path. The "01" prefix is the loader-renamed copy of the bare driver DLL.
	[string]$SteamVRDriversDir = "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Get-Sha($path) {
	if (-not (Test-Path -LiteralPath $path)) { return $null }
	return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

function Resolve-Version($exePath) {
	if (-not (Test-Path -LiteralPath $exePath)) { return "(missing)" }
	try {
		$info = (Get-Item -LiteralPath $exePath).VersionInfo
		if ($info.FileVersion) { return $info.FileVersion }
	} catch { }
	return "(unknown)"
}

if (-not $SkipBuild) {
	Write-Host "Building OpenVR-Pair umbrella + driver + features..."
	& "$PSScriptRoot\build.ps1"
	if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
}

# Build outputs we care about. The umbrella exe lands here; the driver DLL is
# emitted under the staged driver tree with its bare name (the loader-renamed
# "01openvrpair" prefix is applied only inside the release zip stage dir).
$srcExe      = Join-Path $PSScriptRoot "build\artifacts\Release\OpenVR-Pair.exe"
$srcDll      = Join-Path $PSScriptRoot "build\driver_openvrpair\bin\win64\driver_openvrpair.dll"
$srcManifest = Join-Path $PSScriptRoot "build\artifacts\Release\manifest.vrmanifest"
$srcIcon     = Join-Path $PSScriptRoot "build\artifacts\Release\dashboard_icon.png"

foreach ($p in @($srcExe, $srcDll, $srcManifest, $srcIcon)) {
	if (-not (Test-Path -LiteralPath $p)) { throw "Build artifact missing: $p" }
}

# Optional: the C# face-tracking host sidecar. Built when OPENVR_PAIR_BUILD_FACE_HOST
# is on (default) and the .NET 10 SDK is available. If absent, the driver loads
# without the host and the FaceTracking feature runs inert -- so a missing host
# tree is a warning, not a hard failure.
$srcHostDir = Join-Path $PSScriptRoot "build\driver_openvrpair\resources\facetracking\host"
$srcHostExe = Join-Path $srcHostDir "OpenVRPair.FaceModuleHost.exe"
$hostPresent = (Test-Path -LiteralPath $srcHostExe)

# Deployed copies.
$dstExe       = Join-Path $InstallDir "OpenVR-Pair.exe"
$dstManifest  = Join-Path $InstallDir "manifest.vrmanifest"
$dstIcon      = Join-Path $InstallDir "dashboard_icon.png"
$dstDriverRoot = Join-Path $SteamVRDriversDir "01openvrpair"
$dstDriverDir = Join-Path $dstDriverRoot "bin\win64"
$dstDll       = Join-Path $dstDriverDir "driver_01openvrpair.dll"
$dstHostDir   = Join-Path $dstDriverRoot "resources\facetracking\host"
$dstHostExe   = Join-Path $dstHostDir "OpenVRPair.FaceModuleHost.exe"

$srcExeSha      = Get-Sha $srcExe
$srcDllSha      = Get-Sha $srcDll
$srcManifestSha = Get-Sha $srcManifest
$srcIconSha     = Get-Sha $srcIcon
$srcHostExeSha  = if ($hostPresent) { Get-Sha $srcHostExe } else { $null }
$dstExeSha      = Get-Sha $dstExe
$dstDllSha      = Get-Sha $dstDll
$dstManifestSha = Get-Sha $dstManifest
$dstIconSha     = Get-Sha $dstIcon
$dstHostExeSha  = Get-Sha $dstHostExe

Write-Host ""
Write-Host ("Source exe:        {0}" -f $srcExeSha)
Write-Host ("Deployed exe:      {0}" -f $dstExeSha)
Write-Host ("Source DLL:        {0}" -f $srcDllSha)
Write-Host ("Deployed DLL:      {0}" -f $dstDllSha)
Write-Host ("Source manifest:   {0}" -f $srcManifestSha)
Write-Host ("Deployed manifest: {0}" -f $dstManifestSha)
Write-Host ("Source icon:       {0}" -f $srcIconSha)
Write-Host ("Deployed icon:     {0}" -f $dstIconSha)
if ($hostPresent) {
	Write-Host ("Source host:       {0}" -f $srcHostExeSha)
	Write-Host ("Deployed host:     {0}" -f $dstHostExeSha)
} else {
	Write-Host "Source host:       (not built; OPENVR_PAIR_BUILD_FACE_HOST=OFF or .NET 10 SDK missing)"
}

$exeStale      = ($srcExeSha -ne $dstExeSha)
$driverStale   = ($srcDllSha -ne $dstDllSha)
$manifestStale = ($srcManifestSha -ne $dstManifestSha)
$iconStale     = ($srcIconSha -ne $dstIconSha)
$hostStale     = $hostPresent -and ($srcHostExeSha -ne $dstHostExeSha)

if (-not $exeStale -and -not $driverStale -and -not $manifestStale -and -not $iconStale -and -not $hostStale) {
	Write-Host ""
	Write-Host "Already up to date. Deployed build: $(Resolve-Version $dstExe)"
	exit 0
}

Write-Host ""
if ($exeStale)      { Write-Host "Overlay exe needs redeploy." }
if ($driverStale)   { Write-Host "Driver DLL needs redeploy." }
if ($manifestStale) { Write-Host "vrmanifest needs redeploy." }
if ($iconStale)     { Write-Host "Dashboard icon needs redeploy." }
if ($hostStale)     { Write-Host "FaceModuleHost tree needs redeploy." }

if (-not $Yes) {
	$reply = Read-Host "Continue with elevated copy? [y/N]"
	if ($reply -notmatch "^(y|yes)$") {
		Write-Host "Aborted."
		exit 1
	}
}

# Build the elevated helper script. One UAC prompt does both copies and prints
# every step + the post-copy SHA so a failure is easy to read in the report
# file. We capture stdout/stderr to a known log path so a denied UAC or a
# write failure shows up here, not just in the elevated console window.
$helperOut = Join-Path $env:TEMP "OpenVR-Pair-deploy-stdout.log"
$helperErr = Join-Path $env:TEMP "OpenVR-Pair-deploy-stderr.log"
$resultFile = Join-Path $env:TEMP "OpenVR-Pair-deploy-result.txt"
foreach ($p in @($helperOut, $helperErr, $resultFile)) { if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force } }

$hostPresentBool = if ($hostPresent) { '$true' } else { '$false' }

$helperScript = @"
`$ErrorActionPreference = 'Continue'
try {
	New-Item -ItemType Directory -Force -Path '$InstallDir' | Out-Null
	Copy-Item -LiteralPath '$srcExe' -Destination '$dstExe' -Force
	New-Item -ItemType Directory -Force -Path '$dstDriverDir' | Out-Null

	# Park the live DLL aside so SteamVR can still load it if it has it open.
	# Best-effort: a previous backup may itself be locked.
	if (Test-Path -LiteralPath '$dstDll') {
		`$stamp = (Get-Date).ToString('yyyyMMddHHmmss')
		`$bak = '$dstDll' + '.old.' + `$stamp
		try { Rename-Item -LiteralPath '$dstDll' -NewName (Split-Path -Leaf `$bak) -Force } catch { }
	}
	Copy-Item -LiteralPath '$srcDll' -Destination '$dstDll' -Force

	# vrmanifest sits next to the exe so OpenVR-Pair.exe can resolve it via
	# GetModuleFileName + replace_filename at startup.
	Copy-Item -LiteralPath '$srcManifest' -Destination '$dstManifest' -Force

	# Dashboard overlay thumbnail. VrOverlayHost loads this exe-relative
	# at SetOverlayFromFile time.
	Copy-Item -LiteralPath '$srcIcon' -Destination '$dstIcon' -Force

	# Face-tracking host sidecar. Replaced wholesale because the .NET publish
	# output (deps.json, runtimeconfig.json, dependent DLLs) varies between
	# builds. Apply the same rename-aside pattern used for the driver DLL: if
	# SteamVR has the exe open, the rename succeeds (unlinking the name) and
	# the copy proceeds cleanly without leaving a mixed state.
	if ($hostPresentBool) {
		`$hostSrc = '$srcHostDir'
		`$hostDst = '$dstHostDir'
		New-Item -ItemType Directory -Force -Path `$hostDst | Out-Null
		`$hostExeDst = Join-Path `$hostDst 'OpenVRPair.FaceModuleHost.exe'
		if (Test-Path -LiteralPath `$hostExeDst) {
			`$stamp = (Get-Date).ToString('yyyyMMddHHmmss')
			`$bak = `$hostExeDst + '.old.' + `$stamp
			try { Rename-Item -LiteralPath `$hostExeDst -NewName (Split-Path -Leaf `$bak) -Force } catch { }
		}
		Get-ChildItem -LiteralPath `$hostDst -File | Where-Object { `$_.Name -notlike '*.old.*' } | Remove-Item -Force
		Copy-Item -Path (Join-Path `$hostSrc '*') -Destination `$hostDst -Recurse -Force
	}

	`$exeSha      = (Get-FileHash -LiteralPath '$dstExe' -Algorithm SHA256).Hash
	`$dllSha      = (Get-FileHash -LiteralPath '$dstDll' -Algorithm SHA256).Hash
	`$manifestSha = (Get-FileHash -LiteralPath '$dstManifest' -Algorithm SHA256).Hash
	`$iconSha     = (Get-FileHash -LiteralPath '$dstIcon' -Algorithm SHA256).Hash
	`$hostSha     = if (Test-Path -LiteralPath '$dstHostExe') { (Get-FileHash -LiteralPath '$dstHostExe' -Algorithm SHA256).Hash } else { '' }
	Set-Content -LiteralPath '$resultFile' -Value ("OK`n" + `$exeSha + "`n" + `$dllSha + "`n" + `$manifestSha + "`n" + `$iconSha + "`n" + `$hostSha)
	exit 0
} catch {
	Set-Content -LiteralPath '$resultFile' -Value ("ERR`n" + `$_.Exception.Message)
	exit 1
}
"@

$helperPath = Join-Path $env:TEMP "OpenVR-Pair-deploy.ps1"
Set-Content -LiteralPath $helperPath -Value $helperScript -Encoding UTF8

$proc = Start-Process -FilePath "powershell.exe" `
	-ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",$helperPath) `
	-Verb RunAs `
	-WindowStyle Hidden `
	-PassThru `
	-Wait
if (-not $proc) {
	throw "Elevated helper did not launch (UAC denied?)"
}

if (-not (Test-Path -LiteralPath $resultFile)) {
	throw "Elevated helper produced no result file at $resultFile (UAC denied or helper crashed)"
}

$resultLines = Get-Content -LiteralPath $resultFile
if ($resultLines.Count -lt 1 -or $resultLines[0] -ne "OK") {
	$msg = if ($resultLines.Count -ge 2) { $resultLines[1] } else { "(empty)" }
	throw "Elevated helper reported failure: $msg"
}

# Re-verify destination SHA against source. The helper already reported a
# value but we re-read from disk so partial writes or post-write tampering
# get caught.
$postExeSha      = Get-Sha $dstExe
$postDllSha      = Get-Sha $dstDll
$postManifestSha = Get-Sha $dstManifest
$postIconSha     = Get-Sha $dstIcon
$postHostExeSha  = Get-Sha $dstHostExe

$exeOk      = ($postExeSha -eq $srcExeSha)
$dllOk      = ($postDllSha -eq $srcDllSha)
$manifestOk = ($postManifestSha -eq $srcManifestSha)
$iconOk     = ($postIconSha -eq $srcIconSha)
$hostOk     = (-not $hostPresent) -or ($postHostExeSha -eq $srcHostExeSha)

Write-Host ""
Write-Host ("Post-copy exe:      {0} {1}" -f $postExeSha,      ($(if ($exeOk)      { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy DLL:      {0} {1}" -f $postDllSha,      ($(if ($dllOk)      { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy manifest: {0} {1}" -f $postManifestSha, ($(if ($manifestOk) { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy icon:     {0} {1}" -f $postIconSha,     ($(if ($iconOk)     { "OK" } else { "MISMATCH" })))
if ($hostPresent) {
	Write-Host ("Post-copy host:     {0} {1}" -f $postHostExeSha, ($(if ($hostOk) { "OK" } else { "MISMATCH" })))
}

if (-not $exeOk -or -not $dllOk -or -not $manifestOk -or -not $iconOk -or -not $hostOk) {
	$detail = @()
	if (-not $exeOk)      { $detail += "exe at $dstExe still does not match source" }
	if (-not $dllOk)      { $detail += "driver DLL at $dstDll still does not match source" }
	if (-not $manifestOk) { $detail += "manifest at $dstManifest still does not match source" }
	if (-not $iconOk)     { $detail += "icon at $dstIcon still does not match source" }
	if (-not $hostOk)     { $detail += "FaceModuleHost.exe at $dstHostExe still does not match source" }
	throw ("Deploy did not converge: " + ($detail -join "; "))
}

Write-Host ""
Write-Host "Deploy verified. Installed build: $(Resolve-Version $dstExe)"
