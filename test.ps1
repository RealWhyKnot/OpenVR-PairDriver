param(
	# Run the current test binaries without rebuilding first.
	[switch]$SkipBuild,

	# Pass through to build.ps1 when building.
	[switch]$SkipConfigure,

	# Optional GoogleTest filter, for example "Translator*".
	[string]$Filter = ""
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $SkipBuild) {
	$buildArgs = @()
	if ($SkipConfigure) { $buildArgs += "-SkipConfigure" }
	& "$PSScriptRoot\build.ps1" @buildArgs
	if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }
}

$testDir = Join-Path $PSScriptRoot "build\artifacts\Release"
$tests = @(Get-ChildItem -LiteralPath $testDir -Filter "*_tests.exe" -File -ErrorAction SilentlyContinue | Sort-Object Name)
if ($tests.Count -eq 0) {
	throw "No test binaries found under $testDir. Run build.ps1 first."
}

$args = @("--gtest_brief=1")
if ($Filter) {
	$args += "--gtest_filter=$Filter"
}

foreach ($test in $tests) {
	Write-Host ""
	Write-Host ("== Running {0} ==" -f $test.Name)
	& $test.FullName @args
	if ($LASTEXITCODE -ne 0) {
		throw "$($test.Name) failed (exit $LASTEXITCODE)"
	}
}

Write-Host ""
Write-Host ("All {0} test binaries passed." -f $tests.Count)
