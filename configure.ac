AC_PREREQ(2.61)
AC_INIT([aquilon-tools], [1.30.1])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_HEADERS([config.h])
AC_CANONICAL_TARGET
AC_PREFIX_DEFAULT([/usr/local])
AM_INIT_AUTOMAKE([subdir-objects])
AM_SILENT_RULES([yes])
AM_PATH_PYTHON([2.7.3])

AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

AC_ARG_VAR(ZLIB_DIR, [path to zlib library])
AC_ARG_VAR(ZLIB_INC, [path to zlib header files])
AC_SUBST(LDFLAGS, "-L${ZLIB_DIR} -Xlinker -rpath -Xlinker ${ZLIB_DIR}")
AC_SUBST(CPPFLAGS, "-I${ZLIB_INC}")
AC_CHECK_LIB(z, gzbuffer, ,AC_MSG_ERROR([this library is required]))
AC_CHECK_HEADERS(zlib.h, ,AC_MSG_ERROR([this header is required]))

AC_CONFIG_FILES(        Makefile
                        src/Makefile
                        src/etc/Makefile
                        src/bin/Makefile
                        src/trifle/Makefile
                        src/tests/Makefile
                        src/doc/Makefile
                        src/doc/man1/Makefile
                        src/doc/man5/Makefile)
AC_OUTPUT
