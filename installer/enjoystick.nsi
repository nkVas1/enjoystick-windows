; ---------------------------------------------------------------------------
; EnjoyStick Windows — NSIS Installer Script
; Requires: NSIS 3.x (https://nsis.sourceforge.io)
; Build:    makensis installer\enjoystick.nsi
; ---------------------------------------------------------------------------

!define PRODUCT_NAME      "EnjoyStick"
!define PRODUCT_VERSION   "0.1.0"
!define PRODUCT_PUBLISHER "EnjoyStick Project"
!define PRODUCT_URL       "https://github.com/nkVas1/enjoystick-windows"
!define PRODUCT_EXE       "EnjoyStick.exe"
!define PRODUCT_UNINST    "Uninstall EnjoyStick.exe"

!define REGKEY_UNINST  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define REGKEY_RUN     "Software\Microsoft\Windows\CurrentVersion\Run"

; ---------------------------------------------------------------------------
; Includes
; ---------------------------------------------------------------------------

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

; ---------------------------------------------------------------------------
; General
; ---------------------------------------------------------------------------

Name            "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile         "EnjoyStick-Setup-${PRODUCT_VERSION}.exe"
InstallDir      "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKCU "${REGKEY_UNINST}" "InstallLocation"
RequestExecutionLevel admin
SetCompress    auto
SetCompressor  /SOLID lzma

; ---------------------------------------------------------------------------
; MUI Settings
; ---------------------------------------------------------------------------

!define MUI_ABORTWARNING
!define MUI_ICON   "..\resources\app.ico"
!define MUI_UNICON "..\resources\app.ico"

; Welcome page
!define MUI_WELCOMEFINISHPAGE_BITMAP "..\resources\installer-side.bmp"
!define MUI_WELCOMEPAGE_TITLE        "Welcome to EnjoyStick Setup"
!define MUI_WELCOMEPAGE_TEXT         "This wizard will install EnjoyStick ${PRODUCT_VERSION} on your computer.$\r$\n$\r$\nEnjoyStick lets you use your Xbox or PlayStation controller to navigate Windows — cursor, keyboard, radial quick-action menu and more.$\r$\n$\r$\nClick Next to continue."

; Finish page
!define MUI_FINISHPAGE_RUN            "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT       "Launch EnjoyStick now"
!define MUI_FINISHPAGE_SHOWREADME     ""
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

; ---------------------------------------------------------------------------
; Installer Sections
; ---------------------------------------------------------------------------

Section "EnjoyStick" SecMain
    SectionIn RO ; Required section

    ; Terminate running instance before overwriting
    ExecWait 'taskkill /F /IM "${PRODUCT_EXE}" /T' $0

    SetOutPath "$INSTDIR"

    ; Main executable (built by CMake — adjust path if needed)
    File "..\build\Release\Release\${PRODUCT_EXE}"

    ; Resources (icons etc.)
    File /nonfatal /r "..\resources\*.*"

    ; Visual C++ runtime (statically linked, so this may be empty)
    ; File /nonfatal "vcruntime140.dll"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\${PRODUCT_UNINST}"

    ; Add/Remove Programs entry
    WriteRegStr   HKCU "${REGKEY_UNINST}" "DisplayName"      "${PRODUCT_NAME}"
    WriteRegStr   HKCU "${REGKEY_UNINST}" "DisplayVersion"   "${PRODUCT_VERSION}"
    WriteRegStr   HKCU "${REGKEY_UNINST}" "Publisher"        "${PRODUCT_PUBLISHER}"
    WriteRegStr   HKCU "${REGKEY_UNINST}" "URLInfoAbout"     "${PRODUCT_URL}"
    WriteRegStr   HKCU "${REGKEY_UNINST}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKCU "${REGKEY_UNINST}" "UninstallString"  "$INSTDIR\${PRODUCT_UNINST}"
    WriteRegDWORD HKCU "${REGKEY_UNINST}" "NoModify"         1
    WriteRegDWORD HKCU "${REGKEY_UNINST}" "NoRepair"         1

    ; Start Menu shortcut
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
                    "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk" \
                    "$INSTDIR\${PRODUCT_UNINST}"

    ; Desktop shortcut (user can remove it; installer creates it once)
    CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" \
                   "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0

    ; Auto-start on login (same key the app itself manages via AutoStart.cpp)
    WriteRegStr HKCU "${REGKEY_RUN}" "${PRODUCT_NAME}" "\"$INSTDIR\${PRODUCT_EXE}\""

SectionEnd

; ---------------------------------------------------------------------------
; Uninstaller Section
; ---------------------------------------------------------------------------

Section "Uninstall"

    ; Terminate running instance
    ExecWait 'taskkill /F /IM "${PRODUCT_EXE}" /T' $0

    ; Remove files
    Delete "$INSTDIR\${PRODUCT_EXE}"
    Delete "$INSTDIR\${PRODUCT_UNINST}"
    RMDir  /r "$INSTDIR\resources"
    RMDir  "$INSTDIR" ; only if empty

    ; Remove shortcuts
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\*.lnk"
    RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

    ; Remove registry keys
    DeleteRegKey HKCU "${REGKEY_UNINST}"
    DeleteRegValue HKCU "${REGKEY_RUN}" "${PRODUCT_NAME}"

    ; NOTE: %APPDATA%\Enjoystick (user config) is intentionally preserved.
    ; The user can delete it manually if desired.

SectionEnd
