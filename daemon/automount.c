#ident "$Id: automount.c,v 1.38 2005/03/06 09:43:55 raven Exp $"
/* ----------------------------------------------------------------------- *
 *
 *  automount.c - Linux automounter daemon
 *   
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *   Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *   Copyright 2001-2003 Ian Kent <raven@themaw.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *   
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * ----------------------------------------------------------------------- */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <linux/auto_fs4.h>

#include "automount.h"

#ifndef NDEBUG
#define assert(x) 						    \
	do { 							    \
		if (!(x)) {					    \
			crit(__FILE__ ":%d: assertion failed: " #x, \
				__LINE__);			    \
		}						    \
	} while(0)
#else
#define assert(x)	do { } while(0)
#endif

#ifndef NDEBUG
#define assert_r(context, x) 					   \
	do { 							   \
		if (!(x)) {					   \
			crit_r(context,				   \
				__FILE__ ":%d: assertion failed: ",\
				__LINE__);			   \
		}						   \
	} while(0)
#else
#define assert_r(context, x)	do { } while(0)
#endif

const char *program;		/* Initialized with argv[0] */
const char *version = VERSION_STRING;	/* Program version */

static pid_t my_pgrp;		/* The "magic" process group */
static pid_t my_pid;		/* The pid of this process */
static char *pid_file = NULL;	/* File in which to keep pid */

int kproto_version;		/* Kernel protocol version used */
int kproto_sub_version = 0;	/* Kernel protocol version used */
int dumpmap = 0;		/* cmdline arg to dump map contents */

static int submount = 0;

int do_verbose = 0;		/* Verbose feedback option */
int do_debug = 0;		/* Enable full debug output */

sigset_t ready_sigs;		/* signals only accepted in ST_READY */
sigset_t lock_sigs;		/* signals blocked for locking */
sigset_t sigchld_mask;

struct autofs_point ap;

/* re-entrant syslog default context data */
#define AUTOFS_SYSLOG_CONTEXT {-1, 0, 0, LOG_PID, (const char *)0, LOG_DAEMON, 0xff}

volatile struct pending_mount *junk_mounts = NULL;

#define CHECK_RATIO     4	/* exp_runfreq = exp_timeout/CHECK_RATIO */
#define DEFAULT_GHOST_MODE	0

#define EXIT_CHECK_TIME		2000	/* Total time to wait before retry */
#define EXIT_CHECK_DELAY	200	/* Time interval to check if exited */

static void cleanup_exit(const char *path, int exit_code);
static int handle_packet_expire(const struct autofs_packet_expire *pkt);
static int umount_all(int force);

static int do_mkdir(const char *path, mode_t mode)
{
	int status;
	struct stat st;

	status = stat(path, &st);
	if (status == 0) {
		if (!S_ISDIR(st.st_mode)) {
			errno = ENOTDIR;
			return 0;
		}
		return 1;
	}

	if (contained_in_local_fs(path)) {
		if (mkdir(path, mode) == -1) {
			if (errno == EEXIST)
				return 1;
			return 0;
		}
		return 1;
	}

	return 0;
}

int mkdir_path(const char *path, mode_t mode)
{
	char *buf = alloca(strlen(path) + 1);
	const char *cp = path, *lcp = path;
	char *bp = buf;

	do {
		if (cp != path && (*cp == '/' || *cp == '\0')) {
			memcpy(bp, lcp, cp - lcp);
			bp += cp - lcp;
			lcp = cp;
			*bp = '\0';
			if (!do_mkdir(buf, mode)) {
				if (*cp != '\0')
					continue;
				return -1;
			}
		}
	} while (*cp++ != '\0');

	return 0;
}

/* Remove as much as possible of a path */
int rmdir_path(const char *path)
{
	int len = strlen(path);
	char *buf = alloca(len + 1);
	char *cp;
	int first = 1;

	strcpy(buf, path);
	cp = buf + len;

	do {
		*cp = '\0';

		/* Last element of path may be non-dir;
		   all others are directories */
		if (rmdir(buf) == -1 && (!first || unlink(buf) == -1))
			return -1;

		first = 0;
	} while ((cp = strrchr(buf, '/')) != NULL && cp != buf);

	return 0;
}

static int umount_ent(const char *root, const char *name, const char *type)
{
	char path_buf[PATH_MAX];
	struct stat st;
	int sav_errno;
	int is_smbfs = (strcmp(type, "smbfs") == 0);
	int status;
	int rv = 0;

	sprintf(path_buf, "%s/%s", root, name);
	status =  lstat(path_buf, &st);
	sav_errno = errno;

	/* EIO appears to correspond to an smb mount that has gone away */
	if (!status ||
	    (is_smbfs && (sav_errno == EIO || sav_errno == EBADSLT))) {
		int umount_ok = 0;

		if (!status && (S_ISDIR(st.st_mode) && (st.st_dev != ap.dev)))
			umount_ok = 1;

		if (umount_ok || is_smbfs) {
			rv = spawnll(LOG_DEBUG, 
				    PATH_UMOUNT, PATH_UMOUNT, path_buf, NULL);
		}
	}
	return rv;
}

/* Like ftw, except fn gets called twice: before a directory is
   entered, and after.  If the before call returns 0, the directory
   isn't entered. */
static int walk_tree(const char *base, int (*fn) (const char *file,
						  const struct stat * st,
						  int, void *), int incl, void *arg)
{
	char buf[PATH_MAX + 1];
	struct stat st;

	if (lstat(base, &st) != -1 && (fn) (base, &st, 0, arg)) {
		if (S_ISDIR(st.st_mode)) {
			struct dirent **de;
			int n;

			n = scandir(base, &de, 0, alphasort);
			if (n < 0)
				return -1;

			while (n--) {
				int ret, size;

				if (strcmp(de[n]->d_name, ".") == 0 ||
				    strcmp(de[n]->d_name, "..") == 0)
					continue;

				size = sizeof(buf);
				ret = cat_path(buf, size, base, de[n]->d_name);
				if (!ret) {
					do {
						free(de[n]);
					} while (n--);
					free(de);
					return -1;
				}

				walk_tree(buf, fn, 1, arg);
				free(de[n]);
			}
			free(de);
		}
		if (incl)
			(fn) (base, &st, 1, arg);
	}
	return 0;
}

static int rm_unwanted_fn(const char *file, const struct stat *st, int when, void *arg)
{
	int rmsymlink = *(int *) arg;
	struct stat newst;

	if (when == 0) {
		if (st->st_dev != ap.dev)
			return 0;
		return 1;
	}
	info("rm_unwanted_fn: want to remove %s\n", file);
	if (lstat(file, &newst)) {
		crit ("rm_unwanted_fn: unable to stat file, possible race "
		      "condition.");
		return 0;
	}

	if (newst.st_dev != ap.dev) {
		crit ("rm_unwanted_fn: file %s has the wrong device, possible "
		      "race condition.",file);
		return 0;
	}

	if (S_ISDIR(newst.st_mode)) {
		if (rmdir(file)) {
			info ("rm_unwanted_fn: unable to remove directory"
			      " %s", file);
			return 0;
		} else {
		  info("rm_unwanted_fn: removed directory %s",file);
		}
	} else if (S_ISREG(newst.st_mode)) {
		crit ("rm_unwanted_fn: attempting to remove file from a mounted "
		      "directory. - not doing it");
		return 0;
	} else if (S_ISLNK(newst.st_mode) && rmsymlink) {
	        info ("rm_unwanted_fn: removing symlink %s",file);
		unlink(file);
	}

	return 1;
}

static void rm_unwanted(const char *path, int incl, int rmsymlink)
{
	walk_tree(path, rm_unwanted_fn, incl, &rmsymlink);
}

static void check_rm_dirs(const char *path, int incl)
{
	if ((!ap.ghost) ||
	    (ap.state == ST_SHUTDOWN_PENDING ||
	     ap.state == ST_SHUTDOWN))
		rm_unwanted(path, incl, 1);
	else if (ap.ghost && (ap.type == LKP_INDIRECT))
		rm_unwanted(path, 0, 1);
}

/* umount all filesystems mounted under path.  If incl is true, then
   it also tries to umount path itself */
static int umount_multi(const char *path, int incl)
{
	int left;
	struct mnt_list *mntlist = NULL;
	struct mnt_list *mptr;

	debug("umount_multi: path=%s incl=%d\n", path, incl);

	mntlist = get_mnt_list(_PATH_MOUNTED, path, incl);

	if (!mntlist) {
		warn("umount_multi: no mounts found under %s", path);
		check_rm_dirs(path, incl);
		return 0;
	}

	left = 0;
	for (mptr = mntlist; mptr != NULL; mptr = mptr->next) {
		debug("umount_multi: unmounting dir=%s\n", mptr->path);
		if (umount_ent("", mptr->path, mptr->fs_type)) {
			left++;
		}
	}

	free_mnt_list(mntlist);

	/* Delete detritus like unwanted mountpoints and symlinks */
	if (left == 0)
		check_rm_dirs(path, incl);

	return left;
}

static int umount_all(int force)
{
	int left;

	chdir("/");

	left = umount_multi(ap.path, 0);

	if (force && left)
		warn("could not unmount %d dirs under %s", left, ap.path);

	return left;
}

static int do_umount_autofs(void)
{
	int rv;
	int i;
	const int retries = 3;

	if (ap.ioctlfd >= 0) {
		ioctl(ap.ioctlfd, AUTOFS_IOC_CATATONIC, 0);
		close(ap.ioctlfd);
		close(ap.state_pipe[0]);
		close(ap.state_pipe[1]);
	}
	if (ap.pipefd >= 0)
		close(ap.pipefd);
	for (i = 0; i < retries; i++) {
		struct stat st;
		int ret;

		rv = spawnll(LOG_DEBUG,
			    PATH_UMOUNT, PATH_UMOUNT, ap.path, NULL);
		if (rv & MTAB_NOTUPDATED) {
			info("umount %s succeeded: "
			     "mtab not updated, retrying to clean\n",
			      ap.path);
			rv = spawnll(LOG_DEBUG,
				    PATH_UMOUNT, PATH_UMOUNT, ap.path, NULL);
		}
		ret = stat(ap.path, &st);
		if (rv == 0 || (ret == -1 && errno == ENOENT) ||
		    (ret == 0 && (!S_ISDIR(st.st_mode) || st.st_dev != ap.dev))) {
			rv = 0;
			break;
		}
		if (i < retries - 1) {
			info("umount %s failed: retrying...\n", ap.path);
			sleep(1);
		}
	}
	if (rv != 0 || i == retries) {
		error("can't unmount %s\n", ap.path);
		DB(kill(0, SIGSTOP));
	} else {
		if (i != 0)
			info("umount %s succeeded\n", ap.path);

		if (submount)
			rm_unwanted(ap.path, 1, 1);
	}

	free(ap.path);

	return rv;
}

static int umount_autofs(int force)
{
	if (ap.state == ST_INIT)
		return -1;

	if (umount_all(force) && !force)
		return -1;

	return do_umount_autofs();
}

static int mount_autofs(char *path)
{
	int pipefd[2];
	char options[128];
	char our_name[128];
	struct stat st;
	int len;

	if ((ap.state != ST_INIT) || is_mounted(_PATH_MOUNTED, path)) {
		/* This can happen if an autofs process is already running*/
		error("mount_autofs: already mounted");
		return -1;
	}

	/* Must be an absolute pathname */
	if (path[0] != '/') {
		errno = EINVAL;
		return -1;
	}

	ap.path = strdup(path);
	if (!ap.path) {
		errno = ENOMEM;
		return -1;
	}
	ap.pipefd = ap.ioctlfd = -1;

	/* In case the directory doesn't exist, try to mkdir it */
	if (mkdir_path(path, 0555) < 0) {
		if (errno != EEXIST && errno != EROFS) {
			crit("failed to create iautofs directory %s", ap.path);
			return -1;
		}
		/* If we recieve an error, and it's EEXIST or EROFS we know
		   the directory was not created. */
		ap.dir_created = 0;
	} else {
		/* No errors so the directory was successfully created */
		ap.dir_created = 1;
	}

	/* Pipe for kernel communications */
	if (pipe(pipefd) < 0) {
		crit("failed to create commumication pipe for autofs path %s",
		     ap.path);
		rmdir_path(ap.path);
		return -1;
	}

	/* Pipe state changes from signal handler to main loop */
	if (pipe(ap.state_pipe) < 0) {
		crit("failed create state pipe for autofs path %s", ap.path);
		rmdir_path(ap.path);
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}

	len = snprintf(options, sizeof(options),
			"fd=%d,pgrp=%u,minproto=2,maxproto=%d", pipefd[1],
			(unsigned) my_pgrp, AUTOFS_MAX_PROTO_VERSION);
	if (len >= sizeof(options)) {
		crit("buffer to small for options - truncated");
		len = sizeof(options)-1;
	}
	if (len < 0) {
                crit("failed setting up options for autofs path %s", ap.path);
                rmdir_path(ap.path);
                close(pipefd[0]);
                close(pipefd[1]);
                return -1;
        }	
	options[len] = '\0';

	len = snprintf(our_name, sizeof(our_name),
			"automount(pid%u)", (unsigned) my_pid);
	if (len >= sizeof(our_name)) {
		crit("buffer to small for our_name - truncated");
		len = sizeof(our_name)-1;
	}
        if (len < 0) {
                crit("failed setting up our_name for autofs path %s", ap.path);
                rmdir_path(ap.path);
                close(pipefd[0]);
                close(pipefd[1]);
                return -1;
        }
	our_name[len] = '\0';

	if (spawnll(LOG_DEBUG, PATH_MOUNT, PATH_MOUNT,
		   "-t", "autofs", "-o", options, our_name, path, NULL) != 0) {
		crit("failed to mount autofs path %s", ap.path);
		rmdir_path(ap.path);
		close(pipefd[0]);
		close(pipefd[1]);
		close(ap.state_pipe[0]);
		close(ap.state_pipe[1]);
		return -1;
	}

	close(pipefd[1]);	/* Close kernel pipe end */
	ap.pipefd = pipefd[0];

	ap.ioctlfd = open(path, O_RDONLY);	/* Root directory for ioctl()'s */
	if (ap.ioctlfd < 0) {
		umount_autofs(1);
		return -1;
	}

	stat(path, &st);
	ap.dev = st.st_dev;	/* Device number for mount point checks */

	ap.mounts = NULL;	/* No pending mounts */
	ap.state = ST_READY;

	return 0;
}

static void nextstate(enum states next)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;

	if (write(ap.state_pipe[1], &next, sizeof(next)) != sizeof(next))
		error_r(slc, "nextstate: write failed %m");
}

/* Deal with all the signal-driven events in the state machine */
static void sig_statemachine(int sig)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;
	int save_errno = errno;
	enum states next = ap.state;

	switch (sig) {
	default:		/* all the "can't happen" signals */
		error_r(slc, "process %d got unexpected signal %d!",
			getpid(), sig);
		break;
		/* don't FALLTHROUGH */

	case SIGTERM:
	case SIGUSR2:
		if (ap.state != ST_SHUTDOWN)
			nextstate(next = ST_SHUTDOWN_PENDING);
		break;

	case SIGUSR1:
		assert(ap.state == ST_READY);
		nextstate(next = ST_PRUNE);
		break;

	case SIGALRM:
		assert(ap.state == ST_READY);
		nextstate(next = ST_EXPIRE);
		break;

	case SIGHUP:
		assert(ap.state == ST_READY);
		nextstate(next = ST_READMAP);
		break;
	}

	debug_r(slc, "sig %d switching from %d to %d", sig, ap.state, next);

	errno = save_errno;
}

static int send_ready(unsigned int wait_queue_token)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;

	if (wait_queue_token == 0)
		return 0;
	debug_r(slc, "send_ready: token=%d\n", wait_queue_token);
	if (ioctl(ap.ioctlfd, AUTOFS_IOC_READY, wait_queue_token) < 0) {
		error_r(slc, "AUTOFS_IOC_READY: %m");
		return 1;
	}
	return 0;
}

static int send_fail(unsigned int wait_queue_token)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;

	if (wait_queue_token == 0)
		return 0;
	debug_r(slc, "send_fail: token=%d\n", wait_queue_token);
	if (ioctl(ap.ioctlfd, AUTOFS_IOC_FAIL, wait_queue_token) < 0) {
		error_r(slc, "AUTOFS_IOC_FAIL: %m");
		return 1;
	}
	return 0;
}

/* Handle exiting children (either from SIGCHLD or synchronous wait at
   shutdown), and return the next state the system should enter as a
   result.  */
static enum states handle_child(int hang)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;
	pid_t pid;
	int status;
	enum states next = ST_INVAL;

	while ((pid = waitpid(-1, &status, hang ? 0 : WNOHANG)) > 0) {
		struct pending_mount volatile *mt, *volatile *mtp;

		debug_r(slc, "handle_child: got pid %d, sig %d (%d), stat %d",
			pid, WIFSIGNALED(status),
			WTERMSIG(status), WEXITSTATUS(status));

		/* Check to see if expire process finished */
		if (pid == ap.exp_process) {
			int success, ret;

			if (!WIFEXITED(status))
				continue;

			success = !WIFSIGNALED(status) && (WEXITSTATUS(status) == 0);

			ap.exp_process = 0;

			switch (ap.state) {
			case ST_EXPIRE:
				alarm(ap.exp_runfreq);
				/* FALLTHROUGH */
			case ST_PRUNE:
				/* If we're a submount and we've just
				   pruned or expired everything away,
				   try to shut down */
				if (submount && success && ap.state != ST_SHUTDOWN) {
					next = ST_SHUTDOWN_PENDING;
					break;
				}
				/* FALLTHROUGH */

			case ST_READY:
				next = ST_READY;
				break;

			case ST_SHUTDOWN_PENDING:
				next = ST_SHUTDOWN;
				if (success) {
					ret = ioctl(ap.ioctlfd,
						AUTOFS_IOC_ASKUMOUNT, &status);
					if (!ret) {
						if (status)
							break;
					} else
						break;
				}

				/* Failed shutdown returns to ready */
				warn_r(slc,
				   "can't shutdown: filesystem %s still busy",
				   ap.path);
				alarm(ap.exp_runfreq);
				next = ST_READY;
				break;

			default:
				error_r(slc, "bad state %d", ap.state);
			}

			if (next != ST_INVAL)
				debug_r(slc, "sigchld: exp "
				     "%d finished, switching from %d to %d",
				     pid, ap.state, next);

			continue;
		}

		/* Run through pending mount/unmounts and see what (if
		   any) has finished, and tell the kernel about it */
		for (mtp = &ap.mounts; (mt = *mtp); mtp = &mt->next) {
			if (mt->pid != pid)
				continue;

			if (!WIFEXITED(status) && !WIFSIGNALED(status))
				break;

			debug_r(slc, "sig_child: found pending iop pid %d: "
			     "signalled %d (sig %d), exit status %d",
				pid, WIFSIGNALED(status),
				WTERMSIG(status), WEXITSTATUS(status));

			if (WIFSIGNALED(status) || WEXITSTATUS(status) != 0)
				send_fail(mt->wait_queue_token);
			else
				send_ready(mt->wait_queue_token);

			/* Delete from list and add to freelist,
			   since we can't call free() here */
			*mtp = mt->next;
			mt->next = junk_mounts;
			junk_mounts = mt;

			break;
		}
	}

	return next;
}

/* Reap children */
static void sig_child(int sig)
{
	int save_errno = errno;
	enum states next;

	if (sig != SIGCHLD)
		return;

	next = handle_child(0);
	if (next != ST_INVAL)
		nextstate(next);

	errno = save_errno;
}

static int st_ready(void)
{
	debug("st_ready(): state = %d\n", ap.state);

	ap.state = ST_READY;
	sigprocmask(SIG_UNBLOCK, &lock_sigs, NULL);

	return 0;
}

static int counter_fn(const char *file, const struct stat *st, int when, void *arg)
{
	int *countp = (int *) arg;

	if (S_ISLNK(st->st_mode) || (S_ISDIR(st->st_mode) && st->st_dev != ap.dev)) {
		(*countp)++;
		return 0;
	}

	return 1;
}

/* Count mounted filesystems and symlinks */
static int count_mounts(const char *path)
{
	int count = 0;

	if (walk_tree(path, counter_fn, 0, &count) == -1)
		return -1;

	return count;
}

enum expire {
	EXP_ERROR,
	EXP_STARTED,
	EXP_DONE,
	EXP_PARTIAL
};

/*
 * Generate expiry messages.  If "now" is true, timeouts are ignored.
 *
 * Returns: ERROR	- error
 *          STARTED	- expiry process started
 *          DONE	- nothing to expire
 *          PARTIAL	- partial expire
 */
static enum expire expire_proc(int now)
{
	pid_t f;
	sigset_t old;
	int how = now;

	if (kproto_version < 4) {
		if (now)
			umount_all(0);
		else {
			struct autofs_packet_expire pkt;

			while (ioctl(ap.ioctlfd, AUTOFS_IOC_EXPIRE, &pkt) == 0)
				handle_packet_expire(&pkt);
		}

		if (count_mounts(ap.path) != 0)
			return EXP_PARTIAL;

		return EXP_DONE;
	}

	assert(ap.exp_process == 0);

	/* Block SIGCHLD and SIGALRM between forking and setting up
	   exp_process */
	sigprocmask(SIG_BLOCK, &lock_sigs, &old);

	switch (f = fork()) {
		int count;
	case 0:
		ignore_signals();
		close(ap.pipefd);
		close(ap.state_pipe[0]);
		close(ap.state_pipe[1]);

		/* Work around O(1) scheduler */
		nice(-4);

		/* Set the leaves of mount tree to expire for maps
		 * that support ghosting */

		if (kproto_version >= 4 && kproto_sub_version > 1)
			if (ap.type == LKP_DIRECT)
				how |= AUTOFS_EXP_LEAVES;

		/* 
		 * Generate expire messages until there's nothing more to
		 * expire.  If a bug prevents unmounting, limit attempts to
		 * 20/second and a few more than the known number of mounts.
		 */

		count = count_mounts(ap.path) + 3;
		while (ioctl(ap.ioctlfd, AUTOFS_IOC_EXPIRE_MULTI, &how) == 0
					&& count--) {
			struct timespec nap = { 0, 50000000 }; /*5e-2 seconds*/;
			nanosleep(&nap, NULL);
		}

		/* 
		 * EXPIRE_MULTI is synchronous, so we can be sure (famous last
		 * words) the umounts are done by the time we reach here
		 */
		if ((count = count_mounts(ap.path))) {
			debug("expire_proc: %d remaining in %s\n", count, ap.path);
			exit(1);
		}
		exit(0);

	case -1:
		error("expire: fork failed: %m");
		sigprocmask(SIG_SETMASK, &old, NULL);
		return EXP_ERROR;

	default:
		debug("expire_proc: exp_proc=%d", f);
		ap.exp_process = f;
		return EXP_STARTED;
	}
}

static int st_readmap(void)
{
	int status;

	status = ap.lookup->lookup_ghost(ap.path, ap.ghost, 0, ap.lookup->context);

	debug("st_readmap: status %d\n", status);

	/* If I don't exist in the map any more then exit */
	if (status == LKP_FAIL)
		return 0;

	return 1;
}

static int st_prepare_shutdown(void)
{
	int exp;

	info("prep_shutdown: state = %d\n", ap.state);

	assert(ap.state == ST_READY || ap.state == ST_EXPIRE);

	/* Turn off timeouts */
	alarm(0);

	/* Prevent any new mounts */
	sigprocmask(SIG_SETMASK, &lock_sigs, NULL);

	ap.state = ST_SHUTDOWN_PENDING;

	/* Where're the boss, tell everyone to finish up */
	if (getpid() == getpgrp()) 
		signal_children(SIGUSR2);

	/* Unmount everything */
	exp = expire_proc(1);

	debug("prep_shutdown: expire returns %d\n", exp);

	switch (exp) {
	case EXP_ERROR:
	case EXP_PARTIAL:
		/* It didn't work: return to ready */
		alarm(ap.exp_runfreq);
		return st_ready();

	case EXP_DONE:
		/* All expired: go straight to exit */
		ap.state = ST_SHUTDOWN;
		return 1;

	case EXP_STARTED:
		/* Wait until expiry process finishes */
		sigprocmask(SIG_SETMASK, &ready_sigs, NULL);
		return 0;
	}
	return 1;
}

static int st_prune(void)
{
	debug("st_prune(): state = %d\n", ap.state);

	assert(ap.state == ST_READY);

	/* We're the boss, pass on the prune event */
	if (getpid() == getpgrp()) 
		signal_children(SIGUSR1);

	switch (expire_proc(1)) {
	case EXP_DONE:
		if (submount)
			return st_prepare_shutdown();
		/* FALLTHROUGH */

	case EXP_ERROR:
	case EXP_PARTIAL:
		return 1;

	case EXP_STARTED:
		ap.state = ST_PRUNE;
		sigprocmask(SIG_SETMASK, &ready_sigs, NULL);
		return 0;
	}
	return 1;
}

static int st_expire(void)
{
	debug("st_expire(): state = %d\n", ap.state);

	assert(ap.state == ST_READY);

	switch (expire_proc(0)) {
	case EXP_DONE:
		if (submount)
			return st_prepare_shutdown();
		/* FALLTHROUGH */

	case EXP_ERROR:
	case EXP_PARTIAL:
		alarm(ap.exp_runfreq);
		return 1;

	case EXP_STARTED:
		ap.state = ST_EXPIRE;
		sigprocmask(SIG_SETMASK, &ready_sigs, NULL);
		return 0;
	}
	return 1;
}

static int fullread(int fd, void *ptr, size_t len)
{
	char *buf = (char *) ptr;

	while (len > 0) {
		ssize_t r = read(fd, buf, len);

		if (r == -1) {
			if (errno == EINTR)
				continue;
			break;
		}

		buf += r;
		len -= r;
	}

	return len;
}

static int get_pkt(int fd, union autofs_packet_union *pkt)
{
	sigset_t old;
	struct pollfd fds[2];

	fds[0].fd = fd;
	fds[0].events = POLLIN;
	fds[1].fd = ap.state_pipe[0];
	fds[1].events = POLLIN;

	for (;;) {
		if (poll(fds, 2, -1) == -1) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "get_pkt: poll failed: %m");
			return -1;
		}

		if (fds[1].revents & POLLIN) {
			enum states next_state;
			int ret = 1;

			if (fullread(ap.state_pipe[0], &next_state, sizeof(next_state)))
				continue;

			sigprocmask(SIG_BLOCK, &lock_sigs, &old);

			if (next_state != ap.state) {
				debug("get_pkt: state %d, next %d",
					ap.state, next_state);

				switch (next_state) {
				case ST_READY:
					ret = st_ready();
					break;

				case ST_PRUNE:
					ret = st_prune();
					break;

				case ST_EXPIRE:
					ret = st_expire();
					break;

				case ST_SHUTDOWN_PENDING:
					ret = st_prepare_shutdown();
					break;

				case ST_SHUTDOWN:
					assert(ap.state == ST_SHUTDOWN ||
					       ap.state == ST_SHUTDOWN_PENDING);
					ap.state = ST_SHUTDOWN;
					break;

				case ST_READMAP:
					/* Syncronous reread of map */
					ret = st_readmap();
					if (!ret)
						ret = st_prepare_shutdown();
					break;

				default:
					error("get_pkt: bad next state %d",
					      next_state);
				}
			}

			if (ret)
				sigprocmask(SIG_SETMASK, &old, NULL);

			if (ap.state == ST_SHUTDOWN)
				return -1;
		}

		if (fds[0].revents & POLLIN)
			return fullread(fd, pkt, sizeof(*pkt));
	}
}

static int is_path_stupid(const char *name)
{ 
	/* Returns 1 if the path contains a '*' or starts with a '.' */
	if (strchr(name,'*')){ /* Any * in the ldap search will match - at least for our servers - DO NOT WANT! */
		debug("%s: path: %s matches '*' - ignoring it",
		      __func__,name);
		return(1);
	}
	else if (name[0] == '.'){
		debug("%s: path: %s starts with a dot (.)  - ignoring it",
		      __func__,name);
		return(1);
	}
	else if (strstr(name,"automount(pid")){
		/* Samba has a habit of trying to mount up /mp/automount(pidNNNNN) - which of course fails */
		/* It does this because it sees autmount(pidNNNN) /mp autofs etc etc in /etc/mtab and tries to get quota info for it */
		/* I know I should fix samba but this is easier  for now */
		debug("%s: path: %s contains 'automount(pid' - ignoring it",
		      __func__,name);
		return(1);

		
	}
	else {
		return(0);
	}
}

static int handle_packet_missing(const struct autofs_packet_missing *pkt)
{
	struct stat st;
	sigset_t oldsig;
	pid_t f;
	struct pending_mount *mt = NULL;

	debug("handle_packet_missing: token %ld, mp %s name %s\n",
	      (unsigned long) pkt->wait_queue_token, ap.path, pkt->name);

	/* Ignore packet if we're trying to shut down */
	if (ap.state == ST_SHUTDOWN_PENDING || ap.state == ST_SHUTDOWN) {
		send_fail(pkt->wait_queue_token);
		return 0;
	}

	/* 
	   Optionally ignore stupid path names.
	   This will prevent silly ldap lookups like '*' which will mount up the first
	   thing found. And MACS and PC's love .something directories. We NEVER create at the automount level these 
	   and they waste time as automounter forks just to find out the path doesn't exist.
	*/

	if (ap.ignore_stupid_paths && is_path_stupid(pkt->name) ){
		send_fail(pkt->wait_queue_token);
		return 1;
	}

	chdir(ap.path);
	if (lstat(pkt->name, &st) == -1 ||
	   (S_ISDIR(st.st_mode) && st.st_dev == ap.dev)) {
		/* Need to mount or symlink */
		char buf[PATH_MAX + 1];
		int size;

		chdir("/");
		/* Block SIGCHLD while mucking with linked lists */
		sigprocmask(SIG_BLOCK, &sigchld_mask, NULL);
		if ((mt = (struct pending_mount *) junk_mounts)) {
			junk_mounts = junk_mounts->next;
		} else {
			if (!(mt = malloc(sizeof(struct pending_mount)))) {
				error("handle_packet_missing: malloc: %m");
				send_fail(pkt->wait_queue_token);
				return 1;
			}
		}
		sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);

		size = ncat_path(buf, sizeof(buf),
				 ap.path, pkt->name, pkt->len);
		if (!size) {
			crit("handle_packet_missing: "
			     "path to be mounted is to long");

			send_fail(pkt->wait_queue_token);
			free(mt);

			return 0;
		}

		info("attempting to mount entry %s", buf);

		sigprocmask(SIG_BLOCK, &lock_sigs, &oldsig);

		f = fork();
		if (f == -1) {
			sigprocmask(SIG_SETMASK, &oldsig, NULL);
			error("handle_packet_missing: fork: %m");

			send_fail(pkt->wait_queue_token);
			free(mt);

			return 1;
		} else if (!f) {
			int err;

			/* Set up a sensible signal environment */
			ignore_signals();
			close(ap.pipefd);
			close(ap.ioctlfd);
			close(ap.state_pipe[0]);
			close(ap.state_pipe[1]);

			chdir(ap.path);
			err = ap.lookup->lookup_mount(ap.path,
						      pkt->name, pkt->len,
						      ap.lookup->context);
			chdir("/");

			/*
			 * If at first you don't succeed, hide all
			 * evidence you ever tried
			 */
			if (err) {
				error("failed to mount %s", buf);
				umount_multi(buf, 1);
/*				if ((!ap.ghost) ||
				    (ap.state == ST_SHUTDOWN_PENDING
				     || ap.state == ST_SHUTDOWN))
					rm_unwanted(buf, 1, 0); */
			}

			_exit(err ? 1 : 0);
		} else {
			/*
			 * Important: set up data structures while signals
			 * still blocked
			 */
			mt->pid = f;
			mt->wait_queue_token = pkt->wait_queue_token;
			mt->next = ap.mounts;
			ap.mounts = mt;

			sigprocmask(SIG_SETMASK, &oldsig, NULL);
		}
	} else {
		/*
		 * Already there (can happen if a process connects to a
		 * directory while we're still working on it)
		 */
		/*
		 * XXX For v4, this would be the wrong thing to do if it could
		 * happen. It should add the new wait_queue_token to the pending
		 * mount structure so that it gets sent a ready when its really
		 * done.  In practice, the kernel keeps any other processes
		 * blocked until the initial mount request is done. -JSGF
		 */
		send_ready(pkt->wait_queue_token);
	}

	chdir("/");

	return 0;
}

static void do_expire(const char *name, int namelen)
{
	char buf[PATH_MAX + 1];
	int len;

	len = ncat_path(buf, sizeof(buf), ap.path, name, namelen);
	if (!len) {
		crit("do_expire: path to long for buffer");
		return;
	}

	debug("expiring path %s", buf);

	if (umount_multi(buf, 1) == 0) {
		info("expired %s", buf);
	} else {
		int ret;

		/* Oops - umounted some things, but not all; try and
		   recover before anyone notices by remounting
		   everything.

		   This should never happen because the kernel checks
		   whether the umount will work before telling us about
		   it.
		 */

		chdir(ap.path);
		ret = ap.lookup->lookup_mount(ap.path, 
					name, namelen, ap.lookup->context);
		chdir("/");

		if (ret)
			error("failed to recover from partial expiry of %s\n",
			       buf);
	}
}

static int handle_expire(const char *name, int namelen, autofs_wqt_t token)
{
	sigset_t olds;
	pid_t f;
	struct pending_mount *mt = NULL;

	chdir("/");		/* make sure we're out of the way */

	/* Temporarily block SIGCHLD and SIGALRM between forking and setting
	   pending (u)mount info */

	sigprocmask(SIG_BLOCK, &lock_sigs, &olds);

	/* Reclaim from doomed list if there is one */
	if ((mt = (struct pending_mount *) junk_mounts)) {
		junk_mounts = junk_mounts->next;
	} else {
		if (!(mt = malloc(sizeof(struct pending_mount)))) {
			sigprocmask(SIG_SETMASK, &olds, NULL);
			error("handle_expire: malloc: %m");
			return 1;
		}
	}

	f = fork();
	if (f == -1) {
		sigprocmask(SIG_SETMASK, &olds, NULL);
		error("handle_expire: fork: %m");
		free(mt);

		return 1;
	}
	if (f > 0) {
		mt->pid = f;
		mt->wait_queue_token = token;
		mt->next = ap.mounts;
		ap.mounts = mt;

		sigprocmask(SIG_SETMASK, &olds, NULL);

		return 0;
	}

	/* This is the actual expire run, run as a subprocess */

	ignore_signals();
	close(ap.pipefd);
	close(ap.ioctlfd);
	close(ap.state_pipe[0]);
	close(ap.state_pipe[1]);

	do_expire(name, namelen);

	exit(0);
}

static int handle_packet_expire(const struct autofs_packet_expire *pkt)
{
	return handle_expire(pkt->name, pkt->len, 0);
}

static int handle_packet_expire_multi(const struct autofs_packet_expire_multi *pkt)
{
	int ret;

	debug("handle_packet_expire_multi: token %ld, name %s\n",
		  (unsigned long) pkt->wait_queue_token, pkt->name);

	ret = handle_expire(pkt->name, pkt->len, pkt->wait_queue_token);

	if (ret != 0)
		send_fail(pkt->wait_queue_token);
	return ret;
}

static int handle_packet(void)
{
	union autofs_packet_union pkt;

	if (get_pkt(ap.pipefd, &pkt))
		return -1;

	debug("handle_packet: type = %d\n", pkt.hdr.type);

	switch (pkt.hdr.type) {
	case autofs_ptype_missing:
		return handle_packet_missing(&pkt.missing);

	case autofs_ptype_expire:
		return handle_packet_expire(&pkt.expire);

	case autofs_ptype_expire_multi:
		return handle_packet_expire_multi(&pkt.expire_multi);
	}
	error("handle_packet: unknown packet type %d\n", pkt.hdr.type);
	return -1;
}

static void become_daemon(void)
{
	FILE *pidfp;
	pid_t pid;
	int nullfd;

	/* Don't BUSY any directories unnecessarily */
	chdir("/");

	/* Detach from foreground process */
	if (!submount) {
		pid = fork();
		if (pid > 0)
			exit(0);
		else if (pid < 0) {
			fprintf(stderr, "%s: Could not detach process\n",
				program);
			exit(1);
		}
	}

	/* Open syslog */
	openlog("automount", LOG_PID, LOG_DAEMON);

	/* Initialize global data */
	my_pid = getpid();

	/*
	 * Make our own process group for "magic" reason: processes that share
	 * our pgrp see the raw filesystem behind the magic.  So if we are a 
	 * submount, don't change -- otherwise we won't be able to actually
	 * perform the mount.  A pgrp is also useful for controlling all the
	 * child processes we generate. 
	 *
	 * IMK: we now use setsid instead of setpgrp so that we also disassociate
	 * ouselves from the controling tty. This ensures we don't get unexpected
	 * signals. This call also sets us as the process group leader.
	 */
	if (!submount && (setsid() == -1)) {
		crit("setsid: %m");
		exit(1);
	}
	my_pgrp = getpgrp();

	/* Redirect all our file descriptors to /dev/null */
	if ((nullfd = open("/dev/null", O_RDWR)) < 0) {
		crit("cannot open /dev/null: %m");
		exit(1);
	}

	if (dup2(nullfd, STDIN_FILENO) < 0 ||
	    dup2(nullfd, STDOUT_FILENO) < 0 || dup2(nullfd, STDERR_FILENO) < 0) {
		crit("redirecting file descriptors failed: %m");
		exit(1);
	}
	close(nullfd);

	/* Write pid file if requested */
	if (pid_file) {
		if ((pidfp = fopen(pid_file, "wt"))) {
			fprintf(pidfp, "%lu\n", (unsigned long) my_pid);
			fclose(pidfp);
		} else {
			warn("failed to write pid file %s: %m", pid_file);
			pid_file = NULL;
		}
	}
}

/*
 * cleanup_exit() is valid to call once we have daemonized
 */

static void cleanup_exit(const char *path, int exit_code)
{
	if (ap.lookup)
		close_lookup(ap.lookup);

	if (pid_file)
		unlink(pid_file);

	closelog();

	if ((!ap.ghost || !submount) && (*(path + 1) != '-') && ap.dir_created)
		if (rmdir(path) == -1)
			warn("failed to remove dir %s: %m", path);

	exit(exit_code);
}

static unsigned long getnumopt(char *str, char option)
{
	unsigned long val;
	char *end;

	val = strtoul(str, &end, 0);
	if (!*str || *end) {
		fprintf(stderr,
			"%s: option -%c requires a numeric argument, got %s\n",
			program, option, str);
		exit(1);
	}
	return val;
}

static void usage(void)
{
	fprintf(stderr, "Usage: %s [options] path map_type [args...]\n", program);
	fprintf(stderr, "   -D|--dumpmap dumps out the maps read and exits\n");
	fprintf(stderr, "   -r|--random-multimount-selection  randomly selects a multimount server rather than testing each one for performance\n");
	fprintf(stderr, "   -u|--use-old-ldap-lookup instead of figuring out the schema once do it every single time a mount is requested. This is the old behaviour\n");
 	fprintf(stderr, "   -I|--ignore-stupid-paths will never lookup a requested path which contains the * character or which starts with a dot (.) \n");
 	fprintf(stderr, "   -R|--max-nfs-mount-retries <n> and -P|--nfs-mount-retry-pause <max secs> retres nfs mounts when certain error messages are seen. Default is no retry. pause is max seconds to wait (the pause is random from 1 to (pause+1) seconds\n");
}

static void setup_signals(__sighandler_t event_handler, __sighandler_t cld_handler)
{
	struct sigaction sa;

	if (event_handler == NULL)
		return;

	/* Signals which are only used in ST_READY state */
	sigemptyset(&ready_sigs);
	sigaddset(&ready_sigs, SIGUSR1);
	sigaddset(&ready_sigs, SIGUSR2);
	sigaddset(&ready_sigs, SIGTERM);
	sigaddset(&ready_sigs, SIGALRM);
	sigaddset(&ready_sigs, SIGHUP);

	/* Signals which are blocked to do locking */
	memcpy(&lock_sigs, &ready_sigs, sizeof(lock_sigs));
	sigaddset(&lock_sigs, SIGCHLD);

	sigemptyset(&sigchld_mask);
	sigaddset(&sigchld_mask, SIGCHLD);


	/* The following signals cause state transitions */
	sa.sa_handler = event_handler;
	memcpy(&sa.sa_mask, &ready_sigs, sizeof(sa.sa_mask));
	sa.sa_flags = SA_RESTART;

	/* SIGTERM and SIGUSR2 are synonymous */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	/* The SIGALRM handler controls expiration of entries. */
	sigaction(SIGALRM, &sa, NULL);

	/* SIGUSR1 causes a prune event */
	sigaction(SIGUSR1, &sa, NULL);

	/* SIGHUP causes a reread of map */
	sigaction(SIGHUP, &sa, NULL);

	/* The following signals cause a shutdown event to occur, but if we
	   get more than one, permit the signal to proceed so we don't loop.
	   This is basically the complete list of "this shouldn't happen"
	   signals. */
	sa.sa_flags = SA_ONESHOT | SA_RESTART;
	sigaction(SIGIO, &sa, NULL);
	sigaction(SIGXCPU, &sa, NULL);
	sigaction(SIGXFSZ, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
#ifndef DEBUG
	/* When debugging, these signals should be in the default state; when
	   in production, we want to at least attempt to catch them and shut down. */
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGTRAP, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGPROF, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);
#ifdef SIGSYS
	sigaction(SIGSYS, &sa, NULL);
#endif
#ifdef SIGSTKFLT
	sigaction(SIGSTKFLT, &sa, NULL);
#endif
#ifdef SIGLOST
	sigaction(SIGLOST, &sa, NULL);
#endif
#ifdef SIGEMT
	sigaction(SIGEMT, &sa, NULL);
#endif
#endif				/* DEBUG */

	if (cld_handler != NULL) {
		/* The SIGCHLD handler causes state transitions as
		 * processes exit (expire and mount) */
		sa.sa_handler = cld_handler;
		memcpy(&sa.sa_mask, &lock_sigs, sizeof(sa.sa_mask));
		/* Don't need info about stopped children */
		sa.sa_flags = SA_NOCLDSTOP;
		sigaction(SIGCHLD, &sa, NULL);
	}

	/* The following signals shouldn't occur, and are ignored */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGVTALRM, &sa, NULL);
	sigaction(SIGURG, &sa, NULL);
	sigaction(SIGWINCH, &sa, NULL);
#ifdef SIGPWR
	sigaction(SIGPWR, &sa, NULL);
#endif
#ifdef SIGUNUSED
	sigaction(SIGUNUSED, &sa, NULL);
#endif
}

/* Deal with the signals recieved by direct mount supervisor */
static void sig_supervisor(int sig)
{
	static struct syslog_data syslog_context = AUTOFS_SYSLOG_CONTEXT;
	static struct syslog_data *slc = &syslog_context;
	int save_errno = errno;

	switch (sig) {
	default:		/* all the signals not handled */
		error_r(slc, "process %d got unexpected signal %d!",
			getpid(), sig);
		return;
		/* don't FALLTHROUGH */

	case SIGTERM:
	case SIGUSR2:
		/* Tell everyone to finish up */
		signal_children(sig);
		break;

	case SIGUSR1:
		/* Pass on the prune event and ignore self signal */
		signal_children(sig);
		break;

	case SIGCHLD:
		wait(NULL);
		break;

	case SIGHUP:
		ap.lookup->lookup_ghost(ap.path, ap.ghost, 0, ap.lookup->context);

		/* Pass on the reread event and ignore self signal */
		kill(0, SIGHUP);
		discard_pending(SIGHUP);

		break;
	}
	errno = save_errno;
}

int supervisor(char *path)
{
	unsigned int map = 0;

	ap.path = alloca(strlen(path) + 1);
	strcpy(ap.path, path);

	map = ap.lookup->lookup_ghost(ap.path, ap.ghost, 0, ap.lookup->context);
	if (map & LKP_FAIL) {
		error("failed to load map exiting");
		cleanup_exit(ap.path, 1);
	} else if (map & LKP_INDIRECT) {
		error("bad map format: found indirect, expected direct exiting");
		cleanup_exit(ap.path, 1);
	}

	setup_signals(sig_supervisor, sig_supervisor);

	while (waitpid(0, NULL, 0) > 0);

	return 0;
}

int handle_mounts(char *path)
{
	unsigned int map = 0;

	setup_signals(sig_statemachine, sig_child);

	if (mount_autofs(path) < 0) {
		crit("%s: mount failed!", path);
		cleanup_exit(path, 1);
	}

	/* If this ioctl() doesn't work, it is kernel version 2 */
	if (!ioctl(ap.ioctlfd, AUTOFS_IOC_PROTOVER, &kproto_version)) {
		/* If this ioctl() doesn't work, kernel does not support ghosting */
		if (ioctl(ap.ioctlfd, AUTOFS_IOC_PROTOSUBVER, &kproto_sub_version)) {
			debug("kproto sub: %m");
			kproto_sub_version = 0;
			if (ap.ghost) {
				ap.ghost = 0;
				info("kernel does not support ghosting, disabled");
			}
		}
	} else {
		debug("kproto: %m");
		kproto_version = 2;
	}

	info("using kernel protocol version %d.%02d",
	       kproto_version, kproto_sub_version);

	if (kproto_version < 3) {
		ap.exp_timeout = ap.exp_runfreq = 0;
		ap.ghost = 0;
		info("kernel does not support timeouts");
	} else {
		time_t timeout;

		ap.exp_runfreq = (ap.exp_timeout + CHECK_RATIO - 1) / CHECK_RATIO;

		timeout = ap.exp_timeout;

		info("using timeout %d seconds; freq %d secs",
		       (int) ap.exp_timeout, (int) ap.exp_runfreq);

		ioctl(ap.ioctlfd, AUTOFS_IOC_SETTIMEOUT, &timeout);

		/* We often start several automounters at the same time.  Add some
		   randomness so we don't all expire at the same time. */
		if (ap.exp_timeout)
			alarm(ap.exp_runfreq + my_pid % ap.exp_runfreq);
	}

	map = ap.lookup->lookup_ghost(ap.path, ap.ghost, 0, ap.lookup->context);
	if (map & LKP_FAIL) {
		if (map & LKP_INDIRECT) {
			error("bad map format: found indirect, "
			      "expected direct exiting");
		} else {
			error("failed to load map, exiting");
		}
		rm_unwanted(ap.path, 1, 1);
		umount_autofs(1);
		cleanup_exit(path, 1);
	}

	if (map & LKP_DIRECT) {
		char *slash;

		/* Turn into normal automount if top level direct map */
		slash = strchr(ap.path + 1, '/');
		if (!slash) {
			if (submount) {
				submount = 0;
			} else {
				error("bad map format: found direct, "
				      "expected indirect exiting");
				rm_unwanted(ap.path, 1, 1);
				umount_autofs(1);
				cleanup_exit(path, 1);
			}
		}
		ap.type = LKP_DIRECT;
	}

	if (map & LKP_WILD) {
		error("cannot ghost wildcard map key");
	}

	if (map & LKP_NOTSUP)
		ap.ghost = 0;

	if (ap.ghost)
		info("ghosting enabled");

	/* Initialization successful.  If we're a submount, send outselves
	   SIGSTOP to let our parent know that we have grown up and don't
	   need supervision anymore. */
	if (submount || map & LKP_DIRECT)
		kill(my_pid, SIGSTOP);

	while (ap.state != ST_SHUTDOWN) {
		handle_packet();
	}
	debug("Shutting down - ap.state is %d if it's not %d (ST_SHUTDOWN) something bad happened ",ap.state,ST_SHUTDOWN);

	/* Mop up remaining kids */
	handle_child(1);

	/* Close down */
	umount_autofs(1);

	return 0;
}

int main(int argc, char *argv[])
{
	char *path, *map, *mapfmt;
	const char **mapargv;
	int mapargc, opt;
	static const struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"pid-file", 1, 0, 'p'},
		{"timeout", 1, 0, 't'},
		{"verbose", 0, 0, 'v'},
		{"debug", 0, 0, 'd'},
		{"version", 0, 0, 'V'},
		{"ghost", 0, 0, 'g'},
		{"submount", 0, &submount, 1},
		{"dumpmap", 0, 0, 'D'},
		{"random-multimount-selection", 0, 0, 'r'},
		{"use-old-ldap-lookup", 0, 0, 'u'},
		{"ignore-stupid-paths", 0, 0, 'I'},
		{"max-nfs-mount-retries", 1, 0, 'R'},
		{"nfs-mount-retry-pause", 1, 0, 'P'}, /* This is in fact the maximum pause - 1s (ie the code will randomly sleep between 1 and retry-pause +1 seconds) */
		{0, 0, 0, 0}
	};

	program = argv[0];

	memset(&ap, 0, sizeof ap);	/* Initialize ap so we can test for null */
	ap.exp_timeout = DEFAULT_TIMEOUT;
	ap.ghost = DEFAULT_GHOST_MODE;
	ap.type = LKP_INDIRECT;
	ap.dir_created = 0; /* We haven't created the main directory yet */
 

	opterr = 0;
	while ((opt = getopt_long(argc, argv, "+hp:t:vdVgDruIR:P:", long_options, NULL)) != EOF) {
		switch (opt) {
		case 'h':
			usage();
			exit(0);

		case 'p':
			pid_file = optarg;
			break;

		case 't':
			ap.exp_timeout = getnumopt(optarg, opt);
			break;

		case 'v':
			do_verbose = 1;
			break;

		case 'd':
			do_debug = 1;
			break;

		case 'V':
			printf("Linux automount version %s\n", version);
			exit(0);

		case 'g':
			ap.ghost = LKP_GHOST;
			break;

		case 'D':
			dumpmap = 1;
			break;
		case 'r':
			ap.random_multimount = 1;
			break;
		case 'u':
			ap.use_old_ldap_lookup = 1;
			break;
		case 'I':
			ap.ignore_stupid_paths = 1;
			break;

		case 'R':
			ap.max_nfs_mount_retries =  getnumopt(optarg, opt);
			break;

		case 'P':
			ap.nfs_mount_retry_pause =  getnumopt(optarg, opt);
			break;

		case '?':
		case ':':
			printf("%s: Ambiguous or unknown options\n", program);
			exit(1);
		}
	}
	/* Set this to a sane value even if it isn't used */
	if (ap.nfs_mount_retry_pause <= 0){
		ap.nfs_mount_retry_pause = 1;
	}
	if (geteuid() != 0) {
		fprintf(stderr, "%s: This program must be run by root.\n", program);
		exit(1);
	}

	/* Remove the options */
	argv += optind;
	argc -= optind;

	if (argc < 2) {
		usage();
		exit(1);
	}

	if (!dumpmap)
		become_daemon();

	path = argv[0];
	map = argv[1];
	mapargv = (const char **) &argv[2];
	mapargc = argc - 2;

	info("starting automounter version %s, path = %s, "
	       "maptype = %s, mapname = %s", version, path, map,
	       (mapargc < 1) ? "none" : mapargv[0]);

#ifdef DEBUG
	if (mapargc) {
		int i;
		syslog(LOG_DEBUG, "Map argc = %d", mapargc);
		for (i = 0; i < mapargc; i++)
			syslog(LOG_DEBUG, "Map argv[%d] = %s", i, mapargv[i]);
	}
#endif

	if ((mapfmt = strchr(map, ',')))
		*(mapfmt++) = '\0';

	ap.maptype = map;

	if (!(ap.lookup = open_lookup(map, "", mapfmt, mapargc, mapargv)))
		cleanup_exit(path, 1);

	if (dumpmap) {
		int ret;
		ret = ap.lookup->lookup_ghost(ap.path, ap.ghost,
					      0, ap.lookup->context);
		if (ret & LKP_FAIL)
			exit(ret);
		exit(0);
	}

	if (!strncmp(path, "/-", 2)) {
		supervisor(path);
	} else {
		handle_mounts(path);
	}
	info("shut down, path = %s", path);
	cleanup_exit(path, 0);
	exit(0);
}
