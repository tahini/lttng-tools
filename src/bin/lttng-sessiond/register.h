/*
 * Copyright (C) 2011 David Goulet <david.goulet@polymtl.ca>
 * Copyright (C) 2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright (C) 2013 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef SESSIOND_APPLICATION_REGISTRATION_THREAD_H
#define SESSIOND_APPLICATION_REGISTRATION_THREAD_H

#include <stdbool.h>
#include <semaphore.h>
#include "lttng-sessiond.h"

struct registration_thread_handle {
	/*
	 * To inform the collectd thread we are ready.
	 */
	sem_t registration_thread_ready;
};

/* notification_thread_data takes ownership of the channel monitor pipes. */
struct registration_thread_handle *registration_thread_handle_create(void);
void registration_thread_handle_destroy(
		struct registration_thread_handle *handle);
struct lttng_thread *launch_application_registration_thread(
		struct ust_cmd_queue *cmd_queue,
		struct registration_thread_handle *handle);

#endif /* SESSIOND_APPLICATION_REGISTRATION_THREAD_H */
