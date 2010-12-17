#ident "$Id: lock.c,v 1.15 2005/01/17 15:09:28 raven Exp $"
/* ----------------------------------------------------------------------- *
 *
 *  lock.c - autofs lockfile management
 *
 *   Copyright 2004 Ian Kent <raven@themaw.net>
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
 *   This code has adapted from that found in mount/fstab.c of the
 *   util-linux package.
 *
 * ----------------------------------------------------------------------- */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <alloca.h>
#include <stdio.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>

#include "automount.h"

static void setup_locksigs(void);
static void reset_locksigs(void);

/*
 * If waiting for 30 secs is not enough then there's
 * probably no good the requestor continuing anyway?
 */
/* LOCK_TIMEOUT = WAIT_INTERVAL/10**9 * WAIT_TRIES */
#define WAIT_INTERVAL	100000000L
#define WAIT_TRIES	300
#define LOCK_RETRIES    3
#define MAX_PIDSIZE	20

#define LOCK_FILE     AUTOFS_LOCK

/* Flag for already existing lock file. */
static int we_created_lockfile = 0;

/* Flag to indicate that signals have been set up. */
static int signals_have_been_setup = 0;

/* Save previous actions */
static struct sigaction actions[NSIG];
static struct sigaction sigusr1;
static struct sigaction sigusr2;

/* Flag to identify we got a TERM signal */
static int got_term = 0;

/* file descriptor of lock file */
static int fd = -1;

/* Ignore all signals except for SIGTERM */
static void handler(int sig)
{
	/* 
	 * We need ignore most signals as there are quite a few sent
	 * during shutdown. We must continue to wait for the lock.
	 * We should use the USR2 signal for normal operation
	 * and only use a TERM signal to shutdown harshly.
	 */
	if (sig == SIGQUIT || sig == SIGTERM || sig == SIGINT)
		got_term = 1;
}

static int lock_is_owned(int fd)
{
	int pid = 0, tries = 3;

	while (tries--) {
		char pidbuf[MAX_PIDSIZE + 1];
		int got;

		lseek(fd, 0, SEEK_SET);
		got = read(fd, pidbuf, MAX_PIDSIZE);
		/*
		 * We add a terminator to the pid to verify write complete.
		 * If the write isn't finish in 300 milliseconds then it's
		 * probably a stale lock file.
		 */
		if (got > 0 && pidbuf[got - 1] == '\n') {
			sscanf(pidbuf, "%d", &pid);
			break;
		} else {
			struct timespec t = { 0, 100000000 };
			struct timespec r;

			while (nanosleep(&t, &r) == -1 && errno == EINTR) {
				/* So we can exit quickly, return owned */
				if (got_term)
					return 1;
				memcpy(&t, &r, sizeof(struct timespec));
			}
			continue;
		}

		/* Stale lockfile */
		if (!tries)
			return 0;
	}


	if (pid) {
		int ret;

		ret = kill(pid, SIGCONT);
		/* 
		 * If lock file exists but is not owned by a process
		 * we return unowned status so we can get rid of it
		 * and continue.
		 */
		if (ret == -1 && errno == ESRCH)
			return 0;
	} else {
		/*
		 * Odd, no pid in file - so what should we do?
		 * Assume something bad happened to owner and
		 * return unowned status.
		 */
		return 0;
	}

	return 1;
}

static void setup_locksigs(void)
{
	int sig = 0;
	struct sigaction sa;

	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sigfillset(&sa.sa_mask);

	sigprocmask(SIG_BLOCK, &sa.sa_mask, NULL);

	while (sigismember(&sa.sa_mask, ++sig) != -1
			&& sig != SIGCHLD) {
		sigaction(sig, &sa, &actions[sig]);
	}

	sigaction(SIGUSR1, &sa, &sigusr1);
	sigaction(SIGUSR2, &sa, &sigusr2);

	signals_have_been_setup = 1;
	sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL);
}

static void reset_locksigs(void)
{
	int sig = 0;
	sigset_t fullset;
	
	sigfillset(&fullset);
	sigprocmask(SIG_BLOCK, &fullset, NULL);

	sigaction(SIGUSR1, &sigusr1, NULL);
	sigaction(SIGUSR2, &sigusr2, NULL);

	while (sigismember(&fullset, ++sig) != -1
			&& sig != SIGCHLD) {
		sigaction(sig, &actions[sig], NULL);
	}

	signals_have_been_setup = 0;
	sigprocmask(SIG_UNBLOCK, &fullset, NULL);
}

/* Remove lock file. */
void release_lock(void)
{
	if (fd > 0) {
		close(fd);
		fd = -1;
	}

	if (we_created_lockfile) {
		unlink (LOCK_FILE);
		we_created_lockfile = 0;
	}

	if (signals_have_been_setup)
		reset_locksigs();
}

/*
 * Wait for a lock file to be removed.
 * Return -1 for a timeout, 0 if a termination signal
 * is received or 1 for success.
 */
static int wait_for_lockf(const char *lockf)
{
	int tries = WAIT_TRIES;
	int status = 0;
	struct stat st;

	while (tries-- && !status) {
		status = stat(lockf, &st);
		if (!status) {
			struct timespec t = { 0, WAIT_INTERVAL };
			struct timespec r;

			while (nanosleep(&t, &r) == -1 && errno == EINTR) {
				if (got_term)
					return 0;
				memcpy(&t, &r, sizeof(struct timespec));
			}
		}
	}

	if (tries < 0)
		return tries;

	return 1;
}

/*
 * Aquire lock file taking account of autofs signals.
 */
int aquire_lock(void)
{
	int tries = 3;
	char *linkf;
	int len;

	if (!signals_have_been_setup)
		setup_locksigs();

	len = strlen(LOCK_FILE) + MAX_PIDSIZE;
	linkf = alloca(len + 1);
	snprintf(linkf, len, "%s.%d", LOCK_FILE, getpid());

	/* Repeat until it was us who made the link */
	while (!we_created_lockfile) {
		int errsv, i, j;

		i = open(linkf, O_WRONLY|O_CREAT, 0);
		if (i < 0) {
			release_lock();
			return 0;
		}
		close(i);

		j = link(linkf, LOCK_FILE);
		errsv = errno;

		(void) unlink(linkf);

		if (j < 0 && errsv != EEXIST) {
			release_lock();
			return 0;
		}

		fd = open(LOCK_FILE, O_RDWR);
		if (fd < 0) {
			/* Maybe the file was just deleted? */
			if (errno == ENOENT && tries-- > 0)
				continue;
			release_lock();
			return 0;
		}

		if (j == 0) {
			char pidbuf[MAX_PIDSIZE + 1];
			int pidlen;

			pidlen = sprintf(pidbuf, "%d\n", getpid());
			write(fd, pidbuf, pidlen);

			we_created_lockfile = 1;
		} else {
			int status;
			char mess[128] =
				"aquire_lock: can't lock lock file %s: %s";

			/*
			 * Someone else made the link.
			 * If the lock file is not owned by anyone
			 * clean it up and try again, otherwise we
			 * wait.
			 */
			if (!lock_is_owned(fd)) {
				close(fd);
				fd = -1;
				unlink(LOCK_FILE);
				continue;
			}

			status = wait_for_lockf(LOCK_FILE);
			if (status < 0) {
				release_lock();
				crit(mess, "timed out", LOCK_FILE);
				return 0;
			} else if (!status) {
				release_lock();
				crit(mess, "interrupted", LOCK_FILE);
				return 0;
			}
		}

		if (got_term) {
			got_term = 0;
			release_lock();
			return 0;
		}
		close(fd);
		fd = -1;
	}

	reset_locksigs();
	return 1;
}

