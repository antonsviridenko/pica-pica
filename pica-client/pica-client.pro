#-------------------------------------------------
#
# Project created by QtCreator 2011-10-22T02:19:02
#
#-------------------------------------------------

QT       += core gui sql network

TARGET = pica-client
TEMPLATE = app


SOURCES += main.cpp\
    audiodevice.cpp \
    echocanceller.cpp \
    nativeaudio.cpp \
    nativeaudio_wasapi.cpp \
    nativeaudio_coreaudio.cpp \
	audiovideocallcontroller.cpp \
        mainwindow.cpp \
    chatwindow.cpp \
    callwindow.cpp \
    contacts.cpp \
    ../PICA_client.c \
    ../PICA_media.c \
    ../PICA_msgproc.c \
    contactlistwidget.cpp \
    mediadevice.cpp \
    skynet.cpp \
    nodes.cpp \
    accountswindow.cpp \
    accounts.cpp \
    dialogs/addaccountdialog.cpp \
    askpassword.cpp \
    msguirouter.cpp \
    picasystray.cpp \
    picaactioncenter.cpp \
    dialogs/viewcertdialog.cpp \
    dialogs/forgedcertdialog.cpp \
    dialogs/registeraccountdialog.cpp \
    openssltool.cpp \
    history.cpp \
    ../PICA_id.c \
    dhparam.cpp \
    dialogs/showpicaiddialog.cpp \
    dialogs/filetransferdialog.cpp \
    filetransfercontroller.cpp \
    sound.cpp \
    dialogs/settingsdialog.cpp \
    settings.cpp \
    ../PICA_netconf.c \
    ../PICA_signverify.c \
    dialogs/nodesdialog.cpp \
    videodevice.cpp

HEADERS  += mainwindow.h \
    audiodevice.h \
    audioring.h \
    echocanceller.h \
    nativeaudio.h \
    audiovideocallcontroller.h \
    chatwindow.h \
    callwindow.h \
    contacts.h \
    globals.h \
    ../PICA_client.h \
    ../PICA_media.h \
    ../PICA_msgproc.h \
    contactlistwidget.h \
    mediadevice.h \
    skynet.h \
    nodes.h \
    accountswindow.h \
    accounts.h \
    dialogs/addaccountdialog.h \
    askpassword.h \
    msguirouter.h \
    picasystray.h \
    picaactioncenter.h \
    dialogs/viewcertdialog.h \
    dialogs/forgedcertdialog.h \
    ../PICA_common.h \
    ../PICA_proto.h \
    dialogs/registeraccountdialog.h \
    openssltool.h \
    history.h \
    ../PICA_id.h \
    dhparam.h \
    dialogs/showpicaiddialog.h \
    dialogs/filetransferdialog.h \
    filetransfercontroller.h \
    sound.h \
    dialogs/settingsdialog.h \
    settings.h \
    ../PICA_netconf.h \
    ../PICA_security.h \
    ../PICA_signverify.h \
    dialogs/nodesdialog.h \
    videodevice.h


DEFINES = PICA_MULTITHREADED
DEFINES += HAVE_LIBMINIUPNPC

FORMS    += mainwindow.ui



unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += libssl

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += libcrypto

unix|win32: LIBS += -lminiupnpc

# Acoustic echo cancellation. Required everywhere: on Linux there is no other
# cancellation in the picture, and on the platforms that have their own it is
# still the fallback for the endpoints and drivers that turn out not to.
unix:!macx: CONFIG += link_pkgconfig
unix:!macx: PKGCONFIG += speexdsp

# Listing PulseAudio sources and sinks in the settings dialog. Optional -
# playing and capturing through PulseAudio goes via FFmpeg's own "pulse"
# device and needs nothing from us.
unix:!macx: PKGCONFIG += libpulse
unix:!macx: DEFINES += HAVE_LIBPULSE

# The native audio backends. Both source files compile to nothing on the
# platforms they are not for, so they can stay in SOURCES unconditionally;
# what they link against cannot.
win32: LIBS += -lole32 -loleaut32
macx:  LIBS += -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreFoundation
macx:  LIBS += -lspeexdsp
