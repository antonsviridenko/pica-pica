bin_PROGRAMS = 
CLEANFILES =


EXTRA_DIST = 	iniparser/LICENSE \
			COPYING \
			README \
			debian \
			gentoo \
			windows \
			apple \
			pica-client/icons

#####################################
if PICA_NODE

bin_PROGRAMS += pica-node

pica_node_SOURCES = PICA_node.c PICA_msgproc.c PICA_nodeaddrlist.c \
		PICA_nodewait.c PICA_nodeconfig.c PICA_log.c \
		iniparser/src/dictionary.c iniparser/src/iniparser.c \
		PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h \
		PICA_nodewait.h PICA_nodeconfig.h PICA_log.h \
		iniparser/src/dictionary.h iniparser/src/iniparser.h \
		PICA_common.h PICA_proto.h PICA_id.h PICA_id.c \
		PICA_netconf.c PICA_netconf.h PICA_security.h \
		PICA_signverify.c PICA_signverify.h
		
dist_man_MANS = pica-node.8 pica-node.conf.5

pica_node_CPPFLAGS = -DPICA_INSTALLPREFIX=\"$(prefix)\" -DPICA_SYSCONFDIR=\"$(sysconfdir)\" -DPICA_LOCALSTATEDIR=\"$(localstatedir)\"
pica_node_LDADD = $(PICA_NODE_LIBS)
pkglocalstatedir = $(localstatedir)/pica-node

dist_pkglocalstate_DATA = nodelist.db
dist_sysconf_DATA = pica-node.conf 

nodelist.db: nodelist_db_create.sql
	echo "Creating and initializing nodelist database..."
	sqlite3 $@  "`cat $<`" 

CLEANFILES += nodelist.db

EXTRA_DIST += nodelist_db_create.sql 

pkgpicanodedir = ${datarootdir}/pica-node

dist_pkgpicanode_DATA = dhparam4096.pem README nodelist_db_create.sql

endif
#####################################
if PICA_CLIENT
bin_PROGRAMS += pica-client/pica-client

pica_client_pica_client_SOURCES = \
pica-client/accounts.cpp \
pica-client/accounts.h \
pica-client/accountswindow.cpp \
pica-client/accountswindow.h \
pica-client/askpassword.cpp \
pica-client/askpassword.h \
pica-client/chatwindow.cpp \
pica-client/chatwindow.h \
pica-client/callwindow.cpp \
pica-client/callwindow.h \
pica-client/audiovideocallcontroller.cpp \
pica-client/audiovideocallcontroller.h \
pica-client/contactlistwidget.cpp \
pica-client/contactlistwidget.h \
pica-client/contacts.cpp \
pica-client/contacts.h \
pica-client/globals.h \
pica-client/main.cpp \
pica-client/mainwindow.cpp \
pica-client/mainwindow.h \
pica-client/mainwindow.ui \
pica-client/msguirouter.cpp \
pica-client/msguirouter.h \
pica-client/nodes.cpp \
pica-client/nodes.h \
pica-client/openssltool.cpp \
pica-client/openssltool.h \
pica-client/picaactioncenter.cpp \
pica-client/picaactioncenter.h \
pica-client/pica-client.pro \
pica-client/picasystray.cpp \
pica-client/picasystray.h \
pica-client/skynet.cpp \
pica-client/skynet.h \
pica-client/history.h \
pica-client/history.cpp \
pica-client/dhparam.h \
pica-client/dhparam.cpp \
pica-client/filetransfercontroller.cpp \
pica-client/filetransfercontroller.h \
pica-client/sound.cpp \
pica-client/sound.h \
pica-client/toneplayer/toneplayer.cpp \
pica-client/toneplayer/toneplayer.h \
pica-client/settings.cpp \
pica-client/settings.h \
pica-client/mediadevice.cpp \
pica-client/mediadevice.h \
pica-client/audiodevice.cpp \
pica-client/audiodevice.h \
pica-client/audioring.h \
pica-client/echocanceller.cpp \
pica-client/echocanceller.h \
pica-client/nativeaudio.cpp \
pica-client/nativeaudio.h \
pica-client/nativeaudio_wasapi.cpp \
pica-client/nativeaudio_coreaudio.cpp \
pica-client/capturegain.cpp \
pica-client/capturegain.h \
pica-client/capturegain_wasapi.cpp \
pica-client/capturegain_coreaudio.cpp \
pica-client/videodevice.cpp \
pica-client/videodevice.h \
pica-client/videodshow.cpp \
 \
pica-client/dialogs/addaccountdialog.cpp \
pica-client/dialogs/addaccountdialog.h \
pica-client/dialogs/forgedcertdialog.cpp \
pica-client/dialogs/forgedcertdialog.h \
pica-client/dialogs/registeraccountdialog.cpp \
pica-client/dialogs/registeraccountdialog.h \
pica-client/dialogs/viewcertdialog.cpp \
pica-client/dialogs/viewcertdialog.h \
pica-client/dialogs/showpicaiddialog.cpp \
pica-client/dialogs/showpicaiddialog.h \
pica-client/dialogs/filetransferdialog.cpp \
pica-client/dialogs/filetransferdialog.h \
pica-client/dialogs/settingsdialog.cpp \
pica-client/dialogs/settingsdialog.h \
pica-client/dialogs/nodesdialog.cpp \
pica-client/dialogs/nodesdialog.h \
PICA_client.c \
PICA_media.c \
PICA_client.h \
PICA_media.h \
PICA_msgproc.c \
PICA_msgproc.h \
PICA_common.h PICA_proto.h \
PICA_id.c PICA_id.h \
PICA_netconf.c PICA_netconf.h \
PICA_signverify.c PICA_signverify.h \
PICA_security.h

nodist_pica_client_pica_client_SOURCES  = \
moc_accountswindow.cpp \
moc_addaccountdialog.cpp \
moc_askpassword.cpp \
moc_chatwindow.cpp \
moc_callwindow.cpp \
moc_contactlistwidget.cpp \
moc_forgedcertdialog.cpp \
moc_mainwindow.cpp \
moc_msguirouter.cpp \
moc_picaactioncenter.cpp \
moc_picasystray.cpp \
moc_registeraccountdialog.cpp \
moc_filetransferdialog.cpp \
moc_audiovideocallcontroller.cpp \
moc_skynet.cpp \
moc_settingsdialog.cpp \
moc_viewcertdialog.cpp \
moc_showpicaiddialog.cpp \
moc_dhparam.cpp \
moc_filetransfercontroller.cpp \
moc_nodesdialog.cpp \
moc_toneplayer.cpp \
moc_audiodevice.cpp \
moc_videodevice.cpp \
ui_mainwindow.h

# moc runs its own preprocessor and sees none of the compiler's defines, so
# any define that a header's signals or slots are conditional on has to be
# repeated here - otherwise moc skips those declarations and the signals end
# up declared but never generated, failing at link time.
MOCFLAGS =

moc_%.cpp: pica-client/%.h
	$(MOC) $(MOCFLAGS) $< -o $@

moc_%.cpp: pica-client/dialogs/%.h
	$(MOC) $(MOCFLAGS) $< -o $@

moc_%.cpp: pica-client/toneplayer/%.h
	$(MOC) $(MOCFLAGS) $< -o $@

pica-client/mainwindow.cpp: ui_mainwindow.h

#ui_mainwindow.h: pica-client/mainwindow.ui
#	uic $(CPPFLAGS) $< -o $@

ui_%.h: pica-client/%.ui
	$(UIC)  $< -o $@

 
pica_client_pica_client_CPPFLAGS = -DPICA_INSTALLPREFIX=\"$(prefix)\" $(QT_DEPENDENCIES_CFLAGS) $(FFMPEG_CFLAGS) $(PLATFORM_DEPS_CFLAGS) $(SPEEXDSP_CFLAGS)
pica_client_pica_client_LDADD = $(QT_DEPENDENCIES_LIBS) $(FFMPEG_LIBS) $(PLATFORM_DEPS_LIBS) $(SPEEXDSP_LIBS)

# Listing PulseAudio sources and sinks in the settings dialog. The two
# native backend sources above are compiled unconditionally and are empty
# preprocessor-wise on the platforms they are not for, but libpulse is a
# real dependency and only linked when configure found it.
if PICA_PULSE
pica_client_pica_client_CPPFLAGS += $(PULSE_CFLAGS)
pica_client_pica_client_LDADD += $(PULSE_LIBS)
MOCFLAGS += -DHAVE_LIBPULSE
endif

# VAAPI accelerated encoding, decoding and rendering. Everything below is left
# out of the build entirely when configure did not find libva/EGL or was told
# not to look - the code itself is additionally guarded by #ifdef HAVE_VAAPI.
if PICA_VAAPI
pica_client_pica_client_SOURCES += \
pica-client/vaapi.cpp \
pica-client/vaapi.h \
pica-client/vaapivideowidget.cpp \
pica-client/vaapivideowidget.h

nodist_pica_client_pica_client_SOURCES += moc_vaapivideowidget.cpp

pica_client_pica_client_CPPFLAGS += $(VAAPI_CFLAGS)
pica_client_pica_client_LDADD += $(VAAPI_LIBS) -lGL
MOCFLAGS += -DHAVE_VAAPI
endif

# Drawing a decoded surface in an X11 session, where Qt's OpenGL contexts come
# from GLX and cannot import DMA-BUF handles: the driver draws the surface
# straight onto the widget's X window instead.
if PICA_VAAPI_X11
pica_client_pica_client_SOURCES += \
pica-client/vaapix11widget.cpp \
pica-client/vaapix11widget.h

nodist_pica_client_pica_client_SOURCES += moc_vaapix11widget.cpp

pica_client_pica_client_CPPFLAGS += $(VAAPI_X11_CFLAGS)
pica_client_pica_client_LDADD += $(VAAPI_X11_LIBS)
MOCFLAGS += -DHAVE_VAAPI_X11
endif

pkgpicaclientdir = ${datarootdir}/pica-client
dist_pkgpicaclient_DATA =  \
dhparam4096.pem \
pica-client/picapica-icon-fly.png \
pica-client/picapica-icon-sit.png \
pica-client/picapica-snd-newmessage.wav

#$(filter moc_%.cpp,$(pica_client_SOURCES))
CLEANFILES += moc_*.cpp ui_*.h

EXTRA_DIST += pica-client/pica-client.desktop 


deploy: pica-client/pica-client
	mkdir -p PicaPicaMessenger.app/Contents/Resources
	mkdir -p PicaPicaMessenger.app/Contents/MacOS
	mkdir -p PicaPicaMessenger.app/Contents/PlugIns/sqldrivers
	cp apple/Info.plist PicaPicaMessenger.app/Contents/
	cp apple/picapica.icns PicaPicaMessenger.app/Contents/Resources
	cp pica-client/pica-client PicaPicaMessenger.app/Contents/MacOS/PicaPicaMessenger
	cp pica-client/picapica-icon-fly.png PicaPicaMessenger.app/Contents/Resources
	cp pica-client/picapica-icon-sit.png PicaPicaMessenger.app/Contents/Resources
	cp pica-client/picapica-snd-newmessage.wav PicaPicaMessenger.app/Contents/Resources
	cp dhparam4096.pem PicaPicaMessenger.app/Contents/Resources
	cp ${QTDIR}/plugins/sqldrivers/libqsqlite.dylib PicaPicaMessenger.app/Contents/PlugIns/sqldrivers
	macdeployqt PicaPicaMessenger.app -verbose=3
	macdeployqt PicaPicaMessenger.app -verbose=3
	hdiutil create -srcfolder PicaPicaMessenger.app PicaPicaMessenger.dmg
	hdiutil internet-enable -yes PicaPicaMessenger.dmg

endif
#####################################
