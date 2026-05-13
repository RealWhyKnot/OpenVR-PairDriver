; WKOpenVR installer.
; Installs the shared overlay and SteamVR driver tree.
;
; Usage:
;   makensis installer.nsi
;       -> WKOpenVR-Installer.exe  (all features, none pre-enabled)
;
;   makensis /DFEATURE=Calibration  installer.nsi
;   makensis /DFEATURE=Smoothing    installer.nsi
;   makensis /DFEATURE=InputHealth  installer.nsi
;   makensis /DFEATURE=FaceTracking installer.nsi
;       -> WKOpenVR-<Feature>-Setup.exe  (single feature pre-enabled)

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

; Default FEATURE to All when not supplied on the command line.
!ifndef FEATURE
    !define FEATURE "All"
!endif

; Per-feature installer name / output path differ from the umbrella.
!if "${FEATURE}" == "All"
    Name "WKOpenVR"
    OutFile "..\build\artifacts\Release\WKOpenVR-Installer.exe"
!else
    !if "${FEATURE}" == "Calibration"
        Name "WKOpenVR (Calibration)"
        OutFile "..\build\artifacts\Release\WKOpenVR-Calibration-Setup.exe"
    !endif
    !if "${FEATURE}" == "Smoothing"
        Name "WKOpenVR (Smoothing)"
        OutFile "..\build\artifacts\Release\WKOpenVR-Smoothing-Setup.exe"
    !endif
    !if "${FEATURE}" == "InputHealth"
        Name "WKOpenVR (InputHealth)"
        OutFile "..\build\artifacts\Release\WKOpenVR-InputHealth-Setup.exe"
    !endif
    !if "${FEATURE}" == "FaceTracking"
        Name "WKOpenVR (FaceTracking)"
        OutFile "..\build\artifacts\Release\WKOpenVR-FaceTracking-Setup.exe"
    !endif
!endif

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

; --------------------------------------------------------------------------
; ResolveRuntimePath -- populates $vrRuntimePath from openvrpaths.vrpath.
; Always re-resolved at install time so a moved SteamVR is picked up.
; --------------------------------------------------------------------------
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

; --------------------------------------------------------------------------
; CheckProcessNotRunning -- aborts if <ProcessName> (without .exe extension)
; is running. Uses PowerShell Get-Process; exit 0 = running, exit 1 = not found.
; Usage: ${CheckProcessNotRunning} "vrserver" "Message shown on abort."
; --------------------------------------------------------------------------
!macro CheckProcessNotRunning ProcessName AbortMessage
    nsExec::ExecToStack 'powershell -NoProfile -Command "if (Get-Process -Name ''${ProcessName}'' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }"'
    Pop $0
    Pop $1
    StrCmp $0 "0" 0 +3
        MessageBox MB_OK|MB_ICONEXCLAMATION "${AbortMessage}"
        Abort
!macroend
!define CheckProcessNotRunning "!insertmacro CheckProcessNotRunning"

; Shared body for TrimNewlines. NSIS keeps install and uninstall functions in
; separate namespaces ("un." prefix), so each section needs its own copy of
; the function. Defining the body once as a macro avoids duplication drift.
!macro TrimNewlinesBody
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
!macroend

Function TrimNewlines
    !insertmacro TrimNewlinesBody
FunctionEnd

Function un.TrimNewlines
    !insertmacro TrimNewlinesBody
FunctionEnd

; --------------------------------------------------------------------------
Section "Install"
; --------------------------------------------------------------------------
    ; Pre-install checks: SteamVR window, vrserver, WKOpenVR.
    FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
    StrCmp $0 0 +3
        MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Close SteamVR and try again."
        Abort

    ${CheckProcessNotRunning} "vrserver" "vrserver.exe is still running. Close SteamVR completely and try again."
    ${CheckProcessNotRunning} "WKOpenVR" "WKOpenVR is still running. Close WKOpenVR and try again."

    ; Always re-resolve the runtime path so a moved SteamVR is picked up
    ; even when upgrading over a previous install whose registry value is stale.
    !insertmacro ResolveRuntimePath

    SetOutPath "$INSTDIR"
    File /oname=WKOpenVR.exe "${ARTIFACTS_BASEDIR}\WKOpenVR.exe"
    File "${ARTIFACTS_BASEDIR}\openvr_api.dll"
    ; manifest.vrmanifest sits next to the exe so RegisterApplicationManifest
    ; in WKOpenVR can resolve it via GetModuleFileName + replace_filename
    ; on first launch and register the overlay with SteamVR for auto-start.
    File "${ARTIFACTS_BASEDIR}\manifest.vrmanifest"
    File "${ARTIFACTS_BASEDIR}\dashboard_icon.png"
    File "..\LICENSE"
    File /oname=README.md "..\README.md"

    SetOutPath "$INSTDIR\resources"
    File "${ARTIFACTS_BASEDIR}\resources\face-module-sync.ps1"

    CreateDirectory "$SMPROGRAMS\WKOpenVR"
    CreateShortCut "$SMPROGRAMS\WKOpenVR\WKOpenVR.lnk" "$INSTDIR\WKOpenVR.exe"

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
    ;
    ; Labels are required here instead of "+3": File /r expands at compile
    ; time into one runtime instruction per included file, so a numeric
    ; "+3" jump would land somewhere inside the per-file expansion rather
    ; than after it.
    IfFileExists "${DRIVER_BASEDIR}\resources\facetracking\host\OpenVRPair.FaceModuleHost.exe" hasFaceHost skipFaceHost
    hasFaceHost:
        SetOutPath "$vrRuntimePath\drivers\01wkopenvr\resources\facetracking\host"
        File /r "${DRIVER_BASEDIR}\resources\facetracking\host\*.*"
    skipFaceHost:

    ; Drop the feature enable flag when building a per-feature installer.
    ; Content matches what ShellContext::SetFlagPresent writes:
    ;   Set-Content -Value enabled -NoNewline
    !if "${FEATURE}" == "Calibration"
        FileOpen $0 "$vrRuntimePath\drivers\01wkopenvr\resources\enable_calibration.flag" w
        FileWrite $0 "enabled"
        FileClose $0
        DetailPrint "Enabled feature: Calibration"
    !endif
    !if "${FEATURE}" == "Smoothing"
        FileOpen $0 "$vrRuntimePath\drivers\01wkopenvr\resources\enable_smoothing.flag" w
        FileWrite $0 "enabled"
        FileClose $0
        DetailPrint "Enabled feature: Smoothing"
    !endif
    !if "${FEATURE}" == "InputHealth"
        FileOpen $0 "$vrRuntimePath\drivers\01wkopenvr\resources\enable_inputhealth.flag" w
        FileWrite $0 "enabled"
        FileClose $0
        DetailPrint "Enabled feature: InputHealth"
    !endif
    !if "${FEATURE}" == "FaceTracking"
        FileOpen $0 "$vrRuntimePath\drivers\01wkopenvr\resources\enable_facetracking.flag" w
        FileWrite $0 "enabled"
        FileClose $0
        DetailPrint "Enabled feature: FaceTracking"
    !endif

    WriteRegStr HKLM "Software\WKOpenVR\Main" "" "$INSTDIR"
    WriteRegStr HKLM "Software\WKOpenVR\Driver" "" "$vrRuntimePath"
    WriteRegStr HKLM "Software\WKOpenVR\Main" "Version" "${VERSION}"
    WriteRegStr HKLM "Software\WKOpenVR\Main" "Features" "${FEATURE}"

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

; --------------------------------------------------------------------------
Section "Uninstall"
; --------------------------------------------------------------------------
    ; Pre-uninstall checks: removing driver files while vrserver holds them
    ; open silently fails and leaves orphan DLLs in the driver directory.
    FindWindow $0 "Qt5QWindowIcon" "SteamVR Status"
    StrCmp $0 0 +3
        MessageBox MB_OK|MB_ICONEXCLAMATION "SteamVR is still running. Close SteamVR and try again."
        Abort

    ${CheckProcessNotRunning} "vrserver" "vrserver.exe is still running. Close SteamVR completely and try again."
    ${CheckProcessNotRunning} "WKOpenVR" "WKOpenVR is still running. Close WKOpenVR and try again."

    ; Unregister with SteamVR while the exe + manifest still exist on disk.
    ; Sleep gives SteamVR a moment to release the manifest handle before
    ; we delete the file, reducing the chance of a locked-file error.
    IfFileExists "$INSTDIR\WKOpenVR.exe" 0 skipUnregister
        DetailPrint "Unregistering WKOpenVR overlay from SteamVR..."
        ExecWait '"$INSTDIR\WKOpenVR.exe" --unregister-only' $0
        DetailPrint "Unregistration exit code: $0"
        Sleep 500
    skipUnregister:

    ; Re-resolve SteamVR runtime path. Fall back to the registry value when
    ; openvrpaths.vrpath is missing (SteamVR uninstalled after WKOpenVR).
    ClearErrors
    nsExec::ExecToStack 'powershell -NoProfile -Command "try { ((Get-Content -Raw \"$env:LOCALAPPDATA\openvr\openvrpaths.vrpath\" | ConvertFrom-Json).runtime)[0] } catch { exit 1 }"'
    Pop $0
    Pop $vrRuntimePath
    StrCmp $0 "0" runtimeResolved
        ; Resolution failed -- fall back to the registry value.
        ReadRegStr $vrRuntimePath HKLM "Software\WKOpenVR\Driver" ""
    runtimeResolved:
    StrCmp $vrRuntimePath "" skipDriverRemoval
    Push $vrRuntimePath
    Call un.TrimNewlines
    Pop $vrRuntimePath
    DetailPrint "SteamVR runtime path for uninstall: $vrRuntimePath"

    ; ---- Remove the 01wkopenvr driver tree --------------------------------
    ; Step 1: delete every known file individually so the uninstall log
    ; shows exactly what came off disk. Step 2: RMDir /r the dedicated
    ; driver directory to catch anything we did not predict (driver-side
    ; logs, future feature files, etc.). The 01wkopenvr directory belongs
    ; entirely to our driver, so the recursive wipe is safe.
    ClearErrors
    Delete "$vrRuntimePath\drivers\01wkopenvr\driver.vrdrivermanifest"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\driver.vrresources"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\settings\default.vrsettings"
    Delete "$vrRuntimePath\drivers\01wkopenvr\bin\win64\driver_01wkopenvr.dll"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\enable_calibration.flag"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\enable_smoothing.flag"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\enable_inputhealth.flag"
    Delete "$vrRuntimePath\drivers\01wkopenvr\resources\enable_facetracking.flag"
    RMDir /r "$vrRuntimePath\drivers\01wkopenvr\resources\facetracking"
    RMDir /r "$vrRuntimePath\drivers\01wkopenvr"

    ; ---- Best-effort legacy driver cleanup (pre-rename product) -----------
    ; Same recursive-wipe pattern; the 01openvrpair directory was the
    ; pre-rename equivalent of 01wkopenvr and likewise belongs only to
    ; this product.
    ClearErrors
    IfFileExists "$vrRuntimePath\drivers\01openvrpair" 0 skipLegacyDriver
        RMDir /r "$vrRuntimePath\drivers\01openvrpair"
    skipLegacyDriver:

    skipDriverRemoval:

    ; ---- Remove $INSTDIR contents -----------------------------------------
    ; Explicit deletes log each known file; the final RMDir /r catches any
    ; future additions (extra resources, .ini snippets, future helper exes)
    ; so a v2 of the installer doesn't leak files when uninstalling over
    ; a v1 install. $INSTDIR is the user-chosen install directory; any
    ; files in it were placed there by us.
    ClearErrors
    Delete "$INSTDIR\WKOpenVR.exe"
    Delete "$INSTDIR\openvr_api.dll"
    Delete "$INSTDIR\manifest.vrmanifest"
    Delete "$INSTDIR\dashboard_icon.png"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\resources"
    RMDir /r "$INSTDIR"

    ; ---- Best-effort legacy install dir cleanup ---------------------------
    ClearErrors
    IfFileExists "$PROGRAMFILES64\OpenVR-Pair" 0 skipLegacyInstDir
        RMDir /r "$PROGRAMFILES64\OpenVR-Pair"
    skipLegacyInstDir:

    ; ---- User data under %USERPROFILE%\AppData\LocalLow\ ------------------
    ; LocalLow is a SIBLING of Local, not a child, so $LOCALAPPDATA\Low is
    ; the wrong path -- it would resolve to .\AppData\Local\Low\ which never
    ; exists. $PROFILE is %USERPROFILE%, so $PROFILE\AppData\LocalLow lines
    ; up with what SHGetKnownFolderPath(FOLDERID_LocalAppDataLow) returns
    ; for the same user that runs the uninstaller.
    ClearErrors
    StrCpy $0 "$PROFILE\AppData\LocalLow\WKOpenVR"
    IfFileExists "$0" 0 skipUserData
        RMDir /r "$0"
    skipUserData:

    ; ---- Best-effort legacy user data (pre-rename product) ----------------
    ClearErrors
    StrCpy $0 "$PROFILE\AppData\LocalLow\OpenVR-Pair"
    IfFileExists "$0" 0 skipLegacyUserData
        RMDir /r "$0"
    skipLegacyUserData:

    ; ---- Start Menu shortcut ----------------------------------------------
    ClearErrors
    Delete "$SMPROGRAMS\WKOpenVR\WKOpenVR.lnk"
    RMDir "$SMPROGRAMS\WKOpenVR"

    ; ---- Registry cleanup -------------------------------------------------
    ClearErrors
    DeleteRegKey HKLM "Software\WKOpenVR\Driver"
    DeleteRegKey HKLM "Software\WKOpenVR\Main"
    DeleteRegKey /ifempty HKLM "Software\WKOpenVR"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\WKOpenVR"
    ; Legacy registry keys from the pre-rename product.
    DeleteRegKey HKLM "Software\OpenVR-Pair"
    ; The HKCU\Software\OpenVR-WKSpaceCalibrator key is what existed before
    ; the WKOpenVR rename; Migration.cpp copies it into the WKOpenVR-prefixed
    ; key on first launch (kNewKey there is "Software\WKOpenVR-SpaceCalibrator").
    ; Once the migration ran the old key may be empty but Migration leaves it
    ; in place; the new key is what every subsequent launch writes. Delete both.
    DeleteRegKey HKCU "Software\OpenVR-WKSpaceCalibrator"
    DeleteRegKey HKCU "Software\WKOpenVR-SpaceCalibrator"

    ClearErrors
    MessageBox MB_OK|MB_ICONINFORMATION "WKOpenVR uninstalled."
SectionEnd
