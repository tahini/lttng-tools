/*
 * Copyright (C) 2018 Michael Jeanson <mjeanson@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <fcntl.h>

#include <common/defaults.h>
#include <common/common.h>
#include <common/utils.h>
#include <common/compat/poll.h>

#include "lttng-collectd.h"
#include "lttng-collectd-statedump.h"
#include "udev-monitor.h"

int lttng_opt_quiet, lttng_opt_verbose, lttng_opt_mi;

static pthread_t udev_thread;

/*
 * main
 */
int main(int argc, char **argv)
{
	int ret = 0, retval = 0;
	int i, pollfd, fd_w, out;
	uint32_t revents, nb_fd;
	struct lttng_poll_event events;
	char *pipe_path = argv[1];
	void *status;
	const unsigned char magic = DEFAULT_LTTNG_COLLECTD_MAGIC;

	fd_w = open(pipe_path, O_WRONLY);
	if (fd_w < 0) {
		PERROR("open fifo");
		retval = 1;
		goto exit_open;
	}

	 out = open("/tmp/cout.log", O_RDWR|O_CREAT|O_APPEND, 0600);
	 if (-1 == out) {
		 PERROR("opening cout.log");
	 }
	 if (-1 == dup2(out, fileno(stdout))) { PERROR("cannot redirect stdout"); }
	 if (-1 == dup2(out, fileno(stderr))) { PERROR("cannot redirect stderr"); }

	register_lttng_collectd_statedump_notifier();
	// TODO: Setup instrumentation here

	WARN("[lttng-collectd] will create the thread");
	/* Create thread to monitor udev */
	ret = pthread_create(&udev_thread, default_pthread_attr(),
			collectd_thread_monitor_udev,
			NULL);
	WARN("[lttng-collectd] created thread %d", ret);
	if (ret) {
		errno = ret;
		PERROR("pthread_create");
		retval = -1;
		goto exit_udev;
	}

	/*
	 * Write the magic byte on the fifo to signal we are ready
	 */
	ret = write(fd_w, &magic, 1);
	if (ret != 1) {
		PERROR("write fifo");
		retval = 1;
		goto exit;
	}

	ret = lttng_poll_create(&events, 1, LTTNG_CLOEXEC);
	if (ret < 0) {
		retval = 1;
		goto exit;
	}

	ret = lttng_poll_add(&events, fd_w, LPOLLIN);
	if (ret < 0) {
		retval = 1;
		goto exit;
	}

restart:
	for (;;) {
		ret = lttng_poll_wait(&events, -1);
		if (ret < 0) {
			/*
			 * Restart interrupted system call.
			 */
			if (errno == EINTR) {
				goto restart;
			}
			PERROR("poll fifo");
			retval = 1;
			goto exit;
		}

		nb_fd = ret;

		for (i = 0; i < nb_fd; i++) {
			/* Fetch once the poll data */
			revents = LTTNG_POLL_GETEV(&events, i);
			pollfd = LTTNG_POLL_GETFD(&events, i);

			if (!revents) {
				/* No activity for this FD (poll implementation). */
				continue;
			}

			/* Event on the collectd named pipe */
			if (pollfd == fd_w) {
				if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
					ERR("Parent disconnected");
					retval = 1;
					goto exit;
				}
			}
		}
	}

exit:
exit_udev:

	ret = pthread_join(udev_thread, &status);
	if (ret) {
		errno = ret;
		PERROR("pthread_join health_thread");
		retval = -1;
	}
	unregister_lttng_collectd_statedump_notifier();
exit_open:
	return retval;
}
