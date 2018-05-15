/*
 * Copyright (C) 2018 Michael Jeanson <mjeanson@efficios.com>
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

#ifndef COLLECTD_THREAD_H
#define COLLECTD_THREAD_H

#include "thread.h"
#include "register.h"

#ifdef HAVE_LIBLTTNG_UST_CTL

bool launch_collectd_thread(struct registration_thread_handle *registration_thread_handle);

#else /* HAVE_LIBLTTNG_UST_CTL */

static inline
bool launch_collectd_thread(struct registration_thread_handle *registration_thread_handle)
{
	return true;
}

#endif /* HAVE_LIBLTTNG_UST_CTL */

#endif /* COLLECTD_THREAD_H */
