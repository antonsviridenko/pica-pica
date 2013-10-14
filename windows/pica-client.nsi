
Name "Pica Pica Messenger"
OutFile "pica-client_setup.exe"

Page license
Page directory
Page instfiles

LicenseData "LICENSE"

InstallDir "$PROGRAMFILES\Pica Pica Messenger"

Icon "picapica-icon-sit.ico"

RequestExecutionLevel admin

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File "pica-client.exe"
  File "openssl.exe"
  File "QtCore4.dll"
  File "QtGui4.dll"
  File "QtNetwork4.dll"
  File "QtSql4.dll"
  File "picapica-icon-sit.ico"
  File "LICENSE"
  File "OPENSSL.LICENSE"
  File "Qt4.LICENSE"
  File "openssl.cnf"
  SetOutPath "$INSTDIR\share"
  File  "share\CA.pem"
  File  "share\picapica-icon-fly.png"
  File  "share\picapica-icon-sit.png"
  File  "share\dhparam4096.pem"
  SetOutPath "$INSTDIR\sqldrivers"
  File  "sqldrivers\qsqlite4.dll"
  
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
  CreateShortCut "$SMPROGRAMS\Pica Pica Messenger\Pica Pica Messenger.lnk" "$INSTDIR\pica-client.exe" "" "$INSTDIR\picapica-icon-sit.ico" 
  
SectionEnd

Section "Uninstall"
  Delete $INSTDIR\uninstall.exe 
  Delete $INSTDIR\pica-client.exe
  Delete "$INSTDIR\pica-client.exe"
  Delete "$INSTDIR\openssl.exe"
  Delete "$INSTDIR\QtCore4.dll"
  Delete "$INSTDIR\QtGui4.dll"
  Delete "$INSTDIR\QtNetwork4.dll"
  Delete "$INSTDIR\QtSql4.dll"
  Delete "$INSTDIR\picapica-icon-sit.ico"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\OPENSSL.LICENSE"
  Delete "$INSTDIR\Qt4.LICENSE"
  Delete "$INSTDIR\openssl.cnf"
  Delete "$INSTDIR\share\CA.pem"
  Delete "$INSTDIR\share\picapica-icon-fly.png"
  Delete "$INSTDIR\share\picapica-icon-sit.png"
  Delete "$INSTDIR\share\dhparam4096.pem"
  Delete "$INSTDIR\sqldrivers\qsqlite4.dll"
  RMDir $INSTDIR\share
  RMDir $INSTDIR\sqldrivers
  RMDir $INSTDIR
  
  Delete "$SMPROGRAMS\Pica Pica Messenger\*.*"
  RMDir "$SMPROGRAMS\Pica Pica Messenger"
  
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Messenger"
  
SectionEnd
