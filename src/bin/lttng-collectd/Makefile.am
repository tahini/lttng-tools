if HAVE_LIBLTTNG_UST_CTL
AM_CPPFLAGS +=-I./

lttnglibexec_PROGRAMS = lttng-collectd

lttng_collectd_SOURCES = lttng-collectd.c \
	lttng-collectd.h \
	lttng-collectd-utils.h lttng-collectd-utils.c \
	lttng-collectd-statedump-provider.h \
  lttng-collectd-statedump.h lttng-collectd-statedump.c

lttng_collectd_LDADD = \
	$(top_builddir)/src/common/compat/libcompat.la \
	$(top_builddir)/src/common/libcommon.la

lttng_collectd_LDADD += -llttng-ust -ldl
endif
