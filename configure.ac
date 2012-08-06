#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([Pica Pica Node], [0.5], [http://picapica.im/bugzilla/], [pica-node], [http://picapica.im])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([PICA_node.c])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB([crypto], [RAND_add])
AC_CHECK_LIB([ssl], [SSL_library_init])
AC_CHECK_LIB([sqlite3], [sqlite3_open])

AC_CHECK_PROG([SQLITE3], [sqlite3], [yes], [no])

if test "x$SQLITE3" != xyes; then
            AC_MSG_ERROR(["sqlite3 program not found"])
   fi


# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h limits.h netdb.h netinet/in.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h wchar.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([getcwd inet_ntoa memmove memset select socket strchr strstr strtol])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
