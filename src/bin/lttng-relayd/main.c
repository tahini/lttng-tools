/*
 * Copyright (C) 2012 - Julien Desfossez <jdesfossez@efficios.com>
 *                      David Goulet <dgoulet@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <urcu/futex.h>
#include <urcu/uatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <config.h>

#include <lttng/lttng.h>
#include <common/common.h>
#include <common/compat/poll.h>
#include <common/compat/socket.h>
#include <common/defaults.h>
#include <common/futex.h>
#include <common/sessiond-comm/sessiond-comm.h>
#include <common/sessiond-comm/inet.h>
#include <common/hashtable/hashtable.h>
#include <common/sessiond-comm/relayd.h>
#include <common/uri.h>

#include "lttng-relayd.h"

/* command line options */
static int opt_daemon;
static char *opt_output_path;
static struct lttng_uri *control_uri = NULL;
static struct lttng_uri *data_uri = NULL;

const char *progname;
static int is_root;			/* Set to 1 if the daemon is running as root */

/*
 * Quit pipe for all threads. This permits a single cancellation point
 * for all threads when receiving an event on the pipe.
 */
static int thread_quit_pipe[2] = { -1, -1 };

/*
 * This pipe is used to inform the worker thread that a command is queued and
 * ready to be processed.
 */
static int relay_cmd_pipe[2] = { -1, -1 };

static int dispatch_thread_exit;

static pthread_t listener_thread;
static pthread_t dispatcher_thread;
static pthread_t worker_thread;

static uint64_t last_relay_stream_id = 0;
static uint64_t last_relay_session_id = 0;

/*
 * Relay command queue.
 *
 * The relay_thread_listener and relay_thread_dispatcher communicate with this
 * queue.
 */
static struct relay_cmd_queue relay_cmd_queue;

/* buffer allocated at startup, used to store the trace data */
static char *data_buffer = NULL;
static unsigned int data_buffer_size = 0;

/*
 * usage function on stderr
 */
static
void usage(void)
{
	fprintf(stderr, "Usage: %s OPTIONS\n\nOptions:\n", progname);
	fprintf(stderr, "  -h, --help                         Display this usage.\n");
	fprintf(stderr, "  -d, --daemonize                    Start as a daemon.\n");
	fprintf(stderr, "  -C, --control-port                 Control port listening (URI)\n");
	fprintf(stderr, "  -D, --data-port                    Data port listening (URI)\n");
	fprintf(stderr, "  -o, --output                       Output path for traces (PATH)\n");
	fprintf(stderr, "  -v, --verbose                      Verbose mode. Activate DBG() macro.\n");
}

static
int parse_args(int argc, char **argv)
{
	int c;
	int ret = 0;
	char *default_address;

	static struct option long_options[] = {
		{ "control-port", 1, 0, 'C' },
		{ "data-port", 1, 0, 'D' },
		{ "daemonize", 0, 0, 'd' },
		{ "help", 0, 0, 'h' },
		{ "output", 1, 0, 'o' },
		{ "verbose", 0, 0, 'v' },
		{ NULL, 0, 0, 0 }
	};

	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "dhv" "C:D:o:",
				long_options, &option_index);
		if (c == -1) {
			break;
		}

		switch (c) {
		case 0:
			fprintf(stderr, "option %s", long_options[option_index].name);
			if (optarg) {
				fprintf(stderr, " with arg %s\n", optarg);
			}
			break;
		case 'C':
			ret = uri_parse(optarg, &control_uri);
			if (ret < 0) {
				ERR("Invalid control URI specified");
				goto exit;
			}
			if (control_uri->port == 0) {
				control_uri->port = DEFAULT_NETWORK_CONTROL_PORT;
			}
			break;
		case 'D':
			ret = uri_parse(optarg, &data_uri);
			if (ret < 0) {
				ERR("Invalid data URI specified");
				goto exit;
			}
			if (data_uri->port == 0) {
				data_uri->port = DEFAULT_NETWORK_DATA_PORT;
			}
			break;
		case 'd':
			opt_daemon = 1;
			break;
		case 'h':
			usage();
			exit(EXIT_FAILURE);
		case 'o':
			ret = asprintf(&opt_output_path, "%s", optarg);
			if (ret < 0) {
				PERROR("asprintf opt_output_path");
				goto exit;
			}
			break;
		case 'v':
			/* Verbose level can increase using multiple -v */
			lttng_opt_verbose += 1;
			break;
		default:
			/* Unknown option or other error.
			 * Error is printed by getopt, just return */
			ret = -1;
			goto exit;
		}
	}

	/* assign default values */
	if (control_uri == NULL) {
		ret = asprintf(&default_address, "tcp://0.0.0.0:%d",
				DEFAULT_NETWORK_CONTROL_PORT);
		if (ret < 0) {
			PERROR("asprintf default data address");
			goto exit;
		}

		ret = uri_parse(default_address, &control_uri);
		free(default_address);
		if (ret < 0) {
			ERR("Invalid control URI specified");
			goto exit;
		}
	}
	if (data_uri == NULL) {
		ret = asprintf(&default_address, "tcp://0.0.0.0:%d",
				DEFAULT_NETWORK_DATA_PORT);
		if (ret < 0) {
			PERROR("asprintf default data address");
			goto exit;
		}

		ret = uri_parse(default_address, &data_uri);
		free(default_address);
		if (ret < 0) {
			ERR("Invalid data URI specified");
			goto exit;
		}
	}

exit:
	return ret;
}

/*
 * Cleanup the daemon
 */
static
void cleanup(void)
{
	int i, ret;

	DBG("Cleaning up");

	for (i = 0; i < 2; i++) {
		if (thread_quit_pipe[i] >= 0) {
			ret = close(thread_quit_pipe[i]);
			if (ret) {
				PERROR("close");
			}
		}
	}
}

/*
 * Write to writable pipe used to notify a thread.
 */
static
int notify_thread_pipe(int wpipe)
{
	int ret;

	ret = write(wpipe, "!", 1);
	if (ret < 0) {
		PERROR("write poll pipe");
	}

	return ret;
}

/*
 * Stop all threads by closing the thread quit pipe.
 */
static
void stop_threads(void)
{
	int ret;

	/* Stopping all threads */
	DBG("Terminating all threads");
	ret = notify_thread_pipe(thread_quit_pipe[1]);
	if (ret < 0) {
		ERR("write error on thread quit pipe");
	}

	/* Dispatch thread */
	dispatch_thread_exit = 1;
	futex_nto1_wake(&relay_cmd_queue.futex);
}

/*
 * Signal handler for the daemon
 *
 * Simply stop all worker threads, leaving main() return gracefully after
 * joining all threads and calling cleanup().
 */
static
void sighandler(int sig)
{
	switch (sig) {
	case SIGPIPE:
		DBG("SIGPIPE caught");
		return;
	case SIGINT:
		DBG("SIGINT caught");
		stop_threads();
		break;
	case SIGTERM:
		DBG("SIGTERM caught");
		stop_threads();
		break;
	default:
		break;
	}
}

/*
 * Setup signal handler for :
 *		SIGINT, SIGTERM, SIGPIPE
 */
static
int set_signal_handler(void)
{
	int ret = 0;
	struct sigaction sa;
	sigset_t sigset;

	if ((ret = sigemptyset(&sigset)) < 0) {
		PERROR("sigemptyset");
		return ret;
	}

	sa.sa_handler = sighandler;
	sa.sa_mask = sigset;
	sa.sa_flags = 0;
	if ((ret = sigaction(SIGTERM, &sa, NULL)) < 0) {
		PERROR("sigaction");
		return ret;
	}

	if ((ret = sigaction(SIGINT, &sa, NULL)) < 0) {
		PERROR("sigaction");
		return ret;
	}

	if ((ret = sigaction(SIGPIPE, &sa, NULL)) < 0) {
		PERROR("sigaction");
		return ret;
	}

	DBG("Signal handler set for SIGTERM, SIGPIPE and SIGINT");

	return ret;
}

/*
 * Init thread quit pipe.
 *
 * Return -1 on error or 0 if all pipes are created.
 */
static
int init_thread_quit_pipe(void)
{
	int ret, i;

	ret = pipe(thread_quit_pipe);
	if (ret < 0) {
		PERROR("thread quit pipe");
		goto error;
	}

	for (i = 0; i < 2; i++) {
		ret = fcntl(thread_quit_pipe[i], F_SETFD, FD_CLOEXEC);
		if (ret < 0) {
			PERROR("fcntl");
			goto error;
		}
	}

error:
	return ret;
}

/*
 * Create a poll set with O_CLOEXEC and add the thread quit pipe to the set.
 */
static
int create_thread_poll_set(struct lttng_poll_event *events, int size)
{
	int ret;

	if (events == NULL || size == 0) {
		ret = -1;
		goto error;
	}

	ret = lttng_poll_create(events, size, LTTNG_CLOEXEC);
	if (ret < 0) {
		goto error;
	}

	/* Add quit pipe */
	ret = lttng_poll_add(events, thread_quit_pipe[0], LPOLLIN);
	if (ret < 0) {
		goto error;
	}

	return 0;

error:
	return ret;
}

/*
 * Check if the thread quit pipe was triggered.
 *
 * Return 1 if it was triggered else 0;
 */
static
int check_thread_quit_pipe(int fd, uint32_t events)
{
	if (fd == thread_quit_pipe[0] && (events & LPOLLIN)) {
		return 1;
	}

	return 0;
}

/*
 * Create and init socket from uri.
 */
static
struct lttcomm_sock *relay_init_sock(struct lttng_uri *uri)
{
	int ret;
	struct lttcomm_sock *sock = NULL;

	sock = lttcomm_alloc_sock_from_uri(uri);
	if (sock == NULL) {
		ERR("Allocating socket");
		goto error;
	}

	ret = lttcomm_create_sock(sock);
	if (ret < 0) {
		goto error;
	}
	DBG("Listening on sock %d", sock->fd);

	ret = sock->ops->bind(sock);
	if (ret < 0) {
		goto error;
	}

	ret = sock->ops->listen(sock, -1);
	if (ret < 0) {
		goto error;

	}

	return sock;

error:
	if (sock) {
		lttcomm_destroy_sock(sock);
	}
	return NULL;
}

/*
 * This thread manages the listening for new connections on the network
 */
static
void *relay_thread_listener(void *data)
{
	int i, ret, pollfd;
	int val = 1;
	uint32_t revents, nb_fd;
	struct lttng_poll_event events;
	struct lttcomm_sock *control_sock, *data_sock;

	/*
	 * Get allocated in this thread, enqueued to a global queue, dequeued and
	 * freed in the worker thread.
	 */
	struct relay_command *relay_cmd = NULL;

	DBG("[thread] Relay listener started");

	control_sock = relay_init_sock(control_uri);
	if (!control_sock) {
		goto error_sock;
	}

	data_sock = relay_init_sock(data_uri);
	if (!data_sock) {
		goto error_sock;
	}

	/*
	 * Pass 3 as size here for the thread quit pipe, control and data socket.
	 */
	ret = create_thread_poll_set(&events, 3);
	if (ret < 0) {
		goto error_create_poll;
	}

	/* Add the control socket */
	ret = lttng_poll_add(&events, control_sock->fd, LPOLLIN | LPOLLRDHUP);
	if (ret < 0) {
		goto error_poll_add;
	}

	/* Add the data socket */
	ret = lttng_poll_add(&events, data_sock->fd, LPOLLIN | LPOLLRDHUP);
	if (ret < 0) {
		goto error_poll_add;
	}

	while (1) {
		DBG("Listener accepting connections");

		nb_fd = LTTNG_POLL_GETNB(&events);

restart:
		ret = lttng_poll_wait(&events, -1);
		if (ret < 0) {
			/*
			 * Restart interrupted system call.
			 */
			if (errno == EINTR) {
				goto restart;
			}
			goto error;
		}

		DBG("Relay new connection received");
		for (i = 0; i < nb_fd; i++) {
			/* Fetch once the poll data */
			revents = LTTNG_POLL_GETEV(&events, i);
			pollfd = LTTNG_POLL_GETFD(&events, i);

			/* Thread quit pipe has been closed. Killing thread. */
			ret = check_thread_quit_pipe(pollfd, revents);
			if (ret) {
				goto error;
			}

			if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
				ERR("socket poll error");
				goto error;
			} else if (revents & LPOLLIN) {
				struct lttcomm_sock *newsock = NULL;

				relay_cmd = zmalloc(sizeof(struct relay_command));
				if (relay_cmd == NULL) {
					PERROR("relay command zmalloc");
					goto error;
				}

				if (pollfd == data_sock->fd) {
					newsock = data_sock->ops->accept(data_sock);
					if (newsock < 0) {
						PERROR("accepting data sock");
						goto error;
					}
					relay_cmd->type = RELAY_DATA;
					DBG("Relay data connection accepted, socket %d", newsock->fd);
				} else if (pollfd == control_sock->fd) {
					newsock = control_sock->ops->accept(control_sock);
					if (newsock < 0) {
						PERROR("accepting control sock");
						goto error;
					}
					relay_cmd->type = RELAY_CONTROL;
					DBG("Relay control connection accepted, socket %d", newsock->fd);
				}
				ret = setsockopt(newsock->fd, SOL_SOCKET, SO_REUSEADDR,
						&val, sizeof(int));
				if (ret < 0) {
					PERROR("setsockopt inet");
					goto error;
				}
				relay_cmd->sock = newsock;
				/*
				 * Lock free enqueue the request.
				 */
				cds_wfq_enqueue(&relay_cmd_queue.queue, &relay_cmd->node);

				/*
				 * Wake the dispatch queue futex. Implicit memory
				 * barrier with the exchange in cds_wfq_enqueue.
				 */
				futex_nto1_wake(&relay_cmd_queue.futex);
			}
		}
	}

error:
error_poll_add:
	lttng_poll_clean(&events);
error_create_poll:
	if (control_sock->fd >= 0) {
		ret = control_sock->ops->close(control_sock);
		if (ret) {
			PERROR("close");
		}
		lttcomm_destroy_sock(control_sock);
	}
	if (data_sock->fd >= 0) {
		ret = data_sock->ops->close(data_sock);
		if (ret) {
			PERROR("close");
		}
		lttcomm_destroy_sock(data_sock);
	}

	DBG("Relay listener thread cleanup complete");
	stop_threads();
error_sock:
	return NULL;
}

/*
 * This thread manages the dispatching of the requests to worker threads
 */
static
void *relay_thread_dispatcher(void *data)
{
	int ret;
	struct cds_wfq_node *node;
	struct relay_command *relay_cmd = NULL;

	DBG("[thread] Relay dispatcher started");

	while (!dispatch_thread_exit) {
		/* Atomically prepare the queue futex */
		futex_nto1_prepare(&relay_cmd_queue.futex);

		do {
			/* Dequeue commands */
			node = cds_wfq_dequeue_blocking(&relay_cmd_queue.queue);
			if (node == NULL) {
				DBG("Woken up but nothing in the relay command queue");
				/* Continue thread execution */
				break;
			}

			relay_cmd = caa_container_of(node, struct relay_command, node);
			DBG("Dispatching request waiting on sock %d", relay_cmd->sock->fd);

			/*
			 * Inform worker thread of the new request. This
			 * call is blocking so we can be assured that the data will be read
			 * at some point in time or wait to the end of the world :)
			 */
			ret = write(relay_cmd_pipe[1], relay_cmd,
					sizeof(struct relay_command));
			free(relay_cmd);
			if (ret < 0) {
				PERROR("write cmd pipe");
				goto error;
			}
		} while (node != NULL);

		/* Futex wait on queue. Blocking call on futex() */
		futex_nto1_wait(&relay_cmd_queue.futex);
	}

error:
	DBG("Dispatch thread dying");
	stop_threads();
	return NULL;
}

/*
 * Return the realpath(3) of the path even if the last directory token does not
 * exist. For example, with /tmp/test1/test2, if test2/ does not exist but the
 * /tmp/test1 does, the real path is returned. In normal time, realpath(3)
 * fails if the end point directory does not exist.
 */
static
char *expand_full_path(const char *path)
{
	const char *end_path = path;
	char *next, *cut_path, *expanded_path;

	/* Find last token delimited by '/' */
	while ((next = strpbrk(end_path + 1, "/"))) {
		end_path = next;
	}

	/* Cut last token from original path */
	cut_path = strndup(path, end_path - path);

	expanded_path = malloc(PATH_MAX);
	if (expanded_path == NULL) {
		goto error;
	}

	expanded_path = realpath((char *)cut_path, expanded_path);
	if (expanded_path == NULL) {
		switch (errno) {
		case ENOENT:
			ERR("%s: No such file or directory", cut_path);
			break;
		default:
			PERROR("realpath");
			break;
		}
		goto error;
	}

	/* Add end part to expanded path */
	strcat(expanded_path, end_path);

	free(cut_path);
	return expanded_path;

error:
	free(cut_path);
	return NULL;
}


/*
 *  config_get_default_path
 *
 *  Returns the HOME directory path. Caller MUST NOT free(3) the return pointer.
 */
static
char *config_get_default_path(void)
{
	return getenv("HOME");
}

/*
 * Create recursively directory using the FULL path.
 */
static
int mkdir_recursive(char *path, mode_t mode)
{
	char *p, tmp[PATH_MAX];
	struct stat statbuf;
	size_t len;
	int ret;

	ret = snprintf(tmp, sizeof(tmp), "%s", path);
	if (ret < 0) {
		PERROR("snprintf mkdir");
		goto error;
	}

	len = ret;
	if (tmp[len - 1] == '/') {
		tmp[len - 1] = 0;
	}

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (tmp[strlen(tmp) - 1] == '.' &&
					tmp[strlen(tmp) - 2] == '.' &&
					tmp[strlen(tmp) - 3] == '/') {
				ERR("Using '/../' is not permitted in the trace path (%s)",
						tmp);
				ret = -1;
				goto error;
			}
			ret = stat(tmp, &statbuf);
			if (ret < 0) {
				ret = mkdir(tmp, mode);
				if (ret < 0) {
					if (!(errno == EEXIST)) {
						PERROR("mkdir recursive");
						ret = -errno;
						goto error;
					}
				}
			}
			*p = '/';
		}
	}

	ret = mkdir(tmp, mode);
	if (ret < 0) {
		if (!(errno == EEXIST)) {
			PERROR("mkdir recursive last piece");
			ret = -errno;
		} else {
			ret = 0;
		}
	}

error:
	return ret;
}

/*
 * create_output_path: create the output trace directory
 */
static
char *create_output_path(char *path_name)
{
	int ret = 0;
	char *alloc_path = NULL;
	char *traces_path = NULL;
	char *full_path = NULL;

	/* Auto output path */
	if (opt_output_path == NULL) {
		alloc_path = strdup(config_get_default_path());
		if (alloc_path == NULL) {
			ERR("Home path not found.\n \
					Please specify an output path using -o, --output PATH");
			ret = -1;
			goto exit;
		}

		ret = asprintf(&traces_path, "%s/" DEFAULT_TRACE_DIR_NAME
				"/%s", alloc_path, path_name);
		if (ret < 0) {
			PERROR("asprintf trace dir name");
			goto exit;
		}
	} else {
		full_path = expand_full_path(opt_output_path);
		ret = asprintf(&traces_path, "%s/%s", full_path, path_name);
		if (ret < 0) {
			PERROR("asprintf trace dir name");
			goto exit;
		}
	}
	free(alloc_path);
	free(full_path);

exit:
	return traces_path;
}

/*
 * relay_delete_session: Free all memory associated with a session and
 * close all the FDs
 */
static
void relay_delete_session(struct relay_command *cmd, struct lttng_ht *streams_ht)
{
	struct lttng_ht_iter iter;
	struct lttng_ht_node_ulong *node;
	struct relay_stream *stream;
	int ret;

	if (!cmd->session)
		return;

	DBG("Relay deleting session %lu", cmd->session->id);
	free(cmd->session->sock);

	cds_lfht_for_each_entry(streams_ht->ht, &iter.iter, node, node) {
		node = lttng_ht_iter_get_node_ulong(&iter);
		if (node) {
			stream = caa_container_of(node,
					struct relay_stream, stream_n);
			if (stream->session == cmd->session) {
				close(stream->fd);
				ret = lttng_ht_del(streams_ht, &iter);
				assert(!ret);
				free(stream);
			}
		}
	}
}

/*
 * relay_add_stream: allocate a new stream for a session
 */
static
int relay_add_stream(struct lttcomm_relayd_hdr *recv_hdr,
		struct relay_command *cmd, struct lttng_ht *streams_ht)
{
	struct relay_session *session = cmd->session;
	struct lttcomm_relayd_add_stream stream_info;
	struct relay_stream *stream = NULL;
	struct lttcomm_relayd_status_stream reply;
	char *path = NULL, *root_path = NULL;
	int ret, send_ret;

	if (!session || session->version_check_done == 0) {
		ERR("Trying to add a stream before version check");
		ret = -1;
		goto end_no_session;
	}

	/* FIXME : use data_size for something ? */
	ret = cmd->sock->ops->recvmsg(cmd->sock, &stream_info,
			sizeof(struct lttcomm_relayd_add_stream), MSG_WAITALL);
	if (ret < sizeof(struct lttcomm_relayd_add_stream)) {
		ERR("Relay didn't receive valid add_stream struct size : %d", ret);
		ret = -1;
		goto end_no_session;
	}
	stream = zmalloc(sizeof(struct relay_stream));
	if (stream == NULL) {
		PERROR("relay stream zmalloc");
		ret = -1;
		goto end_no_session;
	}

	stream->stream_handle = ++last_relay_stream_id;
	stream->seq = 0;
	stream->session = session;

	root_path = create_output_path(stream_info.pathname);
	if (!root_path) {
		ret = -1;
		goto end;
	}
	ret = mkdir_recursive(root_path, S_IRWXU | S_IRWXG);
	if (ret < 0) {
		free(root_path);
		ERR("relay creating output directory");
		goto end;
	}

	ret = asprintf(&path, "%s/%s", root_path, stream_info.channel_name);
	if (ret < 0) {
		PERROR("asprintf stream path");
		goto end;
	}

	ret = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
	if (ret < 0) {
		PERROR("Relay creating trace file");
		goto end;
	}

	stream->fd = ret;
	DBG("Tracefile %s created", path);

	lttng_ht_node_init_ulong(&stream->stream_n,
			(unsigned long) stream->stream_handle);
	lttng_ht_add_unique_ulong(streams_ht,
			&stream->stream_n);

	DBG("Relay new stream added %s", stream_info.channel_name);

end:
	free(path);
	free(root_path);
	/* send the session id to the client or a negative return code on error */
	if (ret < 0) {
		reply.ret_code = htobe32(LTTCOMM_ERR);
	} else {
		reply.ret_code = htobe32(LTTCOMM_OK);
	}
	reply.handle = htobe64(stream->stream_handle);
	send_ret = cmd->sock->ops->sendmsg(cmd->sock, &reply,
			sizeof(struct lttcomm_relayd_status_stream), 0);
	if (send_ret < 0) {
		ERR("Relay sending stream id");
	}

end_no_session:
	return ret;
}

/*
 * relay_unknown_command: send -1 if received unknown command
 */
static
void relay_unknown_command(struct relay_command *cmd)
{
	struct lttcomm_relayd_generic_reply reply;
	int ret;

	reply.ret_code = htobe32(LTTCOMM_ERR);
	ret = cmd->sock->ops->sendmsg(cmd->sock, &reply,
			sizeof(struct lttcomm_relayd_generic_reply), 0);
	if (ret < 0) {
		ERR("Relay sending unknown command");
	}
}

/*
 * relay_start: send an acknowledgment to the client to tell if we are
 * ready to receive data. We are ready if a session is established.
 */
static
int relay_start(struct lttcomm_relayd_hdr *recv_hdr,
		struct relay_command *cmd)
{
	int ret = htobe32(LTTCOMM_OK);
	struct lttcomm_relayd_generic_reply reply;
	struct relay_session *session = cmd->session;

	if (!session) {
		DBG("Trying to start the streaming without a session established");
		ret = htobe32(LTTCOMM_ERR);
	}

	reply.ret_code = ret;
	ret = cmd->sock->ops->sendmsg(cmd->sock, &reply,
			sizeof(struct lttcomm_relayd_generic_reply), 0);
	if (ret < 0) {
		ERR("Relay sending start ack");
	}

	return ret;
}

/*
 * Get stream from stream id.
 */
static
struct relay_stream *relay_stream_from_stream_id(uint64_t stream_id,
		struct lttng_ht *streams_ht)
{
	struct lttng_ht_node_ulong *node;
	struct lttng_ht_iter iter;
	struct relay_stream *ret;

	lttng_ht_lookup(streams_ht,
			(void *)((unsigned long) stream_id),
			&iter);
	node = lttng_ht_iter_get_node_ulong(&iter);
	if (node == NULL) {
		DBG("Relay stream %lu not found", stream_id);
		ret = NULL;
		goto end;
	}

	ret = caa_container_of(node, struct relay_stream, stream_n);

end:
	return ret;
}

/*
 * relay_recv_metadata: receive the metada for the session.
 */
static
int relay_recv_metadata(struct lttcomm_relayd_hdr *recv_hdr,
		struct relay_command *cmd, struct lttng_ht *streams_ht)
{
	int ret = htobe32(LTTCOMM_OK);
	struct relay_session *session = cmd->session;
	struct lttcomm_relayd_metadata_payload *metadata_struct;
	struct relay_stream *metadata_stream;
	uint64_t data_size, payload_size;

	if (!session) {
		ERR("Metadata sent before version check");
		ret = -1;
		goto end;
	}

	data_size = be64toh(recv_hdr->data_size);
	payload_size = data_size - sizeof(uint64_t);
	if (data_buffer_size < data_size) {
		data_buffer = realloc(data_buffer, data_size);
		if (!data_buffer) {
			ERR("Allocating data buffer");
			ret = -1;
			goto end;
		}
		data_buffer_size = data_size;
	}
	memset(data_buffer, 0, data_size);
	DBG2("Relay receiving metadata, waiting for %lu bytes", data_size);
	ret = cmd->sock->ops->recvmsg(cmd->sock, data_buffer, data_size, MSG_WAITALL);
	if (ret < 0 || ret != data_size) {
		ret = -1;
		ERR("Relay didn't receive the whole metadata");
		goto end;
	}
	metadata_struct = (struct lttcomm_relayd_metadata_payload *) data_buffer;
	metadata_stream = relay_stream_from_stream_id(
			be64toh(metadata_struct->stream_id), streams_ht);
	if (!metadata_stream) {
		ret = -1;
		goto end;
	}

	ret = write(metadata_stream->fd, metadata_struct->payload,
			payload_size);
	if (ret < (payload_size)) {
		ERR("Relay error writing metadata on file");
		ret = -1;
		goto end;
	}
	DBG2("Relay metadata written");

end:
	return ret;
}

/*
 * relay_send_version: send relayd version number
 */
static
int relay_send_version(struct lttcomm_relayd_hdr *recv_hdr,
		struct relay_command *cmd)
{
	int ret = htobe32(LTTCOMM_OK);
	struct lttcomm_relayd_version reply;
	struct relay_session *session = NULL;

	if (cmd->session == NULL) {
		session = zmalloc(sizeof(struct relay_session));
		if (session == NULL) {
			PERROR("relay session zmalloc");
			ret = -1;
			goto end;
		}
		session->id = ++last_relay_session_id;
		DBG("Created session %lu", session->id);
		cmd->session = session;
	}
	session->version_check_done = 1;

	sscanf(VERSION, "%u.%u", &reply.major, &reply.minor);
	reply.major = htobe32(reply.major);
	reply.minor = htobe32(reply.minor);
	ret = cmd->sock->ops->sendmsg(cmd->sock, &reply,
			sizeof(struct lttcomm_relayd_version), 0);
	if (ret < 0) {
		ERR("Relay sending version");
	}
	DBG("Version check done");

end:
	return ret;
}

/*
 * relay_process_control: Process the commands received on the control socket
 */
static
int relay_process_control(struct lttcomm_relayd_hdr *recv_hdr,
		struct relay_command *cmd, struct lttng_ht *streams_ht)
{
	int ret = 0;

	switch (be32toh(recv_hdr->cmd)) {
		/*
	case RELAYD_CREATE_SESSION:
		ret = relay_create_session(recv_hdr, cmd);
		break;
		*/
	case RELAYD_ADD_STREAM:
		ret = relay_add_stream(recv_hdr, cmd, streams_ht);
		break;
	case RELAYD_START_DATA:
		ret = relay_start(recv_hdr, cmd);
		break;
	case RELAYD_SEND_METADATA:
		ret = relay_recv_metadata(recv_hdr, cmd, streams_ht);
		break;
	case RELAYD_VERSION:
		ret = relay_send_version(recv_hdr, cmd);
		break;
	case RELAYD_UPDATE_SYNC_INFO:
	default:
		ERR("Received unknown command (%u)", be32toh(recv_hdr->cmd));
		relay_unknown_command(cmd);
		ret = -1;
		goto end;
	}

end:
	return ret;
}

/*
 * relay_process_data: Process the data received on the data socket
 */
static
int relay_process_data(struct relay_command *cmd, struct lttng_ht *streams_ht)
{
	int ret = 0;
	struct relay_stream *stream;
	struct lttcomm_relayd_data_hdr data_hdr;
	uint64_t stream_id;
	uint32_t data_size;

	ret = cmd->sock->ops->recvmsg(cmd->sock, &data_hdr,
			sizeof(struct lttcomm_relayd_data_hdr), MSG_WAITALL);
	if (ret <= 0) {
		ERR("Connections seems to be closed");
		ret = -1;
		goto end;
	}

	stream_id = be64toh(data_hdr.stream_id);
	stream = relay_stream_from_stream_id(stream_id, streams_ht);
	if (!stream) {
		ret = -1;
		goto end;
	}

	data_size = be32toh(data_hdr.data_size);
	if (data_buffer_size < data_size) {
		data_buffer = realloc(data_buffer, data_size);
		if (!data_buffer) {
			ERR("Allocating data buffer");
			ret = -1;
			goto end;
		}
		data_buffer_size = data_size;
	}
	memset(data_buffer, 0, data_size);

	ret = cmd->sock->ops->recvmsg(cmd->sock, data_buffer, data_size, MSG_WAITALL);
	if (ret <= 0) {
		ret = -1;
		goto end;
	}

	ret = write(stream->fd, data_buffer, data_size);
	if (ret < data_size) {
		ERR("Relay error writing data to file");
		ret = -1;
		goto end;
	}
	DBG2("Relay wrote %d bytes to tracefile for stream id %lu", ret, stream->stream_handle);

end:
	return ret;
}

static
void relay_cleanup_connection(struct lttng_ht *relay_connections_ht, struct lttng_poll_event *events,
		struct lttng_ht *streams_ht, int pollfd, struct lttng_ht_iter *iter)
{
	int ret;

	ret = lttng_ht_del(relay_connections_ht, iter);
	assert(!ret);
	lttng_poll_del(events, pollfd);

	ret = close(pollfd);
	if (ret < 0) {
		ERR("Closing pollfd %d", pollfd);
	}
}

static
int relay_add_connection(int fd, struct lttng_poll_event *events,
		struct lttng_ht *relay_connections_ht)
{
	int ret;
	struct relay_command *relay_connection;

	relay_connection = zmalloc(sizeof(struct relay_command));
	if (relay_connection == NULL) {
		PERROR("Relay command zmalloc");
		ret = -1;
		goto end;
	}
	ret = read(fd, relay_connection, sizeof(struct relay_command));
	if (ret < 0 || ret < sizeof(relay_connection)) {
		PERROR("read relay cmd pipe");
		ret = -1;
		goto end;
	}

	lttng_ht_node_init_ulong(&relay_connection->sock_n,
			(unsigned long) relay_connection->sock->fd);
	lttng_ht_add_unique_ulong(relay_connections_ht,
			&relay_connection->sock_n);
	ret = lttng_poll_add(events,
			relay_connection->sock->fd,
			LPOLLIN | LPOLLRDHUP);

end:
	return ret;
}

/*
 * This thread does the actual work
 */
static
void *relay_thread_worker(void *data)
{
	int i, ret, pollfd;
	uint32_t revents, nb_fd;
	struct relay_command *relay_connection;
	struct lttng_poll_event events;
	struct lttng_ht *relay_connections_ht;
	struct lttng_ht_node_ulong *node;
	struct lttng_ht_iter iter;
	struct lttng_ht *streams_ht;
	struct lttcomm_relayd_hdr recv_hdr;

	DBG("[thread] Relay worker started");

	/* table of connections indexed on socket */
	relay_connections_ht = lttng_ht_new(0, LTTNG_HT_TYPE_ULONG);

	/* tables of streams indexed by stream ID */
	streams_ht = lttng_ht_new(0, LTTNG_HT_TYPE_ULONG);

	ret = create_thread_poll_set(&events, 2);
	if (ret < 0) {
		goto error_poll_create;
	}

	ret = lttng_poll_add(&events, relay_cmd_pipe[0], LPOLLIN | LPOLLRDHUP);
	if (ret < 0) {
		goto error;
	}

	while (1) {
		/* Zeroed the events structure */
		lttng_poll_reset(&events);

		nb_fd = LTTNG_POLL_GETNB(&events);

		/* Infinite blocking call, waiting for transmission */
	restart:
		ret = lttng_poll_wait(&events, -1);
		if (ret < 0) {
			/*
			 * Restart interrupted system call.
			 */
			if (errno == EINTR) {
				goto restart;
			}
			goto error;
		}

		for (i = 0; i < nb_fd; i++) {
			/* Fetch once the poll data */
			revents = LTTNG_POLL_GETEV(&events, i);
			pollfd = LTTNG_POLL_GETFD(&events, i);

			/* Thread quit pipe has been closed. Killing thread. */
			ret = check_thread_quit_pipe(pollfd, revents);
			if (ret) {
				goto error;
			}

			/* Inspect the relay cmd pipe for new connection */
			if (pollfd == relay_cmd_pipe[0]) {
				if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
					ERR("Relay pipe error");
					goto error;
				} else if (revents & LPOLLIN) {
					DBG("Relay command received");
					ret = relay_add_connection(relay_cmd_pipe[0],
							&events, relay_connections_ht);
					if (ret < 0) {
						goto error;
					}
				}
			} else if (revents > 0) {
				lttng_ht_lookup(relay_connections_ht,
						(void *)((unsigned long) pollfd),
						&iter);
				node = lttng_ht_iter_get_node_ulong(&iter);
				if (node == NULL) {
					DBG2("Relay sock %d not found", pollfd);
					goto error;
				}
				relay_connection = caa_container_of(node,
						struct relay_command, sock_n);

				if (revents & (LPOLLERR)) {
					ERR("POLL ERROR");
					relay_cleanup_connection(relay_connections_ht,
							&events, streams_ht, pollfd, &iter);
					free(relay_connection);
				} else if (revents & (LPOLLHUP | LPOLLRDHUP)) {
					DBG("Socket %d hung up", pollfd);
					relay_cleanup_connection(relay_connections_ht,
							&events, streams_ht, pollfd, &iter);
					if (relay_connection->type == RELAY_CONTROL) {
						relay_delete_session(relay_connection, streams_ht);
					}
					free(relay_connection);
				} else if (revents & LPOLLIN) {
					/* control socket */
					if (relay_connection->type == RELAY_CONTROL) {
						ret = relay_connection->sock->ops->recvmsg(
								relay_connection->sock, &recv_hdr,
								sizeof(struct lttcomm_relayd_hdr), MSG_WAITALL);
						/* connection closed */
						if (ret <= 0) {
							relay_cleanup_connection(relay_connections_ht,
									&events, streams_ht, pollfd, &iter);
							relay_delete_session(relay_connection, streams_ht);
							free(relay_connection);
							DBG("Control connection closed with %d", pollfd);
						} else {
							if (relay_connection->session) {
								DBG2("Relay worker receiving data for session : %lu",
										relay_connection->session->id);
							}
							ret = relay_process_control(&recv_hdr,
									relay_connection,
									streams_ht);
							/*
							 * there was an error in processing a control
							 * command: clear the session
							 * */
							if (ret < 0) {
								relay_cleanup_connection(relay_connections_ht,
										&events, streams_ht, pollfd, &iter);
								free(relay_connection);
								DBG("Connection closed with %d", pollfd);
							}
						}
						/* data socket */
					} else if (relay_connection->type == RELAY_DATA) {
						ret = relay_process_data(relay_connection, streams_ht);
						/* connection closed */
						if (ret < 0) {
							relay_cleanup_connection(relay_connections_ht,
									&events, streams_ht, pollfd, &iter);
							relay_delete_session(relay_connection, streams_ht);
							DBG("Data connection closed with %d", pollfd);
						}
					}
				}
			}
		}
	}

error:
	lttng_poll_clean(&events);

	/* empty the hash table and free the memory */
	cds_lfht_for_each_entry(relay_connections_ht->ht, &iter.iter, node, node) {
		node = lttng_ht_iter_get_node_ulong(&iter);
		if (node) {
			relay_connection = caa_container_of(node,
					struct relay_command, sock_n);
			free(relay_connection);
		}
		ret = lttng_ht_del(relay_connections_ht, &iter);
		assert(!ret);
	}
error_poll_create:
	free(data_buffer);
	lttng_ht_destroy(relay_connections_ht);
	DBG("Worker thread cleanup complete");
	stop_threads();
	return NULL;
}

/*
 * Create the relay command pipe to wake thread_manage_apps.
 * Closed in cleanup().
 */
static int create_relay_cmd_pipe(void)
{
	int ret, i;

	ret = pipe(relay_cmd_pipe);
	if (ret < 0) {
		PERROR("relay cmd pipe");
		goto error;
	}

	for (i = 0; i < 2; i++) {
		ret = fcntl(relay_cmd_pipe[i], F_SETFD, FD_CLOEXEC);
		if (ret < 0) {
			PERROR("fcntl relay_cmd_pipe");
			goto error;
		}
	}

error:
	return ret;
}

/*
 * main
 */
int main(int argc, char **argv)
{
	int ret = 0;
	void *status;

	/* Create thread quit pipe */
	if ((ret = init_thread_quit_pipe()) < 0) {
		goto error;
	}

	/* Parse arguments */
	progname = argv[0];
	if ((ret = parse_args(argc, argv) < 0)) {
		goto error;
	}

	if ((ret = set_signal_handler()) < 0) {
		goto exit;
	}

	/* Daemonize */
	if (opt_daemon) {
		ret = daemon(0, 0);
		if (ret < 0) {
			PERROR("daemon");
			goto error;
		}
	}

	/* Check if daemon is UID = 0 */
	is_root = !getuid();

	if (!is_root) {
		if (control_uri->port < 1024 || data_uri->port < 1024) {
			ERR("Need to be root to use ports < 1024");
			ret = -1;
			goto error;
		}
	}

	/* Setup the thread apps communication pipe. */
	if ((ret = create_relay_cmd_pipe()) < 0) {
		goto exit;
	}

	/* Init relay command queue. */
	cds_wfq_init(&relay_cmd_queue.queue);

	/* Set up max poll set size */
	lttng_poll_set_max_size();

	/* Setup the dispatcher thread */
	ret = pthread_create(&dispatcher_thread, NULL,
			relay_thread_dispatcher, (void *) NULL);
	if (ret != 0) {
		PERROR("pthread_create dispatcher");
		goto exit_dispatcher;
	}

	/* Setup the worker thread */
	ret = pthread_create(&worker_thread, NULL,
			relay_thread_worker, (void *) NULL);
	if (ret != 0) {
		PERROR("pthread_create worker");
		goto exit_worker;
	}

	/* Setup the listener thread */
	ret = pthread_create(&listener_thread, NULL,
			relay_thread_listener, (void *) NULL);
	if (ret != 0) {
		PERROR("pthread_create listener");
		goto exit_listener;
	}

exit_listener:
	ret = pthread_join(listener_thread, &status);
	if (ret != 0) {
		PERROR("pthread_join");
		goto error;	/* join error, exit without cleanup */
	}

exit_worker:
	ret = pthread_join(worker_thread, &status);
	if (ret != 0) {
		PERROR("pthread_join");
		goto error;	/* join error, exit without cleanup */
	}

exit_dispatcher:
	ret = pthread_join(dispatcher_thread, &status);
	if (ret != 0) {
		PERROR("pthread_join");
		goto error;	/* join error, exit without cleanup */
	}

exit:
	cleanup();
	if (!ret) {
		exit(EXIT_SUCCESS);
	}
error:
	exit(EXIT_FAILURE);
}
