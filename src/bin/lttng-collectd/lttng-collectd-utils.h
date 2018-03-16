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

#ifndef LTTNG_COLLECTD_UTILS_H
#define LTTNG_COLLECTD_UTILS_H

int lttng_get_command_line_argument(int pid, char *argument, char *argval);

#endif /* LTTNG_COLLECTD_UTILS_H */
