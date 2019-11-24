ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AUTOMAKE_OPTIONS = subdir-objects

# Put . before doc, because gtk-scangobj needs some compiled objects
# TODO add a Makefile.am to src, and add it to SUBDIRS
SUBDIRS = . doc tests

EXTRA_DIST = \
        data/blocaled.8.in \
        data/blocaled.conf.in \
	data/org.freedesktop.locale1.service.in \
	AUTHORS \
	COPYING \
	README \
	NEWS \
	TODO \
	$(NULL)

dist_pkgdata_DATA = \
	data/kbd-model-map \
	$(NULL)

sysconf_DATA = data/blocaled.conf

dbusinterfacesdir = @dbusinterfacesdir@
dist_dbusinterfaces_DATA = \
	data/org.freedesktop.locale1.xml \
	$(NULL)

dbusservicesdir = @dbussystemservicesdir@
dbusservices_DATA = \
	data/org.freedesktop.locale1.service \
	$(NULL)

dbusbusconfigdir = $(sysconfdir)/dbus-1/system.d
dist_dbusbusconfig_DATA = \
	data/org.freedesktop.locale1.conf \
	$(NULL)

polkitactiondir = @polkitactiondir@
dist_polkitaction_DATA = \
	data/org.freedesktop.locale1.policy \
	$(NULL)

pidfile = @pidfile@
#do_subst = $(SED) -e 's,[@]libexecdir[@],$(libexecdir),g' \
#	-e 's,[@]pidfile[@],$(pidfile),g' \
#	$(NULL)

#%.service : %.service.in
#	$(do_subst) < $(srcdir)/$< > $(srcdir)/$@

man_MANS = data/blocaled.8

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPIDFILE=\""$(pidfile)"\" \
	-DLOCALECONFIG=\""$(localeconfig)"\" \
	-DKEYBOARDCONFIG=\""$(keyboardconfig)"\" \
	-DXKBDCONFIG=\""$(xkbdconfig)"\" \
	$(BLOCALED_CFLAGS) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(NULL)

LDADD = $(BLOCALED_LIBS)

libexec_PROGRAMS = blocaled

localed_built_sources = \
	src/locale1-generated.c \
	src/locale1-generated.h \
	$(NULL)

blocaled_SOURCES = \
	src/localed.c \
	src/localed.h \
	src/shellparser.c \
	src/shellparser.h \
	src/polkitasync.c \
	src/polkitasync.h \
	src/main.h \
	src/main.c \
	$(NULL)

nodist_blocaled_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

$(localed_built_sources) : data/org.freedesktop.locale1.xml
	$(AM_V_GEN)( pushd "$(builddir)/src" > /dev/null; \
	$(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace BLocaled \
	--generate-c-code locale1-generated \
	--generate-docbook "$(abs_builddir)/doc/reference/locale1-generated" \
	$(abs_srcdir)/data/org.freedesktop.locale1.xml; \
	popd > /dev/null )

BUILT_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

CLEANFILES = \
	$(localed_built_sources) \
	$(dbusservices_DATA) \
	$(abs_builddir)/doc/reference/locale1-generated-org.freedesktop.locale1.xml
	$(NULL)
