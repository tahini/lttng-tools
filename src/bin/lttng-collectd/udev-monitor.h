/*
 *  Copyright (C) 2018 - Geneviève Bastien <gbastien@versatic.net>
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

#ifndef UDEV_MONITOR_H
#define UDEV_MONITOR_H

#if HAVE_LIBUDEV

void *collectd_thread_monitor_udev(void *data);

#else

static inline
void *collectd_thread_monitor_udev(void *data)
{
	WARN("[collectd-udev] Not monitoring udev as libudev is not available on the system");
	return NULL;
}

#endif /* HAVE_LIBUDEV */

#endif /* UDEV_MONITOR_H */
