;--------------------------------
; OpenVR-PairDriver standalone installer.
;
; Installs the shared SteamVR driver tree at <SteamVR>\drivers\01openvrpair\
; and registers an uninstaller. No overlay binary, no Start Menu shortcut --
; the driver is consumed by OpenVR-SpaceCalibrator and OpenVR-Smoothing, each
; of which drops its own enable_*.flag file when its installer runs.
;
; Most users will not run this installer directly. SC's and Smoothing's
; installers each bundle the driver tree and lay it down themselves so a
; user only ever runs one installer per consumer. This standalone exists
; for users who want only the driver, or for repair scenarios.
;
; Flag-file ownership: enable_calibration.flag is owned by SC's installer;
; enable_smoothing.flag is owned by Smoothing's installer. This installer
; never creates or deletes either, so a re-install of the shared driver
; preserves whichever flags the consumers have already dropped.

;--------------------------------
;Includes

	!include "MUI2.nsh"
	!include "FileFunc.nsh"

;--------------------------------
;General

	!ifndef ARTIFACTS_BASEDIR
		!define ARTIFACTS_BASEDIR "..\build\driver_openvrpair"
	!endif

	Name "OpenVR-PairDriver"
	OutFile "OpenVR-PairDriver-Setup.exe"
	; The actual driver payload lands under SteamVR's drivers folder. This
	; install dir holds Uninstall.exe and the registry record only.
	InstallDir "$PROGRAMFILES64\OpenVR-PairDriver"
	InstallDirRegKey HKLM "Software\OpenVR-PairDriver\Main" ""
	RequestExecutionLevel admin
	ShowInstDetails show

	!ifndef VERSION
		!define VERSION "0.1.0.0"
	!endif
	VIProductVersion "${VERSION}"
	VIAddVersionKey /LANG=1033 "ProductName" "OpenVR-PairDriver"
	VIAddVersionKey /LANG=1033 "FileDescription" "OpenVR-PairDriver Installer"
	VIAddVersionKey /LANG=1033 "LegalCopyright" "MIT, https://github.com/RealWhyKnot/OpenVR-PairDriver"
	VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
	VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"

;--------------------------------
;Variables

VAR vrRuntimePath
VAR upgradeInstallation

;--------------------------------
;Interface Settings

	!define MUI_ABORTWARNING

;--------------------------------
;Pages

	!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
	!define MUI_PAGE_CUSTOMFUNCTION_PRE dirPre
	!insertmacro MUI_PAGE_DIRECTORY
	!insertmacro MUI_PAGE_INSTFILES

	!insertmacro MUI_UNPAGE_CONFIRM
	!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
;Languages

	!insertmacro MUI_LANGUAGE "English"

;--------------------------------
;Functions

Function dirPre
	StrCmp $upgradeInstallation "true" 0 +2
		Abort
FunctionEnd

Function .onInit
	StrCpy $upgradeInstallation "false"

	ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver" "UninstallString"
	StrCmp $R0 "" done

	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION \
			"SteamVR is still running. Cannot install the shared driver while SteamVR holds it open.$\nPlease close SteamVR and try again."
		Abort

	MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
		"OpenVR-PairDriver is already installed.$\n$\nClick OK to upgrade or Cancel to abort." \
		IDOK upgrade
	Abort

	upgrade:
		StrCpy $upgradeInstallation "true"
	done:
FunctionEnd

;--------------------------------
;Helpers

; Resolve <SteamVR> runtime root by parsing %LOCALAPPDATA%\openvr\openvrpaths.vrpath
; via PowerShell. Sets $vrRuntimePath on success; aborts on failure.
!macro ResolveRuntimePath
	nsExec::ExecToStack 'powershell -NoProfile -Command "try { ((Get-Content -Raw \"$env:LOCALAPPDATA\openvr\openvrpaths.vrpath\" | ConvertFrom-Json).runtime)[0] } catch { exit 1 }"'
	Pop $0
	Pop $vrRuntimePath
	StrCmp $0 "0" +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "Could not locate the SteamVR runtime path. Make sure SteamVR has been launched at least once.$\n$\nDetails: $vrRuntimePath"
		Abort
	Push $vrRuntimePath
	Call TrimNewlines
	Pop $vrRuntimePath
	DetailPrint "SteamVR runtime path: $vrRuntimePath"
!macroend

Function TrimNewlines
	Exch $R0
	Push $R1
	Push $R2
	StrCpy $R1 0
	loop:
		IntOp $R1 $R1 - 1
		StrCpy $R2 $R0 1 $R1
		StrCmp $R2 "$\r" loop
		StrCmp $R2 "$\n" loop
		IntOp $R1 $R1 + 1
		IntCmp $R1 0 noTrim
		StrCpy $R0 $R0 $R1
	noTrim:
	Pop $R2
	Pop $R1
	Exch $R0
FunctionEnd

;--------------------------------
;Installer

Section "Install" SecInstall

	; Upgrade path: the silent-mode uninstaller call wipes the previous
	; install record and bookkeeping files. The driver tree itself is
	; refreshed by the File commands below regardless.
	StrCmp $upgradeInstallation "true" 0 noupgrade
		DetailPrint "Removing previous installation record..."
		ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR'
		Delete $INSTDIR\Uninstall.exe
	noupgrade:

	SetOutPath "$INSTDIR"
	File "..\LICENSE"
	File /oname=README.md "..\README.md"

	!insertmacro ResolveRuntimePath

	; Lay the driver tree down at <SteamVR>\drivers\01openvrpair\.
	SetOutPath "$vrRuntimePath\drivers\01openvrpair"
	File "${ARTIFACTS_BASEDIR}\driver.vrdrivermanifest"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\resources"
	File "${ARTIFACTS_BASEDIR}\resources\driver.vrresources"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\resources\settings"
	File "${ARTIFACTS_BASEDIR}\resources\settings\default.vrsettings"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\bin\win64"
	File "${ARTIFACTS_BASEDIR}\bin\win64\driver_openvrpair.dll"

	WriteRegStr HKLM "Software\OpenVR-PairDriver\Main"   "" $INSTDIR
	WriteRegStr HKLM "Software\OpenVR-PairDriver\Driver" "" $vrRuntimePath
	WriteRegStr HKLM "Software\OpenVR-PairDriver\Main"   "Version" "${VERSION}"

	WriteUninstaller "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver" "DisplayName"     "OpenVR-PairDriver"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver" "DisplayVersion"  "${VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver" "Publisher"       "RealWhyKnot"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""

	DetailPrint "Driver installed to $vrRuntimePath\drivers\01openvrpair\"
	DetailPrint "No feature flags dropped. Install OpenVR-SpaceCalibrator and/or OpenVR-Smoothing to wire up subsystems."

SectionEnd

;--------------------------------
;Uninstaller
;
; Removes the driver-tree files we own. enable_*.flag files are owned by the
; consumer installers and are NEVER touched here, so re-installing or
; reinstalling the shared driver preserves the consumers' opt-in state.

Section "Uninstall"
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Cannot uninstall while SteamVR holds the driver open.$\nPlease close SteamVR and try again."
		Abort

	ReadRegStr $vrRuntimePath HKLM "Software\OpenVR-PairDriver\Driver" ""
	StrCmp $vrRuntimePath "" skipPayload

	; Warn (non-blocking) if either consumer's flag is still present so the
	; user knows their overlay is about to lose its driver.
	IfFileExists "$vrRuntimePath\drivers\01openvrpair\resources\enable_calibration.flag" 0 +2
		MessageBox MB_OK|MB_ICONEXCLAMATION "enable_calibration.flag is still present. OpenVR-SpaceCalibrator will stop working until you uninstall it or reinstall the shared driver."
	IfFileExists "$vrRuntimePath\drivers\01openvrpair\resources\enable_smoothing.flag" 0 +2
		MessageBox MB_OK|MB_ICONEXCLAMATION "enable_smoothing.flag is still present. OpenVR-Smoothing will stop working until you uninstall it or reinstall the shared driver."

	DetailPrint "Removing driver-tree files at $vrRuntimePath\drivers\01openvrpair\"
	Delete "$vrRuntimePath\drivers\01openvrpair\driver.vrdrivermanifest"
	Delete "$vrRuntimePath\drivers\01openvrpair\resources\driver.vrresources"
	Delete "$vrRuntimePath\drivers\01openvrpair\resources\settings\default.vrsettings"
	Delete "$vrRuntimePath\drivers\01openvrpair\bin\win64\driver_openvrpair.dll"
	; RMDir without /r so any consumer-owned flag files keep the resources/
	; folder alive.
	RMDir "$vrRuntimePath\drivers\01openvrpair\resources\settings"
	RMDir "$vrRuntimePath\drivers\01openvrpair\resources"
	RMDir "$vrRuntimePath\drivers\01openvrpair\bin\win64"
	RMDir "$vrRuntimePath\drivers\01openvrpair\bin"
	RMDir "$vrRuntimePath\drivers\01openvrpair"

	skipPayload:
	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\README.md"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"

	DeleteRegKey HKLM "Software\OpenVR-PairDriver\Driver"
	DeleteRegKey HKLM "Software\OpenVR-PairDriver\Main"
	DeleteRegKey /ifempty HKLM "Software\OpenVR-PairDriver"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-PairDriver"

SectionEnd
