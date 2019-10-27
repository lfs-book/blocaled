#AUTOMAKE_OPTIONS = subdir-objects
check_PROGRAMS = mylocaled
TESTS = locale-read
AM_TESTS_ENVIRONMENT = . $(src_dir)/tests-env.sh;

mylocaled_SOURCES = mylocaled.c
mylocaled.c: $(top_srcdir)/src/main.c
	cp $< $@

AM_CPPFLAGS = \
        -include $(top_builddir)/config.h \
        -DPKGDATADIR=\""$(abs_top_srcdir)"/data/kbd-model-map\" \
        -DPIDFILE=\""$(abs_builddir)"/scratch/mylocaled.pid\" \
        -DLOCALECONFIG=\""$(abs_builddir)"/scratch/locale\" \
        -DKEYBOARDCONFIG=\""$(abs_builddir)"/scratch/keyboard\" \
        -DXKBDCONFIG=\""$(abs_builddir)"/scratch/xkeyboard\" \
        $(BLOCALED_CFLAGS) \
        -I$(top_srcdir)/src \
        -I$(top_builddir)/src \
        $(NULL)

LDADD = \
	$(BLOCALED_LIBS) \
	$(top_builddir)/src/locale1-generated.o \
	$(top_builddir)/src/localed.o \
	$(top_builddir)/src/polkitasync.o \
	$(top_builddir)/src/shellparser.o \
	$(NULL)

