bin_PROGRAMS = 
CLEANFILES =
#dist_pkgdata_DATA = CA.pem


EXTRA_DIST = 	iniparser/LICENSE \
			LICENSE \
			README

#####################################
if PICA_NODE

bin_PROGRAMS += pica-node

pica_node_SOURCES = PICA_node.c PICA_msgproc.c PICA_nodeaddrlist.c \
		PICA_nodejoinskynet.c PICA_nodeconfig.c PICA_log.c \
		iniparser/src/dictionary.c iniparser/src/iniparser.c \
		PICA_node.h PICA_msgproc.h PICA_nodeaddrlist.h \
		PICA_nodejoinskynet.h PICA_nodeconfig.h PICA_log.h \
		iniparser/src/dictionary.h iniparser/src/iniparser.h \
		PICA_common.h PICA_proto.h
		
dist_man_MANS = pica-node.8 pica-node.conf.5

pica_node_CPPFLAGS = -DPICA_INSTALLPREFIX=\"$(prefix)\"

pkglocalstatedir = $(localstatedir)/pica-node

dist_pkglocalstate_DATA = nodelist.db 

nodelist.db: nodelist_db_create.sql
	echo "Creating and initializing nodelist database..."
	sqlite3 $@  "`cat $<`" 

EXTRA_DIST += nodelist_db_create.sql 

pkgpicanodedir = ${datarootdir}/pica-node

dist_pkgpicanode_DATA = CA.pem README nodelist_db_create.sql

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
pica-client/nodethread.cpp \
pica-client/nodethread.h \
pica-client/openssltool.cpp \
pica-client/openssltool.h \
pica-client/picaactioncenter.cpp \
pica-client/picaactioncenter.h \
pica-client/pica-client.pro \
pica-client/picasystray.cpp \
pica-client/picasystray.h \
pica-client/skynet.cpp \
pica-client/skynet.h \
 \
pica-client/dialogs/addaccountdialog.cpp \
pica-client/dialogs/addaccountdialog.h \
pica-client/dialogs/forgedcertdialog.cpp \
pica-client/dialogs/forgedcertdialog.h \
pica-client/dialogs/registeraccountdialog.cpp \
pica-client/dialogs/registeraccountdialog.h \
pica-client/dialogs/viewcertdialog.cpp \
pica-client/dialogs/viewcertdialog.h \
PICA_client.c \
PICA_client.h \
PICA_msgproc.c \
PICA_msgproc.h \
PICA_common.h PICA_proto.h

nodist_pica_client_pica_client_SOURCES  = \
moc_accountswindow.cpp \
moc_addaccountdialog.cpp \
moc_askpassword.cpp \
moc_chatwindow.cpp \
moc_contactlistwidget.cpp \
moc_forgedcertdialog.cpp \
moc_mainwindow.cpp \
moc_msguirouter.cpp \
moc_nodethread.cpp \
moc_picaactioncenter.cpp \
moc_picasystray.cpp \
moc_registeraccountdialog.cpp \
moc_skynet.cpp \
moc_viewcertdialog.cpp \
ui_mainwindow.h

moc_%.cpp: pica-client/%.h
	moc $< -o $@

moc_%.cpp: pica-client/dialogs/%.h
	moc $< -o $@

pica-client/mainwindow.cpp: ui_mainwindow.h

#ui_mainwindow.h: pica-client/mainwindow.ui
#	uic $(CPPFLAGS) $< -o $@

ui_%.h: pica-client/%.ui
	uic $(CPPFLAGS) $< -o $@

 
pica_client_pica_client_CPPFLAGS = -DPICA_INSTALLPREFIX=\"$(prefix)\" $(QT_DEPENDENCIES_CFLAGS) 
pica_client_pica_client_LDADD = $(QT_DEPENDENCIES_LIBS)

pkgpicaclientdir = ${datarootdir}/pica-client
dist_pkgpicaclient_DATA = CA.pem pica-client/picapica-icon-fly.png pica-client/picapica-icon-sit.png 

#$(filter moc_%.cpp,$(pica_client_SOURCES))
CLEANFILES += moc_*.cpp ui_*.h

endif
#####################################
