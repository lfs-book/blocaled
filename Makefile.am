ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	data/hostname1.xml \
	$(NULL)

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" \
	$(OPENRC_SETTINGSD_CFLAGS) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(NULL)

LDADD = $(OPENRC_SETTINGSD_LIBS)

libexec_PROGRAMS = openrc-settingsd

hostnamed_built_sources = \
	src/hostnamed-generated.c \
	src/hostnamed-generated.h \
	$(NULL)

openrc_settingsd_SOURCES = \
	$(hostnamed_built_sources) \
	src/hostnamed.c \
	src/hostnamed.h \
	src/bus-utils.c \
	src/bus-utils.h \
	src/shell-utils.c \
	src/shell-utils.h \
	src/main.c \
	$(NULL)

$(hostnamed_built_sources) : data/hostname1.xml
	( $(GDBUS_CODEGEN) \
	--interface-prefix org.freedesktop. \
	--c-namespace OpenrcSettingsdHostnamed \
	--generate-c-code hostnamed-generated \
	$(srcdir)/data/hostname1.xml; \
	mv hostnamed-generated.{c,h} $(top_srcdir)/src/ )

BUILT_SOURCES = $(hostnamed_built_sources)
CLEANFILES = $(hostnamed_built_sources)
