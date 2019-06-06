
Name "Pica Pica Messenger"
OutFile "pica-client_setup.exe"

Page license
Page directory
Page instfiles

LicenseData "COPYING"

InstallDir "$PROGRAMFILES\Pica Pica Messenger"

Icon "picapica.ico"

RequestExecutionLevel admin

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File "pica-client.exe"
  File "openssl.exe"
  File "picapica.ico"
  File "COPYING"
  File "OPENSSL.LICENSE.txt"
  File "README"
  File "openssl.cnf"
  File "libeay32.dll"
  File "ssleay32.dll"
  File "libgcc_s_sjlj-1.dll"
  File "libstdc++-6.dll"
  File "miniupnpc.dll"
  File "Qt5Core.dll"
  File "Qt5Gui.dll"
  File "Qt5Network.dll"
  File "Qt5Sql.dll"
  File "Qt5Widgets.dll"
  SetOutPath "$INSTDIR\share"
  File  "share\picapica-icon-fly.png"
  File  "share\picapica-icon-sit.png"
  File  "share\picapica-snd-newmessage.wav"
  File  "share\dhparam4096.pem"
  SetOutPath "$INSTDIR\sqldrivers"
  File  "sqldrivers\qsqlite.dll"
  SetOutPath "$INSTDIR\platforms"
  File "platforms\qwindows.dll"

  
  WriteUninstaller "uninstall.exe"
  
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger" "DisplayName" "Pica Pica Messenger"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger" "NoRepair" 1
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger" "HelpLink" "http://picapica.im"
  
SectionEnd ; end the section

Section "Start Menu Shortcuts"

  CreateDirectory "$SMPROGRAMS\Pica Pica Messenger"
  SetOutPath $INSTDIR
  CreateShortCut "$SMPROGRAMS\Pica Pica Messenger\Pica Pica Messenger.lnk" "$INSTDIR\pica-client.exe" "" "$INSTDIR\picapica.ico" 
  
SectionEnd

Section "Uninstall"
  Delete $INSTDIR\uninstall.exe 
  Delete "$INSTDIR\pica-client.exe"
  Delete "$INSTDIR\openssl.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\picapica.ico"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\README"
  Delete "$INSTDIR\OPENSSL.LICENSE.txt"
  Delete "$INSTDIR\openssl.cnf"
  Delete "$INSTDIR\share\picapica-icon-fly.png"
  Delete "$INSTDIR\share\picapica-icon-sit.png"
  Delete "$INSTDIR\share\picapica-snd-newmessage.wav"
  Delete "$INSTDIR\share\dhparam4096.pem"
  Delete "$INSTDIR\platforms\*.dll"
  Delete "$INSTDIR\sqldrivers\*.dll"
  RMDir $INSTDIR\share
  RMDir $INSTDIR\sqldrivers
  RMDir $INSTDIR\platforms
  RMDir $INSTDIR
  
  Delete "$SMPROGRAMS\Pica Pica Messenger\*.*"
  RMDir "$SMPROGRAMS\Pica Pica Messenger"
  
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger"
  
SectionEnd
