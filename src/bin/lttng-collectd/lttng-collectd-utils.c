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

#define _LGPL_SOURCE
#include <assert.h>

#include <common/common.h>
#include <common/utils.h>

//#include "fd-limit.h"
#include "lttng-collectd-utils.h"

int lttng_get_command_line_argument(int pid, char *argument, char *argval)
{
	FILE *file;
	char *line = NULL;
	size_t len = 0;
	char cmdline_file[64];

	argval[0] = '\0';
	if (pid > 0) {
		sprintf(cmdline_file, "/proc/%d/cmdline", pid);
		file = fopen(cmdline_file, "r");
		if (file) {
		    while (getdelim(&line, &len, '\0', file) != -1) {
		    	if (!strcmp(argument, line)) {
		    		// Read the next value in uuid
		    		if (getdelim(&line, &len, '\0', file) != -1) {
		    			strcpy(argval, line);
		    			goto close_file;
		    		}
		    	}
		    }
		    free(line);
		    fclose(file);
		}
	}
	return 1;

close_file:
	free(line);
	fclose(file);
	return 0;
}
