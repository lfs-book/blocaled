AC_PREREQ([2.60])
AC_INIT([openrc-settingsd], [0])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([check-news foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz])
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT([disable-static pic-only])
AC_PREFIX_DEFAULT([/usr])

AC_PROG_MKDIR_P
PKG_CHECK_MODULES(OPENRC_SETTINGSD, [gio-unix-2.0 >= 2.30 gio-2.0 >= 2.30 glib-2.0 >= 2.30 dbus-1 polkit-gobject-1])
AC_SUBST(OPENRC_SETTINGSD_CFLAGS)
AC_SUBST(OPENRC_SETTINGSD_LIBS)

AC_PATH_PROG(GDBUS_CODEGEN, gdbus-codegen)
if test "x$GDBUS_CODEGEN" = x; then
    AC_MSG_ERROR([Failed to find gdbus-codegen])
fi

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
    Makefile
])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE $VERSION
        =====

        prefix:                 ${prefix}
        sysconfdir:             ${sysconfdir}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}
])
