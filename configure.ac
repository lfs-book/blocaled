AC_PREREQ([2.60])
AC_INIT([blocaled], [0.4], [https://github.com/lfs-book/blocaled/issues], [blocaled], [https://github.com/lfs-book/blocaled])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([check-news foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz])
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])
LT_INIT([disable-static pic-only])

AC_PROG_MKDIR_P
PKG_CHECK_MODULES(BLOCALED,
                  [gio-unix-2.0 >= 2.44
                   gio-2.0 >= 2.44
                   glib-2.0 >= 2.44
                   dbus-1
                   polkit-gobject-1
                   libdaemon])
AC_SUBST(BLOCALED_CFLAGS)
AC_SUBST(BLOCALED_LIBS)

AC_PATH_PROG(GDBUS_CODEGEN, gdbus-codegen)
if test "x$GDBUS_CODEGEN" = x; then
    AC_MSG_ERROR([Failed to find gdbus-codegen])
fi

AC_ARG_WITH([pidfile], AS_HELP_STRING([--with-pidfile=FILENAME], [pid filename @<:@default=/run/blocaled.pid@:>@]), [], [with_pidfile=/run/blocaled.pid])
AC_SUBST([pidfile], [$with_pidfile])

AC_ARG_WITH([localeconfig], AS_HELP_STRING([--with-localeconfig=FILENAME], [locale config filename @<:@default=/etc/locale.conf@:>@]), [], [with_localeconfig=/etc/locale.conf])
AC_SUBST([localeconfig], [$with_localeconfig])

AC_ARG_WITH([keyboardconfig], AS_HELP_STRING([--with-keyboardconfig=FILENAME], [keyboard config filename @<:@default=/etc/sysconfig/console@:>@]), [], [with_keyboardconfig=/etc/sysconfig/console])
AC_SUBST([keyboardconfig], [$with_keyboardconfig])

AC_ARG_WITH([xkbdconfig], AS_HELP_STRING([--with-xkbdconfig=FILENAME], [X keyboard config filename @<:@default=/etc/X11/xorg.conf.d/30-keyboard.conf@:>@]), [], [with_xkbdconfig=/etc/X11/xorg.conf.d/30-keyboard.conf])
AC_SUBST([xkbdconfig], [$with_xkbdconfig])

AC_MSG_CHECKING([dbus interfaces directory])
dbusinterfacesdir=`$PKG_CONFIG --variable=interfaces_dir dbus-1 \
                               --define-variable=prefix=$prefix`
if test "x$dbusinterfacesdir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$dbusinterfacesdir])
fi
AC_SUBST([dbusinterfacesdir])

AC_MSG_CHECKING([dbus system services directory])
dbussystemservicesdir=`$PKG_CONFIG --variable=system_bus_services_dir dbus-1 \
                                   --define-variable=prefix=$prefix`
if test "x$dbussystemservicesdir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$dbussystemservicesdir])
fi
AC_SUBST([dbussystemservicesdir])

AC_MSG_CHECKING([polkit actions directory])
polkitactiondir=`$PKG_CONFIG --variable=actiondir polkit-gobject-1 \
                             --define-variable=prefix=$prefix`
if test "x$polkitactiondir" = "x"; then
    AC_MSG_ERROR([not found])
else
    AC_MSG_RESULT([$polkitactiondir])
fi
AC_SUBST([polkitactiondir])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
    Makefile
    data/blocaled.conf
    data/blocaled.8
    tests/Makefile
    tests/scratch/org.freedesktop.locale1.service
    tests/scratch/test-session.xml
])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE $VERSION
        =====

        prefix:                   ${prefix}
        sysconfdir:               ${sysconfdir}

        pid file:                 ${with_pidfile}
        locale config file:       ${with_localeconfig}
        keyboard config file:     ${with_keyboardconfig}
        X11 keyboard config file: ${with_xkbdconfig}

        compiler:                 ${CC}
        cflags:                   ${CFLAGS}
        ldflags:                  ${LDFLAGS}
])
