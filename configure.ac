#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([Pica Pica Messenger], [0.5], [http://picapica.im/bugzilla/], [pica-pica], [http://picapica.im])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([PICA_node.c])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

# Checks for common libraries, required by both pica-node and pica-client
AC_CHECK_LIB([crypto], [RAND_add],[],[],[ -ldl -lz ])
AC_CHECK_LIB([ssl], [SSL_library_init],[],[], [ -lcrypto -ldl -lz ])


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
AC_CHECK_LIB([sqlite3], [sqlite3_open])

AC_CHECK_PROG([SQLITE3], [sqlite3], [yes], [no])

AS_IF([test "x$SQLITE3" != xyes],  [ AC_MSG_ERROR(["sqlite3 program not found"])])
])


#pica-client tests
AS_IF([test "x$PICA_CLIENT_ENABLED" = "xyes"],[
#AC_PROG_CXX
PKG_CHECK_MODULES([QT_DEPENDENCIES], [QtCore QtGui QtSql QtNetwork])
AC_SUBST([QT_DEPENDENCIES_CFLAGS])
AC_SUBST([QT_DEPENDENCIES_LIBS])
AC_CHECK_PROG([UIC], [uic], [yes], [no])
AC_CHECK_PROG([MOC], [moc], [yes], [no])
#fail if not found ...
AS_IF([test "x$UIC" != xyes],[AC_MSG_ERROR(["uic program not found. Please install Qt development package"])])
AS_IF([test "x$MOC" != xyes],[AC_MSG_ERROR(["moc program not found. Please install Qt development package"])])

#check for xdg-utils
AC_CHECK_PROG([XDG_DESKTOP_MENU], [xdg-desktop-menu], [yes], [no])
AS_IF([test "x$XDG_DESKTOP_MENU" != xyes],[AC_MSG_WARN(["Looks like xdg-utils are not installed. Desktop menu item installation disabled"])])

#check for openssl command line tool

AC_CHECK_PROG([OPENSSL_TOOL], [openssl], [yes], [no])
AS_IF([test "x$OPENSSL_TOOL" != xyes],[AC_MSG_WARN(["openssl command line tool not found. This is a runtime dependency for pica-client!"])])
])

AM_CONDITIONAL([XDG_UTILS], [test "x$XDG_DESKTOP_MENU" = "xyes"])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h limits.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([getcwd inet_ntoa memmove memset select socket strchr strstr strtol])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
