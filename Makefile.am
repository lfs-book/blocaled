ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	data/org.freedesktop.locale1.service.in \
	data/init.d/openrc-settingsd.in \
	AUTHORS \
	COPYING \
	src/copypaste/COPYING.LGPL-2.1 \
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
	data/openrc-settingsd.conf \
	$(NULL)

polkitactiondir = @polkitactiondir@
dist_polkitaction_DATA = \
	data/org.freedesktop.locale1.policy \
	$(NULL)

initddir = $(sysconfdir)/init.d
initd_SCRIPTS = data/init.d/openrc-settingsd

confddir = $(sysconfdir)/conf.d
dist_confd_DATA = data/conf.d/openrc-settingsd

pidfile = @pidfile@
do_subst = $(SED) -e 's,[@]libexecdir[@],$(libexecdir),g' \
	-e 's,[@]pidfile[@],$(pidfile),g' \
	$(NULL)

data/init.d/openrc-settingsd : data/init.d/openrc-settingsd.in
	$(do_subst) < $(srcdir)/data/init.d/openrc-settingsd.in > $(srcdir)/data/init.d/openrc-settingsd

%.service : %.service.in
	$(do_subst) < $(srcdir)/$< > $(srcdir)/$@

dist_man_MANS = data/openrc-settingsd.8

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPIDFILE=\""$(pidfile)"\" \
	-DENV_UPDATE=\""$(prefix)/sbin/env-update"\" \
	$(OPENRC_SETTINGSD_CFLAGS) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(NULL)

LDADD = $(OPENRC_SETTINGSD_LIBS)

libexec_PROGRAMS = openrc-settingsd

localed_built_sources = \
	src/locale1-generated.c \
	src/locale1-generated.h \
	$(NULL)

copypaste_sources = \
	src/copypaste/hwclock.c \
	src/copypaste/hwclock.h \
	src/copypaste/macro.h \
	src/copypaste/util.c \
	src/copypaste/util.h \
	$(NULL)

openrc_settingsd_SOURCES = \
	$(copypaste_sources) \
	src/localed.c \
	src/localed.h \
	src/utils.c \
	src/utils.h \
	src/main.h \
	src/main.c \
	$(NULL)

nodist_openrc_settingsd_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

$(localed_built_sources) : data/org.freedesktop.locale1.xml
	$(AM_V_GEN)( pushd "$(srcdir)/src" > /dev/null; \
	$(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdLocaled \
	--generate-c-code locale1-generated \
	$(abs_srcdir)/data/org.freedesktop.locale1.xml; \
	popd > /dev/null )

BUILT_SOURCES = \
	$(localed_built_sources) \
	$(NULL)

CLEANFILES = \
	$(localed_built_sources) \
	data/init.d/openrc-settingsd \
	$(dbusservices_DATA) \
	$(NULL)
