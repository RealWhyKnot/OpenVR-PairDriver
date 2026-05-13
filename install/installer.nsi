; WKOpenVR installer.
; Installs the shared overlay and SteamVR driver tree.

!include "MUI2.nsh"

!ifndef ARTIFACTS_BASEDIR
	!define ARTIFACTS_BASEDIR "..\build\artifacts\Release"
!endif
!ifndef DRIVER_BASEDIR
	!define DRIVER_BASEDIR "..\build\driver_wkopenvr"
!endif
!ifndef VERSION
	!define VERSION "0.1.0.0"
!endif

Name "WKOpenVR"
OutFile "..\build\artifacts\Release\WKOpenVR-Installer.exe"
InstallDir "$PROGRAMFILES64\WKOpenVR"
InstallDirRegKey HKLM "Software\WKOpenVR\Main" ""
RequestExecutionLevel admin
ShowInstDetails show

VIProductVersion "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "WKOpenVR"
VIAddVersionKey /LANG=1033 "FileDescription" "WKOpenVR Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "GPL-3.0-only, https://github.com/RealWhyKnot/WKOpenVR"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"

Var vrRuntimePath

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro ResolveRuntimePath
	nsExec::ExecToStack 'powershell -NoProfile -Command "try { ((Get-Content -Raw \"$env:LOCALAPPDATA\openvr\openvrpaths.vrpath\" | ConvertFrom-Json).runtime)[0] } catch { exit 1 }"'
	Pop $0
	Pop $vrRuntimePath
	StrCmp $0 "0" +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "Could not locate the SteamVR runtime path. Launch SteamVR once, then run this installer again."
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

Section "Install"
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Close SteamVR and try again."
		Abort

	SetOutPath "$INSTDIR"
	File /oname=WKOpenVR.exe "${ARTIFACTS_BASEDIR}\WKOpenVR.exe"
	; manifest.vrmanifest sits next to the exe so RegisterApplicationManifest
	; in WKOpenVR can resolve it via GetModuleFileName + replace_filename
	; on first launch and register the overlay with SteamVR for auto-start.
	File "${ARTIFACTS_BASEDIR}\manifest.vrmanifest"
	File "..\LICENSE"
	File /oname=README.md "..\README.md"

	CreateDirectory "$SMPROGRAMS\WKOpenVR"
	CreateShortCut "$SMPROGRAMS\WKOpenVR\WKOpenVR.lnk" "$INSTDIR\WKOpenVR.exe"

	!insertmacro ResolveRuntimePath

	SetOutPath "$vrRuntimePath\drivers\01wkopenvr"
	File "${DRIVER_BASEDIR}\driver.vrdrivermanifest"
	SetOutPath "$vrRuntimePath\drivers\01wkopenvr\resources"
	File "${DRIVER_BASEDIR}\resources\driver.vrresources"
	SetOutPath "$vrRuntimePath\drivers\01wkopenvr\resources\settings"
	File "${DRIVER_BASEDIR}\resources\settings\default.vrsettings"
	SetOutPath "$vrRuntimePath\drivers\01wkopenvr\bin\win64"
	File /oname=driver_01wkopenvr.dll "${DRIVER_BASEDIR}\bin\win64\driver_wkopenvr.dll"

	; FaceTracking host sidecar (.NET 10). Driver's HostSupervisor spawns
	; OpenVRPair.FaceModuleHost.exe from this directory when the
	; enable_facetracking.flag is present. The /r flag pulls in the entire
	; published .NET tree (exe + .deps.json + .runtimeconfig.json + dependent
	; DLLs). The whole folder is missing when the build host doesn't have the
	; .NET 10 SDK; in that case the IfFileExists guard skips the install step
	; and the feature simply runs inert -- driver still loads, just no host.
	IfFileExists "${DRIVER_BASEDIR}\resources\facetracking\host\OpenVRPair.FaceModuleHost.exe" 0 +3
		SetOutPath "$vrRuntimePath\drivers\01wkopenvr\resources\facetracking\host"
		File /r "${DRIVER_BASEDIR}\resources\facetracking\host\*.*"

	WriteRegStr HKLM "Software\WKOpenVR\Main" "" "$INSTDIR"
	WriteRegStr HKLM "Software\WKOpenVR\Driver" "" "$vrRuntimePath"
	WriteRegStr HKLM "Software\WKOpenVR\Main" "Version" "${VERSION}"

	; Register the overlay manifest with SteamVR and flip autolaunch on,
	; so the first SteamVR start after install opens WKOpenVR without
	; the user having to launch the exe by hand. --register-only exits as
	; soon as the in-process registration call completes (no GLFW window).
	DetailPrint "Registering WKOpenVR overlay with SteamVR..."
	ExecWait '"$INSTDIR\WKOpenVR.exe" --register-only' $0
	DetailPrint "Registration exit code: $0"

	WriteUninstaller "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR" "DisplayName" "WKOpenVR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR" "DisplayVersion" "${VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR" "Publisher" "RealWhyKnot"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
SectionEnd

Section "Uninstall"
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Close SteamVR and try again."
		Abort

	; Unregister with SteamVR while the exe + manifest still exist on disk.
	; Skipping this would leave wk.wkopenvr pointing at the deleted exe
	; and SteamVR would keep trying to autolaunch a missing binary.
	IfFileExists "$INSTDIR\WKOpenVR.exe" 0 +3
		DetailPrint "Unregistering WKOpenVR overlay from SteamVR..."
		ExecWait '"$INSTDIR\WKOpenVR.exe" --unregister-only' $0

	ReadRegStr $vrRuntimePath HKLM "Software\WKOpenVR\Driver" ""
	StrCmp $vrRuntimePath "" skipDriver
	Delete "$vrRuntimePath\drivers\01wkopenvr\driver.vrdrivermanifest"
	Delete "$vrRuntimePath\drivers\01wkopenvr\resources\driver.vrresources"
	Delete "$vrRuntimePath\drivers\01wkopenvr\resources\settings\default.vrsettings"
	Delete "$vrRuntimePath\drivers\01wkopenvr\bin\win64\driver_01wkopenvr.dll"
	; FaceTracking host tree. RMDir /r is safe here because the path is
	; constructed from the per-install registry value, not user input. Skipped
	; cleanly if the directory was never created (host wasn't installed).
	RMDir /r "$vrRuntimePath\drivers\01wkopenvr\resources\facetracking"
	; Stray enable flag files (toggled on by the Modules tab post-install).
	; Best-effort: leave any other flags alone -- those belong to other
	; feature consumers and survive a WKOpenVR uninstall by design.
	Delete "$vrRuntimePath\drivers\01wkopenvr\resources\enable_facetracking.flag"
	RMDir "$vrRuntimePath\drivers\01wkopenvr\resources\settings"
	RMDir "$vrRuntimePath\drivers\01wkopenvr\resources"
	RMDir "$vrRuntimePath\drivers\01wkopenvr\bin\win64"
	RMDir "$vrRuntimePath\drivers\01wkopenvr\bin"
	RMDir "$vrRuntimePath\drivers\01wkopenvr"
	skipDriver:

	Delete "$INSTDIR\WKOpenVR.exe"
	Delete "$INSTDIR\manifest.vrmanifest"
	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\README.md"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	Delete "$SMPROGRAMS\WKOpenVR\WKOpenVR.lnk"
	RMDir "$SMPROGRAMS\WKOpenVR"

	DeleteRegKey HKLM "Software\WKOpenVR\Driver"
	DeleteRegKey HKLM "Software\WKOpenVR\Main"
	DeleteRegKey /ifempty HKLM "Software\WKOpenVR"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR"
SectionEnd
