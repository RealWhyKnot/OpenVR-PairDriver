param(
	# Skip the build and only run the deploy + verify pass against whatever
	# is currently sitting in build/. Useful if you just want to redeploy.
	[switch]$SkipBuild,

	# Don't prompt before launching the elevated copy. Still pops UAC.
	[switch]$Yes,

	# Override the deployed install dir. Defaults to "C:\Program Files\WKOpenVR".
	[string]$InstallDir = "C:\Program Files\WKOpenVR",

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
	Write-Host "Building WKOpenVR umbrella + driver + features..."
	& "$PSScriptRoot\build.ps1"
	if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
}

# Build outputs we care about. The umbrella exe lands here; the driver DLL is
# emitted under the staged driver tree with its bare name (the loader-renamed
# "01wkopenvr" prefix is applied only inside the release zip stage dir).
$srcExe        = Join-Path $PSScriptRoot "build\artifacts\Release\WKOpenVR.exe"
$srcOpenVR     = Join-Path $PSScriptRoot "build\artifacts\Release\openvr_api.dll"
$srcDiscordRpc = Join-Path $PSScriptRoot "build\artifacts\Release\discord-rpc.dll"
$srcDll        = Join-Path $PSScriptRoot "build\driver_wkopenvr\bin\win64\driver_wkopenvr.dll"
$srcManifest   = Join-Path $PSScriptRoot "build\artifacts\Release\manifest.vrmanifest"
$srcIcon       = Join-Path $PSScriptRoot "build\artifacts\Release\dashboard_icon.png"

foreach ($p in @($srcExe, $srcOpenVR, $srcDiscordRpc, $srcDll, $srcManifest, $srcIcon)) {
	if (-not (Test-Path -LiteralPath $p)) { throw "Build artifact missing: $p" }
}

# Optional: the C# face-tracking host sidecar. Built when OPENVR_PAIR_BUILD_FACE_HOST
# is on (default) and the .NET 10 SDK is available. If absent, the driver loads
# without the host and the FaceTracking feature runs inert -- so a missing host
# tree is a warning, not a hard failure.
$srcHostDir = Join-Path $PSScriptRoot "build\driver_wkopenvr\resources\facetracking\host"
$srcHostExe = Join-Path $srcHostDir "OpenVRPair.FaceModuleHost.exe"
$hostPresent = (Test-Path -LiteralPath $srcHostExe)

# Optional: the native translator host sidecar. Built when
# OPENVR_PAIR_BUILD_TRANSLATOR_HOST is on (default). If absent the driver
# loads but the Translator feature reports CreateProcessW err=3 every Init.
$srcTrHostDir = Join-Path $PSScriptRoot "build\driver_wkopenvr\resources\translator\host"
$srcTrHostExe = Join-Path $srcTrHostDir "WKOpenVR.TranslatorHost.exe"
$trHostPresent = (Test-Path -LiteralPath $srcTrHostExe)

# Overlay-side resources tree (currently: face-module-sync.ps1 for the FaceTracking
# Modules tab's Sync button). The overlay launches resources\face-module-sync.ps1
# via CreateProcessW with an absolute path computed from the exe's directory, so the
# tree has to live next to WKOpenVR.exe. SHA-track the sync script as the canary;
# the tree is copied wholesale below.
$srcResourcesDir = Join-Path $PSScriptRoot "build\artifacts\Release\resources"
$srcSyncScript   = Join-Path $srcResourcesDir "face-module-sync.ps1"
if (-not (Test-Path -LiteralPath $srcSyncScript)) { throw "Build artifact missing: $srcSyncScript" }

# Deployed copies.
$dstExe        = Join-Path $InstallDir "WKOpenVR.exe"
$dstOpenVR     = Join-Path $InstallDir "openvr_api.dll"
$dstDiscordRpc = Join-Path $InstallDir "discord-rpc.dll"
$dstManifest  = Join-Path $InstallDir "manifest.vrmanifest"
$dstIcon      = Join-Path $InstallDir "dashboard_icon.png"
$dstDriverRoot = Join-Path $SteamVRDriversDir "01wkopenvr"
$dstDriverDir = Join-Path $dstDriverRoot "bin\win64"
$dstDll       = Join-Path $dstDriverDir "driver_01wkopenvr.dll"
$dstHostDir   = Join-Path $dstDriverRoot "resources\facetracking\host"
$dstHostExe   = Join-Path $dstHostDir "OpenVRPair.FaceModuleHost.exe"
$dstTrHostDir = Join-Path $dstDriverRoot "resources\translator\host"
$dstTrHostExe = Join-Path $dstTrHostDir "WKOpenVR.TranslatorHost.exe"
$dstResourcesDir = Join-Path $InstallDir "resources"
$dstSyncScript   = Join-Path $dstResourcesDir "face-module-sync.ps1"

$srcExeSha        = Get-Sha $srcExe
$srcOpenVRSha     = Get-Sha $srcOpenVR
$srcDiscordRpcSha = Get-Sha $srcDiscordRpc
$srcDllSha        = Get-Sha $srcDll
$srcManifestSha = Get-Sha $srcManifest
$srcIconSha     = Get-Sha $srcIcon
$srcHostExeSha   = if ($hostPresent) { Get-Sha $srcHostExe } else { $null }
$srcTrHostExeSha = if ($trHostPresent) { Get-Sha $srcTrHostExe } else { $null }
$srcSyncScriptSha = Get-Sha $srcSyncScript
$dstExeSha        = Get-Sha $dstExe
$dstOpenVRSha     = Get-Sha $dstOpenVR
$dstDiscordRpcSha = Get-Sha $dstDiscordRpc
$dstDllSha        = Get-Sha $dstDll
$dstManifestSha  = Get-Sha $dstManifest
$dstIconSha      = Get-Sha $dstIcon
$dstHostExeSha   = Get-Sha $dstHostExe
$dstTrHostExeSha = Get-Sha $dstTrHostExe
$dstSyncScriptSha = Get-Sha $dstSyncScript

Write-Host ""
Write-Host ("Source exe:           {0}" -f $srcExeSha)
Write-Host ("Deployed exe:         {0}" -f $dstExeSha)
Write-Host ("Source openvr_api:    {0}" -f $srcOpenVRSha)
Write-Host ("Deployed openvr_api:  {0}" -f $dstOpenVRSha)
Write-Host ("Source discord-rpc:   {0}" -f $srcDiscordRpcSha)
Write-Host ("Deployed discord-rpc: {0}" -f $dstDiscordRpcSha)
Write-Host ("Source DLL:         {0}" -f $srcDllSha)
Write-Host ("Deployed DLL:       {0}" -f $dstDllSha)
Write-Host ("Source manifest:    {0}" -f $srcManifestSha)
Write-Host ("Deployed manifest:  {0}" -f $dstManifestSha)
Write-Host ("Source icon:        {0}" -f $srcIconSha)
Write-Host ("Deployed icon:      {0}" -f $dstIconSha)
Write-Host ("Source sync-script: {0}" -f $srcSyncScriptSha)
Write-Host ("Deployed sync-scr:  {0}" -f $dstSyncScriptSha)
if ($hostPresent) {
	Write-Host ("Source host:       {0}" -f $srcHostExeSha)
	Write-Host ("Deployed host:     {0}" -f $dstHostExeSha)
} else {
	Write-Host "Source host:       (not built; OPENVR_PAIR_BUILD_FACE_HOST=OFF or .NET 10 SDK missing)"
}
if ($trHostPresent) {
	Write-Host ("Source tr-host:    {0}" -f $srcTrHostExeSha)
	Write-Host ("Deployed tr-host:  {0}" -f $dstTrHostExeSha)
} else {
	Write-Host "Source tr-host:    (not built; OPENVR_PAIR_BUILD_TRANSLATOR_HOST=OFF)"
}

$exeStale        = ($srcExeSha -ne $dstExeSha)
$openVRStale     = ($srcOpenVRSha -ne $dstOpenVRSha)
$discordRpcStale = ($srcDiscordRpcSha -ne $dstDiscordRpcSha)
$driverStale     = ($srcDllSha -ne $dstDllSha)
$manifestStale  = ($srcManifestSha -ne $dstManifestSha)
$iconStale      = ($srcIconSha -ne $dstIconSha)
$resourcesStale = ($srcSyncScriptSha -ne $dstSyncScriptSha)
$hostStale      = $hostPresent -and ($srcHostExeSha -ne $dstHostExeSha)
$trHostStale    = $trHostPresent -and ($srcTrHostExeSha -ne $dstTrHostExeSha)

# Detect stale Start Menu shortcuts. The Modules-tab toggle helper and the
# NSIS installer both stamp distinct --launch arguments on each .lnk so
# Windows Search treats them as separate items; if quick.ps1 deployed a
# binary update without refreshing those arguments (or any shortcut still
# carries the pre-fix empty Arguments string), Windows collapses every
# WKOpenVR shortcut back into a single search result. Reading .lnk
# metadata via WScript.Shell does NOT need admin; rewriting them does, so
# the rewrite still happens inside the elevated helper.
$shortcutArgMap = @{
	'WKOpenVR.lnk'                    = '--launch=umbrella'
	'WKOpenVR - Space Calibrator.lnk' = '--launch=calibration'
	'WKOpenVR - Smoothing.lnk'        = '--launch=smoothing'
	'WKOpenVR - Input Health.lnk'     = '--launch=inputhealth'
	'WKOpenVR - Face Tracking.lnk'    = '--launch=facetracking'
}
$startMenuDir = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\WKOpenVR'
$shortcutsStale = $false
if (Test-Path -LiteralPath $startMenuDir) {
	$wshProbe = New-Object -ComObject WScript.Shell
	foreach ($lnk in (Get-ChildItem -LiteralPath $startMenuDir -Filter '*.lnk' -File -ErrorAction SilentlyContinue)) {
		$expected = $shortcutArgMap[$lnk.Name]
		if ($null -eq $expected) { continue }
		$sc = $wshProbe.CreateShortcut($lnk.FullName)
		if ($sc.Arguments -ne $expected -or $sc.TargetPath -ne $dstExe) {
			$shortcutsStale = $true
			break
		}
	}
}

if (-not $exeStale -and -not $openVRStale -and -not $discordRpcStale -and -not $driverStale -and -not $manifestStale -and -not $iconStale -and -not $resourcesStale -and -not $hostStale -and -not $trHostStale -and -not $shortcutsStale) {
	Write-Host ""
	Write-Host "Already up to date. Deployed build: $(Resolve-Version $dstExe)"
	exit 0
}

Write-Host ""
if ($exeStale)        { Write-Host "Overlay exe needs redeploy." }
if ($openVRStale)     { Write-Host "openvr_api.dll needs redeploy." }
if ($discordRpcStale) { Write-Host "discord-rpc.dll needs redeploy." }
if ($driverStale)     { Write-Host "Driver DLL needs redeploy." }
if ($manifestStale)   { Write-Host "vrmanifest needs redeploy." }
if ($iconStale)       { Write-Host "Dashboard icon needs redeploy." }
if ($resourcesStale)  { Write-Host "Overlay resources tree needs redeploy." }
if ($hostStale)       { Write-Host "FaceModuleHost tree needs redeploy." }
if ($trHostStale)     { Write-Host "TranslatorHost tree needs redeploy." }
if ($shortcutsStale)  { Write-Host "Start Menu shortcuts need refresh (distinct --launch args)." }

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
$helperOut = Join-Path $env:TEMP "WKOpenVR-deploy-stdout.log"
$helperErr = Join-Path $env:TEMP "WKOpenVR-deploy-stderr.log"
$resultFile = Join-Path $env:TEMP "WKOpenVR-deploy-result.txt"
foreach ($p in @($helperOut, $helperErr, $resultFile)) { if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force } }

$hostPresentBool = if ($hostPresent) { '$true' } else { '$false' }
$trHostPresentBool = if ($trHostPresent) { '$true' } else { '$false' }

$helperScript = @"
`$ErrorActionPreference = 'Continue'
try {
	New-Item -ItemType Directory -Force -Path '$InstallDir' | Out-Null
	Copy-Item -LiteralPath '$srcExe' -Destination '$dstExe' -Force
	# openvr_api.dll lives next to the umbrella exe. The Windows loader resolves
	# it from the exe's directory at startup; missing -> "code execution cannot
	# proceed because openvr_api.dll was not found" before main() ever runs.
	Copy-Item -LiteralPath '$srcOpenVR' -Destination '$dstOpenVR' -Force
	# discord-rpc.dll is the Rich Presence runtime; loaded on first Discord_Initialize call.
	Copy-Item -LiteralPath '$srcDiscordRpc' -Destination '$dstDiscordRpc' -Force
	New-Item -ItemType Directory -Force -Path '$dstDriverDir' | Out-Null

	# Park the live DLL aside so SteamVR can still load it if it has it open.
	# Best-effort: a previous backup may itself be locked.
	if (Test-Path -LiteralPath '$dstDll') {
		`$stamp = (Get-Date).ToString('yyyyMMddHHmmss')
		`$bak = '$dstDll' + '.old.' + `$stamp
		try { Rename-Item -LiteralPath '$dstDll' -NewName (Split-Path -Leaf `$bak) -Force } catch { }
	}
	Copy-Item -LiteralPath '$srcDll' -Destination '$dstDll' -Force

	# vrmanifest sits next to the exe so WKOpenVR.exe can resolve it via
	# GetModuleFileName + replace_filename at startup.
	Copy-Item -LiteralPath '$srcManifest' -Destination '$dstManifest' -Force

	# Dashboard overlay thumbnail. VrOverlayHost loads this exe-relative
	# at SetOverlayFromFile time.
	Copy-Item -LiteralPath '$srcIcon' -Destination '$dstIcon' -Force

	# Overlay-side resources tree (currently: face-module-sync.ps1 for the
	# FaceTracking Modules tab's Sync button; future overlay scripts land here
	# too). The overlay launches resources\face-module-sync.ps1 via
	# CreateProcessW with an absolute path derived from the exe's directory --
	# missing this tree means the Sync button silently fails ("Result JSON
	# parse error"). Use -Path (not -LiteralPath) so the * actually globs.
	New-Item -ItemType Directory -Force -Path '$dstResourcesDir' | Out-Null
	Copy-Item -Path (Join-Path '$srcResourcesDir' '*') -Destination '$dstResourcesDir' -Recurse -Force

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

	# Translator host sidecar. Same rename-aside pattern as FaceModuleHost so a
	# SteamVR session holding the exe open does not block the copy.
	if ($trHostPresentBool) {
		`$trHostSrc = '$srcTrHostDir'
		`$trHostDst = '$dstTrHostDir'
		New-Item -ItemType Directory -Force -Path `$trHostDst | Out-Null
		`$trHostExeDst = Join-Path `$trHostDst 'WKOpenVR.TranslatorHost.exe'
		if (Test-Path -LiteralPath `$trHostExeDst) {
			`$stamp = (Get-Date).ToString('yyyyMMddHHmmss')
			`$bak = `$trHostExeDst + '.old.' + `$stamp
			try { Rename-Item -LiteralPath `$trHostExeDst -NewName (Split-Path -Leaf `$bak) -Force } catch { }
		}
		Get-ChildItem -LiteralPath `$trHostDst -File | Where-Object { `$_.Name -notlike '*.old.*' } | Remove-Item -Force
		Copy-Item -Path (Join-Path `$trHostSrc '*') -Destination `$trHostDst -Recurse -Force
	}

	# Refresh existing Start Menu shortcuts so each one carries its
	# distinct --launch argument. Without distinct args, Windows Search
	# dedupes Start Menu entries by (target + arguments) and only one of
	# the WKOpenVR .lnk files surfaces when the user searches for a
	# feature name. The NSIS installer sets these args on first install;
	# quick.ps1 re-applies them on every deploy so shortcuts dropped by
	# an older build pick up the latest mapping and any .lnk whose target
	# fell behind a quick.ps1-only install update is repointed at the
	# current exe path. Only known filenames are touched -- unrelated
	# .lnks in the WKOpenVR Start Menu folder are left alone.
	`$smDir = Join-Path `$env:ProgramData 'Microsoft\Windows\Start Menu\Programs\WKOpenVR'
	if (Test-Path -LiteralPath `$smDir) {
		`$argMap = @{
			'WKOpenVR.lnk'                    = '--launch=umbrella'
			'WKOpenVR - Space Calibrator.lnk' = '--launch=calibration'
			'WKOpenVR - Smoothing.lnk'        = '--launch=smoothing'
			'WKOpenVR - Input Health.lnk'     = '--launch=inputhealth'
			'WKOpenVR - Face Tracking.lnk'    = '--launch=facetracking'
		}
		`$wsh = New-Object -ComObject WScript.Shell
		Get-ChildItem -LiteralPath `$smDir -Filter '*.lnk' -File | ForEach-Object {
			`$arg = `$argMap[`$_.Name]
			if (`$null -eq `$arg) { return }
			`$sc = `$wsh.CreateShortcut(`$_.FullName)
			`$sc.TargetPath   = '$dstExe'
			`$sc.Arguments    = `$arg
			`$sc.IconLocation = '$dstExe' + ',0'
			`$sc.Description  = 'Open WKOpenVR'
			`$sc.Save()
		}
	}

	`$exeSha        = (Get-FileHash -LiteralPath '$dstExe' -Algorithm SHA256).Hash
	`$openVRSha     = (Get-FileHash -LiteralPath '$dstOpenVR' -Algorithm SHA256).Hash
	`$discordRpcSha = (Get-FileHash -LiteralPath '$dstDiscordRpc' -Algorithm SHA256).Hash
	`$dllSha        = (Get-FileHash -LiteralPath '$dstDll' -Algorithm SHA256).Hash
	`$manifestSha  = (Get-FileHash -LiteralPath '$dstManifest' -Algorithm SHA256).Hash
	`$iconSha      = (Get-FileHash -LiteralPath '$dstIcon' -Algorithm SHA256).Hash
	`$syncScriptSha = (Get-FileHash -LiteralPath '$dstSyncScript' -Algorithm SHA256).Hash
	`$hostSha      = if (Test-Path -LiteralPath '$dstHostExe') { (Get-FileHash -LiteralPath '$dstHostExe' -Algorithm SHA256).Hash } else { '' }
	Set-Content -LiteralPath '$resultFile' -Value ("OK`n" + `$exeSha + "`n" + `$openVRSha + "`n" + `$dllSha + "`n" + `$manifestSha + "`n" + `$iconSha + "`n" + `$syncScriptSha + "`n" + `$hostSha)
	exit 0
} catch {
	Set-Content -LiteralPath '$resultFile' -Value ("ERR`n" + `$_.Exception.Message)
	exit 1
}
"@

$helperPath = Join-Path $env:TEMP "WKOpenVR-deploy.ps1"
Set-Content -LiteralPath $helperPath -Value $helperScript -Encoding UTF8

# One-time cleanup: if the old driver dir still exists under the SteamVR
# drivers folder, rename it so SteamVR stops trying to load it.  Do NOT
# delete -- the user can clean up manually after verifying the new driver works.
$oldDriverRoot = Join-Path $SteamVRDriversDir "01openvrpair"
if (Test-Path -LiteralPath $oldDriverRoot) {
	$renamedOldDriver = $oldDriverRoot + ".old-pre-wkopenvr-rename"
	if (-not (Test-Path -LiteralPath $renamedOldDriver)) {
		try {
			Rename-Item -LiteralPath $oldDriverRoot -NewName (Split-Path -Leaf $renamedOldDriver) -Force
			Write-Host "Renamed old driver dir $oldDriverRoot -> $renamedOldDriver (safe to delete after verifying new driver)"
		} catch {
			Write-Host "Could not rename old driver dir $oldDriverRoot -- rename it manually: $_" -ForegroundColor Yellow
		}
	}
}

# Similarly note the old install dir if it still exists.
$oldInstallDir = "C:\Program Files\OpenVR-Pair"
if (Test-Path -LiteralPath $oldInstallDir) {
	Write-Host "Note: old install dir $oldInstallDir still exists. You can remove it after verifying the new install." -ForegroundColor Yellow
}

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
$postExeSha        = Get-Sha $dstExe
$postOpenVRSha     = Get-Sha $dstOpenVR
$postDiscordRpcSha = Get-Sha $dstDiscordRpc
$postDllSha        = Get-Sha $dstDll
$postManifestSha   = Get-Sha $dstManifest
$postIconSha       = Get-Sha $dstIcon
$postSyncScriptSha = Get-Sha $dstSyncScript
$postHostExeSha    = Get-Sha $dstHostExe
$postTrHostExeSha  = Get-Sha $dstTrHostExe

$exeOk         = ($postExeSha -eq $srcExeSha)
$openVROk      = ($postOpenVRSha -eq $srcOpenVRSha)
$discordRpcOk  = ($postDiscordRpcSha -eq $srcDiscordRpcSha)
$dllOk         = ($postDllSha -eq $srcDllSha)
$manifestOk    = ($postManifestSha -eq $srcManifestSha)
$iconOk        = ($postIconSha -eq $srcIconSha)
$syncScriptOk  = ($postSyncScriptSha -eq $srcSyncScriptSha)
$hostOk        = (-not $hostPresent) -or ($postHostExeSha -eq $srcHostExeSha)
$trHostOk      = (-not $trHostPresent) -or ($postTrHostExeSha -eq $srcTrHostExeSha)

Write-Host ""
Write-Host ("Post-copy exe:          {0} {1}" -f $postExeSha,        ($(if ($exeOk)        { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy openvr_api:   {0} {1}" -f $postOpenVRSha,     ($(if ($openVROk)     { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy discord-rpc:  {0} {1}" -f $postDiscordRpcSha, ($(if ($discordRpcOk) { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy DLL:          {0} {1}" -f $postDllSha,        ($(if ($dllOk)        { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy manifest:    {0} {1}" -f $postManifestSha,   ($(if ($manifestOk)   { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy icon:        {0} {1}" -f $postIconSha,       ($(if ($iconOk)       { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy sync-script: {0} {1}" -f $postSyncScriptSha, ($(if ($syncScriptOk) { "OK" } else { "MISMATCH" })))
if ($hostPresent) {
	Write-Host ("Post-copy host:        {0} {1}" -f $postHostExeSha, ($(if ($hostOk) { "OK" } else { "MISMATCH" })))
}
if ($trHostPresent) {
	Write-Host ("Post-copy tr-host:     {0} {1}" -f $postTrHostExeSha, ($(if ($trHostOk) { "OK" } else { "MISMATCH" })))
}

if (-not $exeOk -or -not $openVROk -or -not $discordRpcOk -or -not $dllOk -or -not $manifestOk -or -not $iconOk -or -not $syncScriptOk -or -not $hostOk -or -not $trHostOk) {
	$detail = @()
	if (-not $exeOk)        { $detail += "exe at $dstExe still does not match source" }
	if (-not $openVROk)     { $detail += "openvr_api.dll at $dstOpenVR still does not match source" }
	if (-not $discordRpcOk) { $detail += "discord-rpc.dll at $dstDiscordRpc still does not match source" }
	if (-not $dllOk)        { $detail += "driver DLL at $dstDll still does not match source" }
	if (-not $manifestOk)   { $detail += "manifest at $dstManifest still does not match source" }
	if (-not $iconOk)       { $detail += "icon at $dstIcon still does not match source" }
	if (-not $syncScriptOk) { $detail += "face-module-sync.ps1 at $dstSyncScript still does not match source" }
	if (-not $hostOk)       { $detail += "FaceModuleHost.exe at $dstHostExe still does not match source" }
	if (-not $trHostOk)     { $detail += "WKOpenVR.TranslatorHost.exe at $dstTrHostExe still does not match source" }
	throw ("Deploy did not converge: " + ($detail -join "; "))
}

Write-Host ""
Write-Host "Deploy verified. Installed build: $(Resolve-Version $dstExe)"
