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
$srcExe = Join-Path $PSScriptRoot "build\artifacts\Release\OpenVR-Pair.exe"
$srcDll = Join-Path $PSScriptRoot "build\driver_openvrpair\bin\win64\driver_openvrpair.dll"

foreach ($p in @($srcExe, $srcDll)) {
	if (-not (Test-Path -LiteralPath $p)) { throw "Build artifact missing: $p" }
}

# Deployed copies.
$dstExe       = Join-Path $InstallDir "OpenVR-Pair.exe"
$dstDriverDir = Join-Path $SteamVRDriversDir "01openvrpair\bin\win64"
$dstDll       = Join-Path $dstDriverDir "driver_01openvrpair.dll"

$srcExeSha = Get-Sha $srcExe
$srcDllSha = Get-Sha $srcDll
$dstExeSha = Get-Sha $dstExe
$dstDllSha = Get-Sha $dstDll

Write-Host ""
Write-Host ("Source exe:      {0}" -f $srcExeSha)
Write-Host ("Deployed exe:    {0}" -f $dstExeSha)
Write-Host ("Source DLL:      {0}" -f $srcDllSha)
Write-Host ("Deployed DLL:    {0}" -f $dstDllSha)

$exeStale    = ($srcExeSha -ne $dstExeSha)
$driverStale = ($srcDllSha -ne $dstDllSha)

if (-not $exeStale -and -not $driverStale) {
	Write-Host ""
	Write-Host "Already up to date. Deployed build: $(Resolve-Version $dstExe)"
	exit 0
}

Write-Host ""
if ($exeStale)    { Write-Host "Overlay exe needs redeploy." }
if ($driverStale) { Write-Host "Driver DLL needs redeploy." }

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

	`$exeSha = (Get-FileHash -LiteralPath '$dstExe' -Algorithm SHA256).Hash
	`$dllSha = (Get-FileHash -LiteralPath '$dstDll' -Algorithm SHA256).Hash
	Set-Content -LiteralPath '$resultFile' -Value ("OK`n" + `$exeSha + "`n" + `$dllSha)
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
$postExeSha = Get-Sha $dstExe
$postDllSha = Get-Sha $dstDll

$exeOk = ($postExeSha -eq $srcExeSha)
$dllOk = ($postDllSha -eq $srcDllSha)

Write-Host ""
Write-Host ("Post-copy exe:   {0} {1}" -f $postExeSha, ($(if ($exeOk) { "OK" } else { "MISMATCH" })))
Write-Host ("Post-copy DLL:   {0} {1}" -f $postDllSha, ($(if ($dllOk) { "OK" } else { "MISMATCH" })))

if (-not $exeOk -or -not $dllOk) {
	$detail = @()
	if (-not $exeOk) { $detail += "exe at $dstExe still does not match source" }
	if (-not $dllOk) { $detail += "driver DLL at $dstDll still does not match source" }
	throw ("Deploy did not converge: " + ($detail -join "; "))
}

Write-Host ""
Write-Host "Deploy verified. Installed build: $(Resolve-Version $dstExe)"
