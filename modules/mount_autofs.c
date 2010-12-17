#ident "$Id: mount_autofs.c,v 1.14 2005/01/10 13:28:29 raven Exp $"
/*
 * mount_autofs.c
 *
 * Module for recursive autofs mounts.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <alloca.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MODULE_MOUNT
#include "automount.h"

#define MODPREFIX "mount(autofs): "

int mount_version = AUTOFS_MOUNT_VERSION;	/* Required by protocol */

int mount_init(void **context)
{
	return 0;
}

int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *c_options,
		void *context)
{
	char *fullpath, **argv;
	int argc, status, ghost = ap.ghost;
	char *options, *p;
	pid_t slave, wp;
	char timeout_opt[30];

	fullpath = alloca(strlen(root) + name_len + 2);
	if (!fullpath) {
		error(MODPREFIX "alloca: %m");
		return 1;
	}
	sprintf(fullpath, "%s/%s", root, name);

	if (c_options) {
		options = alloca(strlen(c_options) + 1);
		if (!options) {
			error(MODPREFIX "alloca: %m");
			return 1;
		}
		strcpy(options, c_options);
	} else {
		options = NULL;
	}

	debug(MODPREFIX "calling mkdir_path %s", fullpath);

	if (mkdir_path(fullpath, 0555) && errno != EEXIST) {
		error(MODPREFIX "mkdir_path %s failed: %m", name);
		return 1;
	}

	debug(MODPREFIX "fullpath=%s what=%s options=%s",
		  fullpath, what, options);

	if (is_mounted(_PATH_MOUNTED, fullpath)) {
		error(MODPREFIX 
		 "warning: about to mount over %s, continuing", fullpath);
		return 0;
	}

	if (strstr(options, "browse")) {
		if (strstr(options, "nobrowse"))
			ghost = 0;
		else
			ghost = 1;
	}
	/* Build our argument vector.  */

	argc = 5;
	if (ghost)
		argc++;

	if (do_verbose || do_debug)
		argc++;

	if (ap.exp_timeout && ap.exp_timeout != DEFAULT_TIMEOUT) {
		argc++;
		sprintf(timeout_opt, "--timeout=%d", (int) ap.exp_timeout);
	}

	if (options) {
		char *p = options;
		do {
			argc++;
			if (*p == ',')
				p++;
		} while ((p = strchr(p, ',')) != NULL);
	}
	argv = (char **) alloca((argc + 1) * sizeof(char *));

	argc = 0;
	argv[argc++] = PATH_AUTOMOUNT;
	argv[argc++] = "--submount";

	if (ghost)
		argv[argc++] = "--ghost";

	if (ap.exp_timeout != DEFAULT_TIMEOUT)
		argv[argc++] = timeout_opt;

	if (do_debug)
		argv[argc++] = "--debug";
	else if (do_verbose)
		argv[argc++] = "--verbose";

	argv[argc++] = fullpath;
	argv[argc++] = strcpy(alloca(strlen(what) + 1), what);

	if ((p = strchr(argv[argc - 1], ':')) == NULL) {
		error(MODPREFIX "%s missing script type on %s", name, what);
		goto error;
	}

	*p++ = '\0';
	argv[argc++] = p;

	if (options) {
		/* Rainer Clasen reported funniness using strtok() here. */

		p = options;
		do {
			if (*p == ',') {
				*p = '\0';
				p++;
			}
			argv[argc++] = p;
		} while ((p = strchr(p, ',')) != NULL);
	}
	argv[argc] = NULL;

	/*
	 * Spawn a new daemon.  If initialization is successful,
	 * the daemon will send itself SIGSTOP, which we detect
	 * and let it go on its merry way.
	 */

	slave = fork();
	if (slave < 0) {
		error(MODPREFIX "fork: %m");
		goto error;
	} else if (slave == 0) {
		/* Slave process */
		execv(PATH_AUTOMOUNT, argv);
		_exit(255);
	}

	while ((wp = waitpid(slave, &status, WUNTRACED)) == -1 && errno == EINTR);
	if (wp != slave) {
		error(MODPREFIX "waitpid: %m");
		goto error;
	}

	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) {
		error(MODPREFIX "sub automount returned status 0x%x", status);
		goto error;
	}

	kill(slave, SIGCONT);	/* Carry on, private */

	debug(MODPREFIX "mounted %s on %s", what, fullpath);

	return 0;

      error:
	if (!ap.ghost)
		rmdir_path(fullpath);

	error(MODPREFIX "failed to mount %s on %s", what, fullpath);

	return 1;
}

int mount_done(void *context)
{
	return 0;
}
