AC_PREREQ([2.60])
AC_INIT([openrc-settingsd], [1.0.1], [https://bugs.gentoo.org/], [openrc-settingsd], [http://gnome.gentoo.org/openrc-settingsd.xml])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([check-news foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz])
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT([disable-static pic-only])
AC_PREFIX_DEFAULT([/usr])

AC_PROG_MKDIR_P
AC_SEARCH_LIBS([clock_gettime], [rt], [], [AC_MSG_ERROR([librt not found])])
PKG_CHECK_MODULES(OPENRC_SETTINGSD,
                  [gio-unix-2.0 >= 2.30
                   gio-2.0 >= 2.30
                   glib-2.0 >= 2.30
                   dbus-1
                   polkit-gobject-1
                   libdaemon
                   openrc])
AC_SUBST(OPENRC_SETTINGSD_CFLAGS)
AC_SUBST(OPENRC_SETTINGSD_LIBS)

AC_PATH_PROG(GDBUS_CODEGEN, gdbus-codegen)
if test "x$GDBUS_CODEGEN" = x; then
    AC_MSG_ERROR([Failed to find gdbus-codegen])
fi

AC_ARG_WITH([pidfile], AS_HELP_STRING([--with-pidfile=FILENAME], [pid filename @<:@default=/var/run/openrc-settingsd.pid@:>@]), [], [with_pidfile=/var/run/openrc-settingsd.pid])
AC_SUBST([pidfile], [$with_pidfile])

AC_MSG_CHECKING([dbus interfaces directory])
dbusinterfacesdir=`$PKG_CONFIG --variable=interfaces_dir dbus-1`
if test "x$dbusinterfacesdir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$dbusinterfacesdir])
fi
AC_SUBST([dbusinterfacesdir])

AC_MSG_CHECKING([dbus system services directory])
dbussystemservicesdir=`$PKG_CONFIG --variable=system_bus_services_dir dbus-1`
if test "x$dbussystemservicesdir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$dbussystemservicesdir])
fi
AC_SUBST([dbussystemservicesdir])

AC_MSG_CHECKING([polkit actions directory])
polkitactiondir=`$PKG_CONFIG --variable=actiondir polkit-gobject-1`
if test "x$polkitactiondir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$polkitactiondir])
fi
AC_SUBST([polkitactiondir])

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
        pid file:               ${with_pidfile}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}
])
