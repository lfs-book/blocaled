AUTOMAKE_OPTIONS = serial-tests
check_PROGRAMS = mylocaled gdbus-mock-polkit
TESTS = locale-read \
        keyboard-read \
        xkbd-read \
        locale-write \
        keyboard-write \
        keyboard-write-no-file \
        locale-write-no-file \
        locale-write-twice \
        locale-erase-write \
        locale-read-userconf \
        keyboard-read-userconf \
        xkbd-read-userconf \
        bad-read-userconf

nodist_mylocaled_SOURCES = mylocaled.c
mylocaled.c: $(top_srcdir)/src/main.c
	cp $< $@

mylocaled_CPPFLAGS = \
        -include $(top_builddir)/config.h \
        -DPKGDATADIR=\""$(abs_top_srcdir)"/data\" \
        -DPIDFILE=\""$(abs_builddir)"/scratch/mylocaled.pid\" \
        -DLOCALECONFIG=\""$(abs_builddir)"/scratch/locale\" \
        -DKEYBOARDCONFIG=\""$(abs_builddir)"/scratch/keyboard\" \
        -DXKBDCONFIG=\""$(abs_builddir)"/scratch/xkeyboard\" \
        -DSYSCONFDIR=\""$(abs_builddir)"/scratch\" \
        $(BLOCALED_CFLAGS) \
        -I$(top_srcdir)/src \
        -I$(top_builddir)/src \
        $(NULL)

gdbus_mock_polkit_CPPFLAGS = \
        $(BLOCALED_CFLAGS) \
        $(NULL)

mylocaled_LDADD = \
        $(BLOCALED_LIBS) \
        $(top_builddir)/src/locale1-generated.o \
        $(top_builddir)/src/localed.o \
        $(top_builddir)/src/polkitasync.o \
        $(top_builddir)/src/shellparser.o \
        $(NULL)

gdbus_mock_polkit_LDADD = \
	$(BLOCALED_LIBS) \
	$(NULL)

CLEANFILES = \
	     mylocaled.c \
	     scratch/org.freedesktop.locale1.service \
	     scratch/test-session.xml \
             locale-read.log \
             keyboard-read.log \
             xkbd-read.log \
             locale-write.log \
             keyboard-write.log \
             keyboard-write-no-file.log \
             locale-write-no-file.log \
             locale-write-twice.log \
             locale-erase-write.log \
             locale-read-userconf.log \
             keyboard-read-userconf.log \
             xkbd-read-userconf.log \
             bad-read-userconf.log \
	     $(NULL)

EXTRA_DIST = $(TESTS) \
	     ref-dbus.sh \
	     unref-dbus.sh \
	     ref-polkit.sh \
	     unref-polkit.sh \
	     ref-localed-myconf.sh \
	     unref-localed-myconf.sh \
	     scratch/locale-read-result1 \
	     scratch/keyboard-read-result1 \
	     scratch/keyboard-read-result2 \
	     scratch/xkeyboard-read-result1 \
	     scratch/system.d/mylocaled.conf \
	     scratch/system.d/polkit-mock.conf \
	     $(NULL)
