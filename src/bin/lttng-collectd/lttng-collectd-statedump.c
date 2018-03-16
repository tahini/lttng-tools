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

#include <common/common.h>
#include <common/utils.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <regex.h>
#include <lttng/statedump-notifier.h>
#include "lttng-collectd-statedump.h"
#include "lttng-collectd-utils.h"

#define TRACEPOINT_DEFINE
#define TRACEPOINT_CREATE_PROBES
#define TP_SESSION_CHECK
#include "lttng-collectd-statedump-provider.h"

static struct lttng_ust_notifier notifier;

#define DEBUGFS_KVM "/sys/kernel/debug/kvm"
#define KVM_UUID_PARAM "-uuid"
#define QEMU_KVM_UUID_LEN	37

static void lttng_collectd_statedump_kvm(struct lttng_session *session, void *priv)
{
	DBG2("[collectd-statedump] Statedumping the KVM guests");

	char uuid[QEMU_KVM_UUID_LEN];
	char pidstr[6];
	regex_t regex;
	char* pos;
	int pid;
	uuid[0] = '\0';

	// FIXME: any lock to take? Or has the caller done everything necessary?

	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir (DEBUGFS_KVM)) == NULL) {
		if (errno == EACCES)
			DBG("[collectd-statedump] Cannot open debugfs to statedump the KVM guests: Is the daemon running as root?");
		else
			perror("[collectd-statedump] Error opening debugfs directory: ");
		return;
	}

	if (regcomp(&regex, "^[[:digit:]+-].*", REG_EXTENDED)) {
		WARN("[collectd-statedump] Couldn't compile regular expression");
		return;
	}

	/* print all the files and directories within directory */
	while ((ent = readdir (dir)) != NULL) {
		if (regexec(&regex, ent->d_name, 0, NULL, 0) ){
			continue;
		}

		if (NULL == (pos = strstr(ent->d_name, "-"))) {
			DBG3("'-' String not found in %s!! Strange, it passed the regex", ent->d_name);
			continue;
		}
		strncpy(pidstr, ent->d_name, pos - &ent->d_name[0]);
		pid = atoi(pidstr);
		lttng_get_command_line_argument(pid, KVM_UUID_PARAM, uuid);
		DBG3("[collectd-statedump] Found a kvm guest for pid %d: %s", pid, uuid);
		tracepoint(lttng_collectd_statedump, kvm_guest, session, pid, uuid);

	}

	closedir (dir);
}

static void lttng_collectd_do_statedump(struct lttng_session *session, void *priv)
{
	DBG2("[collectd-statedump] Doing collectd-specific statedump");
	lttng_collectd_statedump_kvm(session, priv);
}

void *register_lttng_collectd_statedump_notifier() {
	DBG2("[collectd-statedump] Registering statedump notifier");
	lttng_ust_init_statedump_notifier(&notifier, lttng_collectd_do_statedump, NULL);
	lttng_ust_register_statedump_notifier(&notifier);

	return NULL;
}

void *unregister_lttng_collectd_statedump_notifier() {
	DBG2("[collectd-statedump] Un-registering statedump notifier");
	lttng_ust_unregister_statedump_notifier(&notifier);

	return NULL;
}
