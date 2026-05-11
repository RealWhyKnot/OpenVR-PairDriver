; OpenVR-Pair installer.
; Installs the shared overlay and SteamVR driver tree.

!include "MUI2.nsh"

!ifndef ARTIFACTS_BASEDIR
	!define ARTIFACTS_BASEDIR "..\build\artifacts\Release"
!endif
!ifndef DRIVER_BASEDIR
	!define DRIVER_BASEDIR "..\build\driver_openvrpair"
!endif
!ifndef VERSION
	!define VERSION "0.1.0.0"
!endif

Name "OpenVR-Pair"
OutFile "..\build\artifacts\Release\OpenVR-Pair-Installer.exe"
InstallDir "$PROGRAMFILES64\OpenVR-Pair"
InstallDirRegKey HKLM "Software\OpenVR-Pair\Main" ""
RequestExecutionLevel admin
ShowInstDetails show

VIProductVersion "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "OpenVR-Pair"
VIAddVersionKey /LANG=1033 "FileDescription" "OpenVR-Pair Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "MIT, https://github.com/RealWhyKnot/OpenVR-WKPairDriver"
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
	File /oname=OpenVR-Pair.exe "${ARTIFACTS_BASEDIR}\OpenVR-Pair.exe"
	File "..\LICENSE"
	File /oname=README.md "..\README.md"

	CreateDirectory "$SMPROGRAMS\OpenVR-Pair"
	CreateShortCut "$SMPROGRAMS\OpenVR-Pair\OpenVR-Pair.lnk" "$INSTDIR\OpenVR-Pair.exe"

	!insertmacro ResolveRuntimePath

	SetOutPath "$vrRuntimePath\drivers\01openvrpair"
	File "${DRIVER_BASEDIR}\driver.vrdrivermanifest"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\resources"
	File "${DRIVER_BASEDIR}\resources\driver.vrresources"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\resources\settings"
	File "${DRIVER_BASEDIR}\resources\settings\default.vrsettings"
	SetOutPath "$vrRuntimePath\drivers\01openvrpair\bin\win64"
	File /oname=driver_01openvrpair.dll "${DRIVER_BASEDIR}\bin\win64\driver_openvrpair.dll"

	WriteRegStr HKLM "Software\OpenVR-Pair\Main" "" "$INSTDIR"
	WriteRegStr HKLM "Software\OpenVR-Pair\Driver" "" "$vrRuntimePath"
	WriteRegStr HKLM "Software\OpenVR-Pair\Main" "Version" "${VERSION}"
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-Pair" "DisplayName" "OpenVR-Pair"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-Pair" "DisplayVersion" "${VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-Pair" "Publisher" "RealWhyKnot"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-Pair" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
SectionEnd

Section "Uninstall"
	FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
	StrCmp $0 0 +3
		MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Close SteamVR and try again."
		Abort

	ReadRegStr $vrRuntimePath HKLM "Software\OpenVR-Pair\Driver" ""
	StrCmp $vrRuntimePath "" skipDriver
	Delete "$vrRuntimePath\drivers\01openvrpair\driver.vrdrivermanifest"
	Delete "$vrRuntimePath\drivers\01openvrpair\resources\driver.vrresources"
	Delete "$vrRuntimePath\drivers\01openvrpair\resources\settings\default.vrsettings"
	Delete "$vrRuntimePath\drivers\01openvrpair\bin\win64\driver_01openvrpair.dll"
	RMDir "$vrRuntimePath\drivers\01openvrpair\resources\settings"
	RMDir "$vrRuntimePath\drivers\01openvrpair\resources"
	RMDir "$vrRuntimePath\drivers\01openvrpair\bin\win64"
	RMDir "$vrRuntimePath\drivers\01openvrpair\bin"
	RMDir "$vrRuntimePath\drivers\01openvrpair"
	skipDriver:

	Delete "$INSTDIR\OpenVR-Pair.exe"
	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\README.md"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	Delete "$SMPROGRAMS\OpenVR-Pair\OpenVR-Pair.lnk"
	RMDir "$SMPROGRAMS\OpenVR-Pair"

	DeleteRegKey HKLM "Software\OpenVR-Pair\Driver"
	DeleteRegKey HKLM "Software\OpenVR-Pair\Main"
	DeleteRegKey /ifempty HKLM "Software\OpenVR-Pair"
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenVR-Pair"
SectionEnd
