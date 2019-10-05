ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AUTOMAKE_OPTIONS = subdir-objects

EXTRA_DIST = \
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
	data/blocaled.conf \
	$(NULL)

polkitactiondir = @polkitactiondir@
dist_polkitaction_DATA = \
	data/org.freedesktop.locale1.policy \
	$(NULL)

confddir = $(sysconfdir)/conf.d
dist_confd_DATA = data/conf.d/blocaled


pidfile = @pidfile@
do_subst = $(SED) -e 's,[@]libexecdir[@],$(libexecdir),g' \
	-e 's,[@]pidfile[@],$(pidfile),g' \
	$(NULL)

%.service : %.service.in
	$(do_subst) < $(srcdir)/$< > $(srcdir)/$@

dist_man_MANS = data/blocaled.8

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPIDFILE=\""$(pidfile)"\" \
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
	src/utils.c \
	src/utils.h \
	src/main.h \
	src/main.c \
	$(NULL)

nodist_blocaled_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

$(localed_built_sources) : data/org.freedesktop.locale1.xml
	$(AM_V_GEN)( pushd "$(srcdir)/src" > /dev/null; \
	$(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace BLocaled \
	--generate-c-code locale1-generated \
	$(abs_srcdir)/data/org.freedesktop.locale1.xml; \
	popd > /dev/null )

BUILT_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

CLEANFILES = \
	$(localed_built_sources) \
	$(dbusservices_DATA) \
	$(NULL)
