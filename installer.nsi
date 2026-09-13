!define APPNAME "TaskbarEngine"
!define APPVERSION "1.0.0"
!define APPDESC "TaskbarEngine Customization Tool"

Name "${APPNAME} ${APPVERSION}"
OutFile "TaskbarEngine_Release.exe"
InstallDir "$PROGRAMFILES\${APPNAME}"
RequestExecutionLevel admin

Page directory
Page instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  
  ; Main app files
  File "ReleaseStaging\*.exe"
  File "ReleaseStaging\*.dll"
  
  ; Config
  SetOutPath "$INSTDIR\Config"
  File "ReleaseStaging\Config\*.*"
  
  ; Core
  SetOutPath "$INSTDIR\Core"
  File "ReleaseStaging\Core\*.*"
  
  ; Modules
  SetOutPath "$INSTDIR\Modules\dummy"
  File /nonfatal "ReleaseStaging\Modules\dummy\*.*"
  
  SetOutPath "$INSTDIR\Modules\icon_hover"
  File /nonfatal "ReleaseStaging\Modules\icon_hover\*.*"
  
  SetOutPath "$INSTDIR\Modules\TaskbarPayload"
  File /nonfatal "ReleaseStaging\Modules\TaskbarPayload\*.*"
  
  SetOutPath "$INSTDIR\Modules\taskbar_resize"
  File /nonfatal "ReleaseStaging\Modules\taskbar_resize\*.*"
  
  SetOutPath "$INSTDIR\Modules\taskbar_transparency"
  File /nonfatal "ReleaseStaging\Modules\taskbar_transparency\*.*"
  
  ; Create shortcut
  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\TaskbarEngineHost.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\TaskbarEngineHost.exe"
  
  ; Write uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
