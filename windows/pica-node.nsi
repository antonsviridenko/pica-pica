
Name "Pica Pica Node"
OutFile "pica-node_setup.exe"

Page license
Page directory
Page instfiles

LicenseData "COPYING"

InstallDir "$PROGRAMFILES\Pica Pica Node"

Icon "picapica-icon-sit.ico"

RequestExecutionLevel admin

; The stuff to install
Section "" ;No components page, name is not important

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  
  ; Put file there
  File "pica-node.exe"
  File "libeay32.dll"
  File "libevent-2-1-6.dll"
  File "libgcc_s_sjlj-1.dll"
  File "libsqlite3-0.dll"
  File "miniupnpc.dll"
  File "ssleay32.dll"
  File "pica-node.conf"
  File "nodelist.db"
  File "COPYING"
  File "README"
  File "OPENSSL.LICENSE.txt"
  File "libevent.LICENSE.txt"
  File "openssl.cnf"
  SetOutPath "$INSTDIR\share"
  File  "share\dhparam4096.pem"
  
  WriteUninstaller "uninstall.exe"
  
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node" "DisplayName" "Pica Pica Node"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node" "NoRepair" 1
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node" "HelpLink" "https://picapica.im"

  Exec 'sc create PicaNodeService binPath= $\"$INSTDIR\pica-node.exe$\" start= auto obj= $\"NT AUTHORITY\LocalService$\"'
  Exec 'sc start PicaNodeService'
SectionEnd ; end the section


Section "Uninstall"
  Exec 'sc stop PicaNodeService'
  Delete $INSTDIR\uninstall.exe 
  Delete "$INSTDIR\pica-node.exe"
  Delete "$INSTDIR\libeay32.dll"
  Delete "$INSTDIR\libevent-2-1-6.dll"
  Delete "$INSTDIR\libgcc_s_sjlj-1.dll"
  Delete "$INSTDIR\libsqlite3-0.dll"
  Delete "$INSTDIR\miniupnpc.dll"
  Delete "$INSTDIR\ssleay32.dll"
  Delete "$INSTDIR\pica-node.conf"
  Delete "$INSTDIR\nodelist.db"
  Delete "$INSTDIR\COPYING"
  Delete "$INSTDIR\README"
  Delete "$INSTDIR\OPENSSL.LICENSE.txt"
  Delete "$INSTDIR\libevent.LICENSE.txt"
  Delete "$INSTDIR\openssl.cnf"
  Delete "$INSTDIR\share\dhparam4096.pem"
  RMDir $INSTDIR\share
  RMDir $INSTDIR
  
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Pica Pica Node"
  Exec 'sc delete PicaNodeService' 
SectionEnd
