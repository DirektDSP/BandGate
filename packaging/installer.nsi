; DirektDSP BandGate NSIS Installer
; VERSION is passed via /DVERSION=x.x.x on the makensis command line
; ARTIFACTS_DIR is passed via /DARTIFACTS_DIR=path

!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.0.1"
!endif

!ifndef ARTIFACTS_DIR
  !define ARTIFACTS_DIR "..\artifacts\windows"
!endif

Name "DirektDSP BandGate ${VERSION}"
!ifndef OUTDIR
  !define OUTDIR "..\artifacts"
!endif
OutFile "${OUTDIR}\DirektDSP-BandGate-${VERSION}-Windows.exe"
InstallDir "$PROGRAMFILES64\DirektDSP\BandGate"
RequestExecutionLevel admin
Unicode True
BrandingText "DirektDSP BandGate ${VERSION}"

;----------------------------------------------------------------------
; MUI2 Appearance
;----------------------------------------------------------------------

; Icons
!define MUI_ICON "icon.ico"
!define MUI_UNICON "icon.ico"

; Header image (top banner on most pages)
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "header.bmp"
!define MUI_HEADERIMAGE_RIGHT

; Welcome / Finish page sidebar image
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"

; Abort warning
!define MUI_ABORTWARNING
!define MUI_ABORTWARNING_TEXT "Are you sure you want to cancel the DirektDSP BandGate installation?"

;----------------------------------------------------------------------
; Welcome Page
;----------------------------------------------------------------------

!define MUI_WELCOMEPAGE_TITLE "Welcome to DirektDSP BandGate ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "This will install the DirektDSP BandGate spectral gate plugin on your computer.$\r$\n$\r$\nFormats included: VST3, CLAP, Standalone$\r$\n$\r$\nClick Next to continue."

;----------------------------------------------------------------------
; Finish Page
;----------------------------------------------------------------------

!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "DirektDSP BandGate ${VERSION} has been installed successfully.$\r$\n$\r$\nThank you for choosing DirektDSP! Load up your DAW and look for BandGate under the DirektDSP manufacturer.$\r$\n$\r$\nFor documentation, presets, and updates, visit our website."
!define MUI_FINISHPAGE_LINK "Visit direktdsp.com"
!define MUI_FINISHPAGE_LINK_LOCATION "https://direktdsp.com"
!define MUI_FINISHPAGE_NOREBOOTSUPPORT

;----------------------------------------------------------------------
; Pages
;----------------------------------------------------------------------

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;----------------------------------------------------------------------
; VST3 Plugin
;----------------------------------------------------------------------

Section "BandGate VST3" SecVST3
  SetOutPath "$COMMONFILES64\VST3\DirektDSP"
  File /nonfatal /r "${ARTIFACTS_DIR}\BandGate.vst3"
SectionEnd

;----------------------------------------------------------------------
; CLAP Plugin
;----------------------------------------------------------------------

Section "BandGate CLAP" SecCLAP
  SetOutPath "$COMMONFILES64\CLAP\DirektDSP"
  File /nonfatal "${ARTIFACTS_DIR}\BandGate.clap"
SectionEnd

;----------------------------------------------------------------------
; Standalone Application
;----------------------------------------------------------------------

Section "BandGate Standalone" SecStandalone
  SetOutPath "$INSTDIR"
  File /nonfatal "${ARTIFACTS_DIR}\BandGate.exe"
SectionEnd

;----------------------------------------------------------------------
; Uninstaller
;----------------------------------------------------------------------

Section "-Uninstaller"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall-BandGate.exe"

  ; Add/Remove Programs registry
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate" \
    "DisplayName" "DirektDSP BandGate ${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate" \
    "UninstallString" "$\"$INSTDIR\Uninstall-BandGate.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate" \
    "Publisher" "DirektDSP"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate" \
    "DisplayIcon" "$INSTDIR\Uninstall-BandGate.exe"
SectionEnd

;----------------------------------------------------------------------
; Component descriptions
;----------------------------------------------------------------------

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} "BandGate VST3 plugin — installed to the system VST3 folder."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCLAP} "BandGate CLAP plugin — installed to the system CLAP folder."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStandalone} "BandGate standalone application."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;----------------------------------------------------------------------
; Uninstall section
;----------------------------------------------------------------------

Section "Uninstall"
  ; VST3
  RMDir /r "$COMMONFILES64\VST3\DirektDSP\BandGate.vst3"
  RMDir "$COMMONFILES64\VST3\DirektDSP"

  ; CLAP
  Delete "$COMMONFILES64\CLAP\DirektDSP\BandGate.clap"
  RMDir "$COMMONFILES64\CLAP\DirektDSP"

  ; Standalone + uninstaller
  Delete "$INSTDIR\BandGate.exe"
  Delete "$INSTDIR\Uninstall-BandGate.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\DirektDSP"

  ; Registry
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\DirektDSP-BandGate"
SectionEnd
