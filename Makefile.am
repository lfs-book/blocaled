ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	data/hostname1.xml \
	data/locale1.xml \
	data/timedate1.xml \
	$(NULL)

pkgdata_DATA = \
	data/kbd-model-map \
	$(NULL)

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
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
	$(hostnamed_built_sources) \
	$(localed_built_sources) \
	$(timedated_built_sources) \
	$(copypaste_sources) \
	src/hostnamed.c \
	src/hostnamed.h \
	src/localed.c \
	src/localed.h \
	src/timedated.c \
	src/timedated.h \
	src/utils.c \
	src/utils.h \
	src/main.c \
	$(NULL)

$(hostnamed_built_sources) : data/hostname1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdHostnamed \
	--generate-c-code hostname1-generated \
	$(srcdir)/data/hostname1.xml; \
	mv hostname1-generated.{c,h} $(top_srcdir)/src/ )

$(localed_built_sources) : data/locale1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdLocaled \
	--generate-c-code locale1-generated \
	$(srcdir)/data/locale1.xml; \
	mv locale1-generated.{c,h} $(top_srcdir)/src/ )

$(timedated_built_sources) : data/timedate1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdTimedated \
	--generate-c-code timedate1-generated \
	$(srcdir)/data/timedate1.xml; \
	mv timedate1-generated.{c,h} $(top_srcdir)/src/ )

BUILT_SOURCES = $(hostnamed_built_sources) $(localed_built_sources) $(timedated_built_sources)
CLEANFILES = $(hostnamed_built_sources) $(localed_built_sources) $(timedated_built_sources)
