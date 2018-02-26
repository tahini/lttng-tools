/*
 * Copyright (C) 2018 - Geneviève Bastien <gbastien@versatic.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

//#define _LGPL_SOURCE
#include <assert.h>

#include <common/common.h>
#include <common/utils.h>
#include <libudev.h>

#include "udev-monitor.h"
#include "lttng-collectd-utils.h"

/*
 * We need to define TRACEPOINT_DEFINE in one C file in the program
 * before including provider headers.
 */
#define TRACEPOINT_DEFINE
#define TRACEPOINT_CREATE_PROBES
#include "udev-monitor-provider.h"

#define KVM_NODE "/dev/kvm"
#define KVM_EVENT_CREATE "create"
#define KVM_EVENT_DESTROY "destroy"
#define KVM_UUID_PARAM "-uuid"
#define QEMU_KVM_UUID_LEN	37

int ust_thread_kvm_create(int pid) {
	char uuid[QEMU_KVM_UUID_LEN];
	uuid[0] = '\0';

	lttng_get_command_line_argument(pid, KVM_UUID_PARAM, uuid);

	DBG("[udev-monitor-thread] kvm created %d: %s", pid, uuid);
	tracepoint(udev_monitor, kvm_created, pid, uuid);

	return 0;
}

int ust_thread_kvm_event(struct udev_device * dev) {
	struct udev_list_entry *list, *current;
	const char *event, *pidstr;
	int pid = -1;

	if (!tracepoint_enabled(udev_monitor, kvm_created) &&
			!tracepoint_enabled(udev_monitor, kvm_destroyed)) {
		// No need to do anything
		DBG3("[udev-monitor-thread] kvm monitoring tracepoint disabled");
	    return 0;
	}

	list = udev_device_get_properties_list_entry(dev);
	current = udev_list_entry_get_by_name(list, "EVENT");
	if (!current) {
		return 0;
	}
	event = udev_list_entry_get_value(current);
	current = udev_list_entry_get_by_name(list, "PID");
	if (!current) {
		return 0;
	}
	pidstr = udev_list_entry_get_value(current);
	pid = strtoul(pidstr, NULL, 0);
	if (!strcmp(KVM_EVENT_CREATE, event)) {
		ust_thread_kvm_create(pid);
	} else if (!strcmp(KVM_EVENT_DESTROY, event)) {
		DBG("[udev-monitor-thread] kvm destroyed %d", pid);
		tracepoint(udev_monitor, kvm_destroyed, pid);
	}

	return 0;
}

/*
 * This thread manage application notify communication.
 */
void *collectd_thread_monitor_udev(void *data)
{
	int ret, err = -1;
	struct udev *udev;
	struct udev_device *dev;
	struct udev_monitor *mon;
	int fd;
	const char *node;

	WARN("[udev-monitor-thread] Monitoring udev events ");


//	if (testpoint(sessiond_thread_app_manage_notify)) {
//		goto error_testpoint;
//	}

	/* Create the udev object */
	udev = udev_new();
	if (!udev) {
		WARN("[udev-monitor-thread] Can't create udev\n");
		goto error;
	}

	/* Set up a monitor to monitor the misc devices */
	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "misc", NULL);
	udev_monitor_enable_receiving(mon);
	/* Get the file descriptor (fd) for the monitor.
	   This fd will get passed to select() */
	fd = udev_monitor_get_fd(mon);

	/* This section will run continuously, calling usleep() at
	   the end of each pass. This is to demonstrate how to use
	   a udev_monitor in a non-blocking way. */
	while (1) {
		/* Set up the call to select(). In this case, select() will
		   only operate on a single file descriptor, the one
		   associated with our udev_monitor. Note that the timeval
		   object is set to 0, which will cause select() to not
		   block. */
		fd_set fds;
		struct timeval tv;

		// TODO See if thread needs to be killed

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;

		ret = select(fd+1, &fds, NULL, NULL, &tv);

		/* Check if our file descriptor has received data. */
		if (ret > 0 && FD_ISSET(fd, &fds)) {
			WARN("[udev-monitor-thread] Received events from udev");
			/* Make the call to receive the device.
			   select() ensured that this will not block. */
			dev = udev_monitor_receive_device(mon);
			if (dev) {
				node = udev_device_get_devnode(dev);
				if (!strcmp(KVM_NODE, node)) {
					WARN("[udev-monitor-thread] Received events from udev for kvm node");
					ust_thread_kvm_event(dev);
				}

				udev_device_unref(dev);
			}
		}
		usleep(250*1000);
	}

error:

//	utils_close_pipe(apps_cmd_notify_pipe);
//	apps_cmd_notify_pipe[0] = apps_cmd_notify_pipe[1] = -1;
	DBG("[udev-monitor-thread] Udev monitoring thread cleanup complete");
	if (err) {
		ERR("[udev-monitor-thread] Health error occurred in %s", __func__);
	}
	return NULL;
}
