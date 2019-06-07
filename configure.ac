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
	[*-w64-mingw*], [QT_DEPENDENCIES_LIBS="$QT_DEPENDENCIES_LIBS -mwindows"]
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
])


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h limits.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T

# Checks for library functions.
AC_CHECK_FUNCS([getcwd inet_ntoa memmove memset select socket strchr strstr strtol])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
