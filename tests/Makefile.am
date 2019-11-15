check_PROGRAMS = mylocaled gdbus-mock-polkit
DEFAULT_CONFIG_TESTS = locale-read \
                       keyboard-read \
                       xkbd-read \
                       locale-write \
                       locale-write-no-file \
                       locale-write-twice \
                       locale-erase-write

default_config_tests_log = $(DEFAULT_CONFIG_TESTS:=.log)

USER_CONFIG_TESTS = locale-read-userconf \
                    keyboard-read-userconf \
                    xkbd-read-userconf

user_config_tests_log = $(USER_CONFIG_TESTS:=.log)

$(user_config_tests_log): $(default_config_tests_log)

TESTS = $(DEFAULT_CONFIG_TESTS) $(USER_CONFIG_TESTS)


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
	     $(NULL)

EXTRA_DIST = $(TESTS) \
	     ref-dbus.sh \
	     unref-dbus.sh \
	     ref-polkit.sh \
	     unref-polkit.sh \
	     scratch/locale-read-result1 \
	     scratch/keyboard-read-result1 \
	     scratch/xkeyboard-read-result1 \
	     scratch/system.d/blocaled.conf \
	     scratch/system.d/polkit-mock.conf \
	     $(NULL)
