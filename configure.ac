#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([Pica Pica Messenger], [0.9.0dev], [http://picapica.im/bugzilla/], [pica-pica], [http://picapica.im])
AM_INIT_AUTOMAKE([-Wall  foreign subdir-objects])
AC_CONFIG_SRCDIR([PICA_node.c])

AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_SYS_LARGEFILE

# Platform-specific environment settings
AS_CASE([$host],
    [*-apple-darwin*], [LDFLAGS="$LDFLAGS -headerpad_max_install_names"],
    [*-w64-mingw*], [
	AC_CHECK_LIB([ws2_32], [main], [LIBS="$LIBS -lws2_32"], [])
	AC_CHECK_LIB([winmm], [main], [LIBS="$LIBS -lwinmm"], [])
	AC_CHECK_LIB([iphlpapi], [main], [LIBS="$LIBS -liphlpapi"])
	],
    []
)

AC_ARG_WITH([miniupnpc],
  [AS_HELP_STRING([--without-miniupnpc],
  [disable support for miniupnpc])],
  [],
  [with_miniupnpc=yes])

AC_ARG_WITH([qt],
  [AS_HELP_STRING([--with-qt=qt4|qt5 qt5 is default],
  [Select Qt version, 4 or 5])],
  [qt_version=$withval],
  [qt_version=qt5])

AS_IF([test "x$with_miniupnpc" != xno],
  [AC_CHECK_LIB([miniupnpc], [main],
    [],
    [AC_MSG_FAILURE(
      [miniupnpc test failed (--without-miniupnpc to disable)])],
  [])])

AS_IF([test "x$qt_version" = xqt5],
	[
	QT_MODULES="Qt5Core Qt5Gui Qt5Sql Qt5Network Qt5Widgets"
	QT_UIC_NAMES="uic-qt5 uic5 uic"
	QT_MOC_NAMES="moc-qt5 moc5 moc"
	CFLAGS="-fPIC $CFLAGS"
	CXXFLAGS="-fPIC $CXXFLAGS"
	],
  [test "x$qt_version" = xqt4],
	[
	QT_MODULES="QtCore QtGui QtSql QtNetwork"
	QT_UIC_NAMES="uic-qt4 uic4 uic"
	QT_MOC_NAMES="moc-qt4 moc4 moc"
	],
  [AC_MSG_ERROR(["Invalid Qt version"])])

# Checks for common libraries, required by both pica-node and pica-client
AC_SEARCH_LIBS([dlopen], [dl])
AC_CHECK_LIB([crypto], [RAND_add],[],[AC_MSG_ERROR(["libcrypto from OpenSSL package not found"])],[ -lz ])
AC_CHECK_LIB([ssl], [SSL_new],[],[AC_MSG_ERROR(["libssl from OpenSSL package not found"])])
AC_CHECK_LIB([pthread], [pthread_create])

#checking if pica-node is disabled
AC_ARG_ENABLE([node],
    AS_HELP_STRING([--disable-node], [Disable build of pica-node]))

AS_IF([test "x$enable_node" != "xno"], [
  PICA_NODE_ENABLED=yes
])

#checking if pica-client is disabled
AC_ARG_ENABLE([client],
    AS_HELP_STRING([--disable-client], [Disable build of pica-client]))

AS_IF([test "x$enable_client" != "xno"], [
  PICA_CLIENT_ENABLED=yes
])

#checking if VAAPI hardware acceleration is wanted
#unspecified means autodetect: use it when the libraries are there, do without
#them when they are not. --enable-vaapi makes their absence a hard error,
#--disable-vaapi skips looking for them at all.
AC_ARG_ENABLE([vaapi],
    AS_HELP_STRING([--enable-vaapi],
      [Enable VAAPI accelerated video encoding, decoding and rendering (default: autodetect)])
    AS_HELP_STRING([--disable-vaapi],
      [Disable VAAPI accelerated video encoding, decoding and rendering]))

#checking if PulseAudio device enumeration is wanted. Only affects what the
#settings dialog can list; see the check further down.
AC_ARG_ENABLE([pulse],
    AS_HELP_STRING([--enable-pulse],
      [Enable listing of PulseAudio/PipeWire capture and playback devices (default: autodetect)])
    AS_HELP_STRING([--disable-pulse],
      [Disable listing of PulseAudio/PipeWire devices]))

#checking if the X11 rendering path is wanted
#Qt gives widgets a GLX context by default in an X11 session, and a GLX
#context has no way to import the DMA-BUF handles a VA surface is exported
#as - that path needs EGL. vaPutSurface() leaves OpenGL out of it altogether
#and has the driver draw the surface straight onto an X11 window, which is
#what makes hardware rendering work in such a session. Autodetected like
#VAAPI itself, and pointless without it.
AC_ARG_ENABLE([vaapi-x11],
    AS_HELP_STRING([--enable-vaapi-x11],
      [Enable vaPutSurface() rendering, for X11 sessions where Qt uses GLX rather than EGL (default: autodetect)])
    AS_HELP_STRING([--disable-vaapi-x11],
      [Disable vaPutSurface() rendering]))

#at least one program must be enabled
AS_IF([test "x$PICA_NODE_ENABLED" != "xyes" && test "x$PICA_CLIENT_ENABLED" != "xyes"  ],
  AC_MSG_ERROR(["Either pica-node or pica-client must be enabled"])
)


AM_CONDITIONAL([PICA_NODE], [test "x$PICA_NODE_ENABLED" = "xyes"])
AM_CONDITIONAL([PICA_CLIENT], [test "x$PICA_CLIENT_ENABLED" = "xyes"])

#pica-node tests
AS_IF([test "x$PICA_NODE_ENABLED" = "xyes"],[
AC_CHECK_LIB([sqlite3], [sqlite3_open], [], [AC_MSG_ERROR(["libsqlite3 library not found"])])
AC_CHECK_LIB([event], [event_get_version], [PICA_NODE_LIBS="-levent"], [AC_MSG_ERROR(["libevent not found"])])
AC_SUBST([PICA_NODE_LIBS])
AC_CHECK_PROG([SQLITE3], [sqlite3], [yes], [no])

AS_IF([test "x$SQLITE3" != xyes],  [ AC_MSG_ERROR(["sqlite3 program not found"])])
])


#pica-client tests
AS_IF([test "x$PICA_CLIENT_ENABLED" = "xyes"],[
#AC_PROG_CXX
PKG_CHECK_MODULES([QT_DEPENDENCIES], [$QT_MODULES])
AS_CASE([$host],
	[*-linux-*],
		[
		PKG_CHECK_MODULES([PLATFORM_DEPS],["alsa"])
		],
	[*-w64-mingw*],
		[
		TARGET_OS=windows
		QT_DEPENDENCIES_LIBS="$QT_DEPENDENCIES_LIBS -mwindows"
		#the WASAPI backend is COM: ole32 for CoCreateInstance and the
		#task allocator, oleaut32 for PROPVARIANT.
		PLATFORM_DEPS_LIBS="$PLATFORM_DEPS_LIBS -lole32 -loleaut32"
		AC_PATH_TOOL(WINDRES, windres, none)
		if test x$WINDRES = xnone; then
			AC_MSG_ERROR("windres not found")
		fi
		],
	[*-apple-darwin*],
		[
		TARGET_OS=darwin
		#the CoreAudio backend: the HAL for device enumeration,
		#AudioUnit/AudioToolbox for the VoiceProcessingIO unit that does
		#the echo cancellation, CoreFoundation for the CFString handling
		#the HAL's names come back as.
		PLATFORM_DEPS_LIBS="$PLATFORM_DEPS_LIBS -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework CoreFoundation"
		],
	[]
)
AC_SUBST([QT_DEPENDENCIES_CFLAGS])
AC_SUBST([QT_DEPENDENCIES_LIBS])
AC_CHECK_PROGS([UIC], [$QT_UIC_NAMES], [notfound])
AC_CHECK_PROGS([MOC], [$QT_MOC_NAMES], [notfound])
#fail if not found ...
AS_IF([test "x$UIC" = xnotfound],[AC_MSG_ERROR(["uic program not found. Please install Qt development package"])])
AS_IF([test "x$MOC" = xnotfound],[AC_MSG_ERROR(["moc program not found. Please install Qt development package"])])

#check for FFmpeg libraries
PKG_CHECK_MODULES([FFMPEG],["libavcodec libavdevice libavformat libavutil libswresample libswscale"])
AC_SUBST([FFMPEG_CFLAGS])
AC_SUBST([FFMPEG_LIBS])

#check for webrtc-audio-processing, which provides AEC3 - the acoustic echo
#cancellation pica-client runs itself. Not optional: on Linux there is no
#other cancellation in the picture unless the user has set their sound server
#up for it, and on the platforms that have their own this is still what the
#"use own echo cancellation" setting selects.
#
#Either the 1.x or the 2.x ABI will do. They install in parallel under
#separate pkg-config names, and pica-client/echocanceller.cpp carries the few
#conditionals the API break between them needs. 0.x is not a candidate at any
#price: it predates AEC3 entirely, and its whole API is different.
#
#1.x is preferred when both are installed, only because it is the one this has
#had the most hours on. --with-webrtc-audio-processing=2 forces the other,
#which is how the 2.x path gets exercised deliberately rather than by accident
#of what is on the build machine.
AC_ARG_WITH([webrtc-audio-processing],
  [AS_HELP_STRING([--with-webrtc-audio-processing=@<:@auto|1|2@:>@],
  [webrtc-audio-processing major version to build against (default: auto, preferring 1)])],
  [], [with_webrtc_audio_processing=auto])

AS_CASE([$with_webrtc_audio_processing],
  [auto], [webrtc_audio_processing_try="1 2"],
  [1],    [webrtc_audio_processing_try="1"],
  [2],    [webrtc_audio_processing_try="2"],
  [AC_MSG_ERROR([--with-webrtc-audio-processing must be auto, 1 or 2])])

WEBRTC_AUDIO_PROCESSING_MAJOR=
for webrtc_audio_processing_major in $webrtc_audio_processing_try; do
  AS_IF([test "x$WEBRTC_AUDIO_PROCESSING_MAJOR" = x],
    [PKG_CHECK_MODULES([WEBRTC_AUDIO_PROCESSING],
      [webrtc-audio-processing-$webrtc_audio_processing_major],
      [WEBRTC_AUDIO_PROCESSING_MAJOR=$webrtc_audio_processing_major],
      [:])])
done

AS_IF([test "x$WEBRTC_AUDIO_PROCESSING_MAJOR" = x],
  [AC_MSG_ERROR(["webrtc-audio-processing 1.x or 2.x was not found. It provides the acoustic echo cancellation used in calls. On Debian 13 and Ubuntu 25.10 or newer it is libwebrtc-audio-processing-dev; the same name on Ubuntu 24.04 is version 0.3, which predates AEC3 and will not do. On Gentoo it is media-libs/webrtc-audio-processing:1"])])

AC_MSG_NOTICE([using webrtc-audio-processing-$WEBRTC_AUDIO_PROCESSING_MAJOR])

#echocanceller.cpp can work this out from the headers on its own, but only the
#check above knows which one was picked when both are installed.
AC_DEFINE_UNQUOTED([PICA_WEBRTC_AUDIO_PROCESSING_MAJOR],
  [$WEBRTC_AUDIO_PROCESSING_MAJOR],
  [Major version of the webrtc-audio-processing library being built against])

AC_SUBST([WEBRTC_AUDIO_PROCESSING_CFLAGS])
AC_SUBST([WEBRTC_AUDIO_PROCESSING_LIBS])

#the WebRTC headers, and the abseil ones they include, refuse to compile as
#anything older. Recent compilers default to C++17 or later and need nothing
#added; older ones have to be told.
AC_LANG_PUSH([C++])
AC_MSG_CHECKING([whether $CXX compiles C++17 without help])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#if __cplusplus < 201703L
#error C++17 is not the default
#endif]], [])],
  [AC_MSG_RESULT([yes])],
  [
    AC_MSG_RESULT([no])
    pica_saved_cxxflags="$CXXFLAGS"
    CXXFLAGS="$CXXFLAGS -std=c++17"
    AC_MSG_CHECKING([whether $CXX accepts -std=c++17])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#if __cplusplus < 201703L
#error C++17 is not enabled
#endif]], [])],
      [AC_MSG_RESULT([yes])],
      [
        AC_MSG_RESULT([no])
        CXXFLAGS="$pica_saved_cxxflags"
        AC_MSG_ERROR(["A C++17 compiler is required to build pica-client"])
      ])
  ])
AC_LANG_POP([C++])

#check for libpulse, used only to list PulseAudio sources and sinks in the
#settings dialog - playing and capturing through PulseAudio goes via FFmpeg's
#own "pulse" device and needs nothing from us. Without it the "PulseAudio /
#PipeWire" driver still works, but offers only the server's default device.
AS_IF([test "x$enable_pulse" != "xno"], [
  PKG_CHECK_MODULES([PULSE], [libpulse],
    [
      PICA_PULSE_ENABLED=yes
      AC_DEFINE([HAVE_LIBPULSE], [1], [Define to 1 if PulseAudio device enumeration is available])
      AC_SUBST([PULSE_CFLAGS])
      AC_SUBST([PULSE_LIBS])
    ],
    [
      AS_IF([test "x$enable_pulse" = "xyes"],
        [AC_MSG_ERROR(["PulseAudio support was requested but libpulse was not found"])],
        [AC_MSG_WARN(["libpulse not found - the PulseAudio driver will only offer the default device"])])
    ])
])

AM_CONDITIONAL([PICA_PULSE], [test "x$PICA_PULSE_ENABLED" = "xyes"])

#check for VAAPI libraries, unless explicitly disabled
#libva is the accelerator itself, libva-drm opens it through a DRM render node,
#and EGL is what lets a decoded surface be handed to OpenGL without a copy.
AS_IF([test "x$enable_vaapi" != "xno"], [
  PKG_CHECK_MODULES([VAAPI], [libva libva-drm egl],
    [
      PICA_VAAPI_ENABLED=yes
      AC_DEFINE([HAVE_VAAPI], [1], [Define to 1 if VAAPI accelerated video is available])
      AC_SUBST([VAAPI_CFLAGS])
      AC_SUBST([VAAPI_LIBS])
    ],
    [
      AS_IF([test "x$enable_vaapi" = "xyes"],
        [AC_MSG_ERROR(["VAAPI support was requested but libva, libva-drm or EGL was not found"])],
        [AC_MSG_WARN(["libva, libva-drm or EGL not found - building without VAAPI acceleration"])])
    ])
])

#check for libva's X11 rendering support, needed to draw a decoded surface in
#an X11 session, where Qt's OpenGL contexts are GLX rather than EGL ones.
#libGL comes along because working out which of the two a session gives means
#asking GLX whether it owns the context.
AS_IF([test "x$enable_vaapi_x11" = "xyes" && test "x$PICA_VAAPI_ENABLED" != "xyes"],
  [AC_MSG_ERROR(["X11 rendering was requested but VAAPI support is not enabled"])])

AS_IF([test "x$PICA_VAAPI_ENABLED" = "xyes" && test "x$enable_vaapi_x11" != "xno"], [
  PKG_CHECK_MODULES([VAAPI_X11], [libva-x11 x11 gl],
    [
      PICA_VAAPI_X11_ENABLED=yes
      AC_DEFINE([HAVE_VAAPI_X11], [1], [Define to 1 if libva X11 rendering support is available])
      AC_SUBST([VAAPI_X11_CFLAGS])
      AC_SUBST([VAAPI_X11_LIBS])
    ],
    [
      AS_IF([test "x$enable_vaapi_x11" = "xyes"],
        [AC_MSG_ERROR(["X11 rendering was requested but libva-x11, libX11 or libGL was not found"])],
        [AC_MSG_WARN(["libva-x11, libX11 or libGL not found - building without X11 rendering, hardware rendering will need an EGL session"])])
    ])
])
])

AM_CONDITIONAL([PICA_VAAPI], [test "x$PICA_VAAPI_ENABLED" = "xyes"])
AM_CONDITIONAL([PICA_VAAPI_X11], [test "x$PICA_VAAPI_X11_ENABLED" = "xyes"])

AC_SUBST([PLATFORM_DEPS_CFLAGS])
AC_SUBST([PLATFORM_DEPS_LIBS])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h limits.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T

# Checks for library functions.
AC_CHECK_FUNCS([getcwd inet_ntoa memmove memset select socket strchr strstr strtol])

VERSION=$PACKAGE_VERSION
AC_SUBST([VERSION])

# Both the assemblyIdentity element of a Windows application manifest and the
# FILEVERSION field of a VERSIONINFO resource only accept a version of exactly
# four numbers, which the package version ("0.9.0dev") is not. Cut it at the
# first character that is neither a digit nor a dot and pad the rest with ".0"
# up to four components. The manifest wants them separated by dots and
# VERSIONINFO by commas, so both spellings are substituted.
AC_MSG_CHECKING([for the Windows binary version number])
PICA_WINDOWS_VERSION=`echo "$PACKAGE_VERSION" | sed -e ['s/[^0-9.].*$//'] -e ['s/\.*$//']`
AS_IF([test -z "$PICA_WINDOWS_VERSION"], [PICA_WINDOWS_VERSION=0])
while test `echo "$PICA_WINDOWS_VERSION" | tr -dc . | wc -c` -lt 3
do
	PICA_WINDOWS_VERSION="$PICA_WINDOWS_VERSION.0"
done
PICA_WINDOWS_VERSION_COMMAS=`echo "$PICA_WINDOWS_VERSION" | tr . ,`
AC_MSG_RESULT([$PICA_WINDOWS_VERSION])
AC_SUBST([PICA_WINDOWS_VERSION])
AC_SUBST([PICA_WINDOWS_VERSION_COMMAS])

AM_CONDITIONAL([TARGET_WINDOWS], [test x$TARGET_OS = xwindows])
AM_CONDITIONAL([TARGET_DARWIN], [test x$TARGET_OS = xdarwin])
AC_CONFIG_FILES([Makefile windows/installer32.nsi windows/installer64.nsi windows/pica-client.exe.manifest windows/pica-node.exe.manifest])
AC_OUTPUT
