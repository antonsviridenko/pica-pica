#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([Pica Pica Messenger], [0.7.1], [http://picapica.im/bugzilla/], [pica-pica], [http://picapica.im])
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
	[*-w64-mingw*], [QT_DEPENDENCIES_LIBS="$QT_DEPENDENCIES_LIBS -mwindows"],
	[]
)
AC_SUBST([QT_DEPENDENCIES_CFLAGS])
AC_SUBST([QT_DEPENDENCIES_LIBS])
AC_CHECK_PROGS([UIC], [$QT_UIC_NAMES], [notfound])
AC_CHECK_PROGS([MOC], [$QT_MOC_NAMES], [notfound])
#fail if not found ...
AS_IF([test "x$UIC" = xnotfound],[AC_MSG_ERROR(["uic program not found. Please install Qt development package"])])
AS_IF([test "x$MOC" = xnotfound],[AC_MSG_ERROR(["moc program not found. Please install Qt development package"])])

#check for openssl command line tool

AC_CHECK_PROG([OPENSSL_TOOL], [openssl], [yes], [no])
AS_IF([test "x$OPENSSL_TOOL" != xyes],[AC_MSG_WARN(["openssl command line tool not found. This is a runtime dependency for pica-client!"])])

#check for FFmpeg libraries
PKG_CHECK_MODULES([FFMPEG],["libavcodec libavdevice libavformat libavutil libswresample libswscale"])
AC_SUBST([FFMPEG_CFLAGS])
AC_SUBST([FFMPEG_LIBS])

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

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
