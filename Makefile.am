ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	data/org.freedesktop.hostname1.service.in \
	data/org.freedesktop.locale1.service.in \
	data/org.freedesktop.timedate1.service.in \
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
	data/org.freedesktop.hostname1.xml \
	data/org.freedesktop.locale1.xml \
	data/org.freedesktop.timedate1.xml \
	$(NULL)

dbusservicesdir = @dbussystemservicesdir@
dbusservices_DATA = \
	data/org.freedesktop.hostname1.service \
	data/org.freedesktop.locale1.service \
	data/org.freedesktop.timedate1.service \
	$(NULL)

dbusbusconfigdir = $(sysconfdir)/dbus-1/system.d
dist_dbusbusconfig_DATA = \
	data/openrc-settingsd.conf \
	$(NULL)

polkitactiondir = @polkitactiondir@
dist_polkitaction_DATA = \
	data/org.freedesktop.hostname1.policy \
	data/org.freedesktop.locale1.policy \
	data/org.freedesktop.timedate1.policy \
	$(NULL)

initddir = $(sysconfdir)/init.d
initd_SCRIPTS = data/init.d/openrc-settingsd

confddir = $(sysconfdir)/conf.d
dist_confd_DATA = data/conf.d/openrc-settingsd

pidfile = @pidfile@
do_subst = sed -e 's,[@]libexecdir[@],$(libexecdir),g' \
	-e 's,[@]pidfile[@],$(pidfile),g' \
	$(NULL)

data/init.d/openrc-settingsd : data/init.d/openrc-settingsd.in
	$(do_subst) < $(srcdir)/data/init.d/openrc-settingsd.in > $(top_srcdir)/data/init.d/openrc-settingsd

%.service : %.service.in
	$(do_subst) < $(srcdir)/$< > $(top_srcdir)/$@

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPIDFILE=\""$(pidfile)"\" \
	$(OPENRC_SETTINGSD_CFLAGS) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(NULL)

LDADD = $(OPENRC_SETTINGSD_LIBS)

libexec_PROGRAMS = openrc-settingsd

hostnamed_built_sources = \
	src/hostname1-generated.c \
	src/hostname1-generated.h \
	$(NULL)

localed_built_sources = \
	src/locale1-generated.c \
	src/locale1-generated.h \
	$(NULL)

timedated_built_sources = \
	src/timedate1-generated.c \
	src/timedate1-generated.h \
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
	src/hostnamed.c \
	src/hostnamed.h \
	src/localed.c \
	src/localed.h \
	src/timedated.c \
	src/timedated.h \
	src/utils.c \
	src/utils.h \
	src/main.h \
	src/main.c \
	$(NULL)

nodist_openrc_settingsd_SOURCES = \
	$(hostnamed_built_sources) \
	$(localed_built_sources) \
	$(timedated_built_sources) \
	$(NULL)

$(hostnamed_built_sources) : data/org.freedesktop.hostname1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdHostnamed \
	--generate-c-code hostname1-generated \
	$(srcdir)/data/org.freedesktop.hostname1.xml; \
	mv hostname1-generated.{c,h} $(top_srcdir)/src/ )

$(localed_built_sources) : data/org.freedesktop.locale1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdLocaled \
	--generate-c-code locale1-generated \
	$(srcdir)/data/org.freedesktop.locale1.xml; \
	mv locale1-generated.{c,h} $(top_srcdir)/src/ )

$(timedated_built_sources) : data/org.freedesktop.timedate1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdTimedated \
	--generate-c-code timedate1-generated \
	$(srcdir)/data/org.freedesktop.timedate1.xml; \
	mv timedate1-generated.{c,h} $(top_srcdir)/src/ )

BUILT_SOURCES = \
	$(hostnamed_built_sources) \
	$(localed_built_sources) \
	$(timedated_built_sources) \
	$(NULL)

CLEANFILES = \
	$(hostnamed_built_sources) \
	$(localed_built_sources) \
	$(timedated_built_sources) \
	data/init.d/openrc-settingsd \
	$(dbusservices_DATA) \
	$(NULL)
