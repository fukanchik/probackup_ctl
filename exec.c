#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "probackup_ctl.h"

#define PROBACKUP_RESPONSE_TIME 5

bool probackup_log_commands = false;

/* Convert List* into execvp array */
static char **
list_to_args(List *args_list)
{
	int             len = list_length(args_list);
	char          **ret = palloc((len + 1) * sizeof(char *));
	const ListCell *cell;
	int             i = 0;

	foreach (cell, args_list)
	{
		char *p  = lfirst(cell);
		ret[i++] = p;
	}

	ret[i] = NULL;

	if (probackup_log_commands)
	{
		StringInfoData resp;
		char         **p = ret;

		initStringInfo(&resp);
		while (*p)
		{
			appendStringInfo(&resp, " %s", *p);
			p++;
		}
		ereport(INFO, errmsg("command:%s", resp.data));
	}
	if (ret[0] == NULL)
	{
		ereport(ERROR, errmsg("Empty argument list"));
	}

	return ret;
}

/* Exec with redefining stdout/err into given fds */
static void
run0(int fd_out, int fd_err, char *args[])
{
	if (dup2(fd_out, STDOUT_FILENO) < 0)
	{
		ereport(ERROR, errmsg("dup2(%s): %m", args[0]));
	}

	close(fd_out);

	if (dup2(fd_err, STDERR_FILENO) < 0)
	{
		ereport(ERROR, errmsg("dup2(%s): %m", args[0]));
	}

	close(fd_err);

	execvp(args[0], args);

	ereport(ERROR, errmsg("run '%s': %m", args[0]));
}

/* Communicate to subprocess via out_fd and err_fd accumulating output into out
 * and err. */
static void
feed(int out_fd, int err_fd, StringInfoData *out, StringInfoData *err)
{
	char           buf[BUFSZ];
	StringInfoData resp;
	int            flags = 1;
	ioctl(err_fd, FIONBIO, &flags);
	ioctl(out_fd, FIONBIO, &flags);

	initStringInfo(&resp);

	while (1)
	{
		int            retval;
		fd_set         rfds;
		struct timeval tv;
		int            nfds;
		int            maxfd;

	again:
		nfds  = 0;
		maxfd = 0;
		FD_ZERO(&rfds);

		if (out_fd != -1)
		{
			FD_SET(out_fd, &rfds);
			nfds++;
			if (out_fd > maxfd) maxfd = out_fd;
		}
		if (err_fd != -1)
		{
			FD_SET(err_fd, &rfds);
			nfds++;
			if (err_fd > maxfd) maxfd = err_fd;
		}

		tv.tv_sec  = PROBACKUP_RESPONSE_TIME;
		tv.tv_usec = 0;

		if (nfds == 0) break;
		retval = select(maxfd + 1, &rfds, NULL, NULL, &tv);

		if (retval == -1)
		{
			if (errno == EINTR) goto again;
			ereport(ERROR, errmsg("select(): %m"));
		} else if (retval)
		{
			if (FD_ISSET(out_fd, &rfds))
			{
				int rc = read(out_fd, buf, BUFSZ - 1);
				if (rc == 0)
				{
					close(out_fd);
					out_fd = -1;
				} else
				{
					buf[rc] = '\0';
					appendStringInfo(out, "%s", buf);
				}
			}
			if (FD_ISSET(err_fd, &rfds))
			{
				int rc = read(err_fd, buf, BUFSZ - 1);
				if (rc == 0)
				{
					close(err_fd);
					err_fd = -1;
				} else
				{
					buf[rc] = '\0';
					appendStringInfo(err, "%s", buf);
				}
			}
		} else
			ereport(INFO, errmsg("Probackup not responding within %d seconds",
			                     PROBACKUP_RESPONSE_TIME));
	}
}

/* Run a program with argument list, collect output into out and err. */
int
run(List *args_list, StringInfoData *out, StringInfoData *err)
{
	int   out_pipes[2];
	int   err_pipes[2];
	pid_t child;
	int   status;

	if (pipe(out_pipes) < 0)
	{
		ereport(ERROR, errmsg("pipe(): %m"));
	}

	if (pipe(err_pipes) < 0)
	{
		ereport(ERROR, errmsg("pipe(): %m"));
	}

	child = fork();

	if (child < 0)
	{
		ereport(ERROR, errmsg("fork(): %m"));
	}

	if (child == 0)
	{
		close(out_pipes[0]);
		close(err_pipes[0]);
		run0(out_pipes[1], err_pipes[1], list_to_args(args_list));
	}

	close(out_pipes[1]);
	close(err_pipes[1]);

	feed(out_pipes[0], err_pipes[0], out, err);

	waitpid(child, &status, 0);

	return status;
}
