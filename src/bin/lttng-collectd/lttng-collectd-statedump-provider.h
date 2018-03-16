#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER lttng_collectd_statedump

#if !defined(_TRACEPOINT_LTTNG_COLLECTD_STATEDUMP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TRACEPOINT_LTTNG_COLLECTD_STATEDUMP_H

#ifdef __cplusplus
extern "C" {
#endif

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

#include <stdint.h>
#include <unistd.h>
#include <lttng/ust-events.h>
#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(lttng_collectd_statedump, kvm_guest,
	TP_ARGS(struct lttng_session *, session,
		int, pid,
		char *, guest_uuid),
	TP_FIELDS(
		ctf_integer(int64_t, pid, pid)
		ctf_string(guest_uuid, guest_uuid)
	)
)

#endif /* _TRACEPOINT_LTTNG_COLLECTD_STATEDUMP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./lttng-collectd-statedump-provider.h"

/* This part must be outside ifdef protection */
#include <lttng/tracepoint-event.h>

#ifdef __cplusplus
}
#endif
