#ident "$Id: spawn.c,v 1.10 2005/02/10 12:56:53 raven Exp $"
/* ----------------------------------------------------------------------- *
 * 
 *  spawn.c - run programs synchronously with output redirected to syslog
 *   
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *   Copyright 2005 Ian Kent <raven@themaw.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "automount.h"

/* Make gcc happy */
pid_t getpgid(pid_t);

/*
 * Used by subprocesses which exec to avoid carrying over the main
 * daemon's rather weird signalling environment
 */
void reset_signals(void)
{
	struct sigaction sa;
	sigset_t allsignals;
	int i;

	sigfillset(&allsignals);
	sigprocmask(SIG_BLOCK, &allsignals, NULL);

	/* Discard all pending signals */
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	for (i = 1; i < NSIG; i++)
		sigaction(i, &sa, NULL);

	sa.sa_handler = SIG_DFL;

	for (i = 1; i < NSIG; i++)
		sigaction(i, &sa, NULL);

	/* Ignore the user signals that may be sent so that we
	 *  don't terminate execed program by mistake */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	sigprocmask(SIG_UNBLOCK, &allsignals, NULL);
}

/*
 * Used by subprocesses which don't exec to avoid carrying over the
 * main daemon's rather weird signalling environment.  Signals are
 * mostly ignored so that "/bin/kill -x automount" (where x is
 * typically SIGTERM or SIGUSR1) only affects the main process.
 */
void ignore_signals(void)
{
	struct sigaction sa;
	sigset_t allsignals;
	int i;

	sigfillset(&allsignals);
	sigprocmask(SIG_BLOCK, &allsignals, NULL);

	/* Discard all pending signals and ignore them */
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	for (i = 1; i < NSIG; i++)
		sigaction(i, &sa, NULL);

	/* Default handler for SIGCHLD so that waitpid() still works */
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, NULL);

	sigprocmask(SIG_UNBLOCK, &allsignals, NULL);
}

/* Throw away an unwanted pending signal */
void discard_pending(int sig)
{
	struct sigaction sa, oldsa;

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(sig, &sa, &oldsa);
	sigaction(sig, &oldsa, NULL);
}

/*
 * Process group signals each child in the process group the given signal.
 * For us we must signal deepest mount path to shortest to avoid mount
 * busy on exit.
 */
int signal_children(int sig)
{
	struct mnt_list *mnts = get_mnt_list(_PATH_MOUNTED, "/", 0);
	struct mnt_list *next;
	pid_t pgrp = getpgrp();
	int ret = -1;

	if (!mnts) {
		warn("signal_children: no mounts found");
		goto out;
	}

	info("signal_children: send %d to process group %d", sig, pgrp);

	next = mnts;
	while (next) {
		/* 30 * 100000000 ns = 5 secs */
		int tries = 30;
		int status;
		struct mnt_list *this = next;
		pid_t pid = this->pid;

		next = this->next;

		if (!pid)
			continue;

		/* Don't signal ourselves */
		if (pid == pgrp)
			continue;

		/* Only signal members of our process group */
		if (getpgid(pid) != pgrp)
			continue;

		if (strncmp(this->fs_type, "autofs", 6))
			continue;

		/* Gone in between */
		if (kill(pid, SIGCONT) == -1 && errno == ESRCH)
			continue;

		debug("signal_children: signal %s %d", this->path, pid);

		status = kill(pid, sig);
		if (status)
			goto out;

		while (tries--) {
			struct timespec t = { 0, 100000000L };
			struct timespec r;

		again:
			status = nanosleep(&t, &r);
			/* For a prune event delay a little and pass it on */
/*
			if (sig == SIGUSR1)
				break;
*/
			if (kill(pid, SIGCONT) == -1 && errno == ESRCH)
				break;

			if (status == -1 && errno == EINTR) {
				memcpy(&t, &r, sizeof(struct timespec));
				goto again;
			}
		}

		if (sig != SIGUSR1 && tries < 0) {
			warn("signal_children: "
			      "%d did nor exit - giving up.", pid);
			goto out;
		}
	}
	ret = 0;
out:
	free_mnt_list(mnts);
	return ret;
}

#define ERRBUFSIZ 2047		/* Max length of error string excl \0 */

/*
 * this is horrible. I need to evaluate the error from a failed mount request
 * to see if it's a retryably NFS error
 * But spawnv is called by spawn_mount and it's the only place that deals with the 
 * error string that was returned by mount.
 *
 * the smallest change therefore is to run any error messages through retry_error_p and set a 
 * gasp flag that says an error was returned that was retryable.
 *
 * This means I actually have nfs code in spawn.c which will never get past any maintainer because it 
 * breaks the whole idea of separate modules.
 *
 * However - this approach won't break anything that doesn't use it and I won't have to introduce new argumenst
 * and propogate them everywhere.
 *
 * how embarrassing.
 */

int found_retryable_error = 0  ; /* This is the variable we set. - it is defined as extern (obviously) in mount_nfs.c */


/* These are the errors that can occur on a overloaded or if too many mounts are started up at once on a client */
/* It's a substring match */
static char *retryable_errors[] = {
	"RPC: Remote system error - Connection refused", /* heavy fileserver load */
	"RPC: Timed out", /* heavy fileserver load */
	"RPC: Remote system error - Connection timed out", /* heavy fileserver load */
	"Input/output error", /* too many mounts starting at once on a client  - centos 2.6.18 */
	"can't read superblock", /* too many mounts starting at once on a client - 2.6.25.18 and others */ 
	"nfs bindresvport: Address already in use", /* too many mounts starting at once on a client - renderwall jul 2010 */
	"mount system call failed",  /* too many mounts starting at once on a client - kunbuntu_10_04 2.6.31.12-xxxx-20100212 */
	"server is down",  /* p0wned fileserver - seen on kubuntu 7 */
};


/*
 * retry_error_p returns a string which the error message matched if that error is retryable.
 * if it returns NULL then the error is not retryable. 
 */
char *retry_error_p(char *error_mesg) /* _p means predicate - is it's a test - old LISP  naming */
{ /* retry_error_p */
	int i;
	
	for (i = 0; i < (sizeof(retryable_errors)/sizeof(char *)) ; i++){
		if (strstr(error_mesg,retryable_errors[i]) != NULL){
			debug("spawn.c:%s: Found a retryable error %s", __func__, retryable_errors[i]) ;
			return(retryable_errors[i]);
		}
	}

	return NULL;
	  
} /* retry_error_p */

static int do_spawn(int logpri, int use_lock, const char *prog, const char *const *argv)
{
	pid_t f;
	int status, pipefd[2];
	char errbuf[ERRBUFSIZ + 1], *p, *sp;
	int errp, errn;
	sigset_t allsignals, tmpsig, oldsig;

	if (use_lock)
		if (!aquire_lock())
			return -1;

	sigfillset(&allsignals);
	sigprocmask(SIG_BLOCK, &allsignals, &oldsig);

	if (pipe(pipefd)) {
		if (use_lock)
			release_lock();
		sigprocmask(SIG_SETMASK, &oldsig, NULL);
		return -1;
	}

	f = fork();
	if (f == 0) {
		reset_signals();
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);

		execv(prog, (char *const *) argv);
		_exit(255);	/* execv() failed */
	} else {
		/* Careful here -- if we enable SIGCHLD yet we may not receive the
		   waitpid() at the end */

		tmpsig = oldsig;

		sigaddset(&tmpsig, SIGCHLD);
		sigprocmask(SIG_SETMASK, &tmpsig, NULL);

		close(pipefd[1]);

		if (f < 0) {
			close(pipefd[0]);
			if (use_lock)
				release_lock();
			sigprocmask(SIG_SETMASK, &oldsig, NULL);
			return -1;
		}

		found_retryable_error = 0;

		errp = 0;
		do {
			while ((errn =
				read(pipefd[0], errbuf + errp, ERRBUFSIZ - errp)) == -1
			       && errno == EINTR);

			if (errn > 0) {
				errp += errn;

				sp = errbuf;

				if((ap.max_nfs_mount_retries > 0) &&  retry_error_p(errbuf))
				   found_retryable_error = 1 ;

				while (errp && (p = memchr(sp, '\n', errp))) {
					*p++ = '\0';
					if (sp[0])	/* Don't output empty lines */
					  syslog(logpri, "%s 1 >> %s", __func__, sp);
					errp -= (p - sp);
					sp = p;
				}

				if (errp && sp != errbuf)
					memmove(errbuf, sp, errp);

				if (errp >= ERRBUFSIZ) {
					/* Line too long, split */
					errbuf[errp] = '\0';
					syslog(logpri, "%s 2 >> %s", __func__, errbuf);
					errp = 0;
				}
			}
		} while (errn > 0);
		close(pipefd[0]);

		if (errp > 0) {
			/* End of file without \n */
			errbuf[errp] = '\0';
			syslog(logpri, ">> %s", errbuf);
		}

		if (waitpid(f, &status, 0) != f)
			status = -1;	/* waitpid() failed */

		if (use_lock)
			release_lock();

		sigprocmask(SIG_SETMASK, &oldsig, NULL);

		return status;
	}
}

int spawnv(int logpri, const char *prog, const char *const *argv)
{
	return do_spawn(logpri, 0, prog, argv);
}

int spawnl(int logpri, const char *prog, ...)
{
	va_list arg;
	int argc;
	char **argv, **p;

	va_start(arg, prog);
	for (argc = 1; va_arg(arg, char *); argc++);
	va_end(arg);

	if (!(argv = alloca(sizeof(char *) * argc)))
		return -1;

	va_start(arg, prog);
	p = argv;
	while ((*p++ = va_arg(arg, char *)));
	va_end(arg);

	return do_spawn(logpri, 0, prog, (const char **) argv);
}

#ifdef ENABLE_MOUNT_LOCKING
int spawnll(int logpri, const char *prog, ...)
{
	va_list arg;
	int argc;
	char **argv, **p;

	va_start(arg, prog);
	for (argc = 1; va_arg(arg, char *); argc++);
	va_end(arg);

	if (!(argv = alloca(sizeof(char *) * argc)))
		return -1;

	va_start(arg, prog);
	p = argv;
	while ((*p++ = va_arg(arg, char *)));
	va_end(arg);

	return do_spawn(logpri, 1, prog, (const char **) argv);
}
#endif
