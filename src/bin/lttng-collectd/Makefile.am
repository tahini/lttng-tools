if HAVE_LIBLTTNG_UST_CTL
AM_CPPFLAGS +=-I./

lttnglibexec_PROGRAMS = lttng-collectd

lttng_collectd_SOURCES = lttng-collectd-utils.h lttng-collectd-utils.c \
	lttng-collectd-statedump-provider.h \
	lttng-collectd-statedump.h lttng-collectd-statedump.c \
	udev-monitor.h

if HAVE_LIBUDEV
lttng_collectd_SOURCES += udev-monitor-provider.h udev-monitor.c
endif

# Add main collectd file at the dn for compile order
lttng_collectd_SOURCES += lttng-collectd.c lttng-collectd.h

lttng_collectd_LDADD = \
	$(top_builddir)/src/common/compat/libcompat.la \
	$(top_builddir)/src/common/libcommon.la
	$(UDEV_LIBS)

lttng_collectd_LDADD += -llttng-ust -ldl $(KMOD_LIBS) $(UDEV_LIBS)
endif
