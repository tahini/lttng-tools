/*
 * Copyright (C) 2018 Geneviève Bastien <gbastien@versatic.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * udev_monitor lttng-ust tracepoint provider.
 */

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER udev_monitor

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./udev-monitor-provider.h"

#if !defined(_UDEV_MONITOR_PROVIDER_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _UDEV_MONITOR_PROVIDER_H

#include <lttng/tracepoint.h>

/*
 * KVM creation tracepoint. Adds the pid of the newly created machine
 * and its uuid to match it with an 'env'.'uuid' metadata in the guest
 * trace
 */
TRACEPOINT_EVENT(
	udev_monitor,
  kvm_created,
  TP_ARGS(unsigned int, pid, const char *, uuid),
  TP_FIELDS(
  	ctf_integer(unsigned int, pid, pid)
	  ctf_string(uuid, uuid)
  )
)

/*
 * KVM destruction tracepoint. Adds the pid of the destroyed machine
 */
TRACEPOINT_EVENT(
	udev_monitor,
  kvm_destroyed,
	TP_ARGS(unsigned int, pid),
	TP_FIELDS(
		ctf_integer(unsigned int, pid, pid)
  )
)

#endif /* _UDEV_MONITOR_PROVIDER_H */

#include <lttng/tracepoint-event.h>
