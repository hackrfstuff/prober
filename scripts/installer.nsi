!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif

Name "prober v${VERSION}"
OutFile "..\dist\prober-${VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\prober"
InstallDirRegKey HKLM "Software\prober" "InstallDir"
RequestExecutionLevel admin

!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"

  ; Copy dist contents
  File /r "..\dist\prober.exe"
  File /r "..\dist\prober_gui.exe"
  File /nonfatal "..\dist\vc_redist.x64.exe"
  SetOutPath "$INSTDIR\tools"
  File /r "..\dist\tools\*.*"

  SetOutPath "$INSTDIR"

  ; Write uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Registry keys for Add/Remove Programs
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "DisplayName" "prober v${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "Publisher" "hackrfstuff"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober" "EstimatedSize" $0

  ; Save install dir
  WriteRegStr HKLM "Software\prober" "InstallDir" "$INSTDIR"

  ; Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\prober"
  CreateShortcut "$SMPROGRAMS\prober\prober GUI.lnk" "$INSTDIR\prober_gui.exe"
  CreateShortcut "$SMPROGRAMS\prober\Uninstall prober.lnk" "$INSTDIR\uninstall.exe"

  ; Desktop shortcut
  CreateShortcut "$DESKTOP\prober.lnk" "$INSTDIR\prober_gui.exe"

  ; Add to PATH
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  StrCpy $1 "$INSTDIR"
  Push $0
  Push $1
  Call AddToPath

  ; Install VC++ Redistributable if needed
  Call InstallVCRedist
SectionEnd

Section "Uninstall"
  ; Remove files
  RMDir /r "$INSTDIR\tools"
  Delete "$INSTDIR\prober.exe"
  Delete "$INSTDIR\prober_gui.exe"
  Delete "$INSTDIR\vc_redist.x64.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\prober\prober GUI.lnk"
  Delete "$SMPROGRAMS\prober\Uninstall prober.lnk"
  RMDir "$SMPROGRAMS\prober"
  Delete "$DESKTOP\prober.lnk"

  ; Remove from PATH
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  StrCpy $1 "$INSTDIR"
  Push $0
  Push $1
  Call un.RemoveFromPath

  ; Remove registry
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\prober"
  DeleteRegKey HKLM "Software\prober"
SectionEnd

; --- Helper: Add directory to system PATH ---
Function AddToPath
  Pop $1 ; dir to add
  Pop $0 ; current PATH

  ; Check if already in PATH
  Push $0
  Push $1
  Call StrContains
  Pop $2
  StrCmp $2 "" 0 already_in_path

  StrLen $3 $0
  IntCmp $3 0 set_path
  StrCpy $0 "$0;$1"

  set_path:
  StrCmp $0 "" 0 +2
    StrCpy $0 "$1"
  WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0"
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

  already_in_path:
FunctionEnd

; --- Helper: Remove directory from system PATH (uninstall) ---
Function un.RemoveFromPath
  Pop $1 ; dir to remove
  Pop $0 ; current PATH

  StrLen $2 $1
  StrLen $3 $0
  StrCpy $4 "" ; result

  loop:
    StrLen $5 $0
    IntCmp $5 0 done
    ; Find next semicolon
    StrCpy $6 $0 1
    StrCpy $7 ""
    find_semi:
      StrCpy $6 $0 1
      StrCmp $6 "" got_token
      StrCmp $6 ";" got_token
      StrCpy $7 "$7$6"
      StrCpy $0 $0 "" 1
      Goto find_semi
    got_token:
      StrCpy $0 $0 "" 1 ; skip semicolon
      StrCmp $7 $1 loop ; skip if matches dir to remove
      StrLen $8 $4
      IntCmp $8 0 0 +2 +2
        StrCpy $4 "$4;"
      StrCpy $4 "$4$7"
      Goto loop

  done:
  WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$4"
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
FunctionEnd

; --- Helper: Check if string contains substring ---
Function StrContains
  Pop $1 ; needle
  Pop $0 ; haystack
  StrLen $2 $1
  StrLen $3 $0
  ${If} $3 < $2
    Push ""
    Return
  ${EndIf}
  IntOp $4 $3 - $2
  StrCpy $5 0
  check_loop:
    IntCmp $5 $4 0 0 not_found
    StrCpy $6 $0 $2 $5
    StrCmp $6 $1 found
    IntOp $5 $5 + 1
    Goto check_loop
  found:
    Push "1"
    Return
  not_found:
    Push ""
FunctionEnd

; --- Install VC++ Redistributable if not present ---
Function InstallVCRedist
  ; Check if VC++ 2015-2022 x64 runtime is installed
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
  IntCmp $0 1 vcredist_done

  ; Try bundled installer
  IfFileExists "$INSTDIR\vc_redist.x64.exe" 0 vcredist_done
  DetailPrint "Installing Visual C++ Redistributable..."
  nsExec::ExecToLog '"$INSTDIR\vc_redist.x64.exe" /install /quiet /norestart'
  Pop $0
  DetailPrint "VC++ Redistributable installer exited with code: $0"

  vcredist_done:
FunctionEnd
