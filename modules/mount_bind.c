#ident "$Id: mount_bind.c,v 1.15 2005/01/10 13:28:29 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  mount_bind.c      - module to mount a local filesystem if possible;
 *			otherwise create a symlink.
 *
 *   Copyright 2000 Transmeta Corporation - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MODULE_MOUNT
#include "automount.h"

#define MODPREFIX "mount(bind): "

int mount_version = AUTOFS_MOUNT_VERSION;	/* Required by protocol */

static int bind_works = 0;

int mount_init(void **context)
{
	char *tmp1 = tempnam(NULL, "auto");
	char *tmp2 = tempnam(NULL, "auto");
	int err;
	struct stat st1, st2;

	if (tmp1 == NULL || tmp2 == NULL) {
		if (tmp1)
			free(tmp1);
		if (tmp2)
			free(tmp2);
		return 0;
	}

	if (mkdir(tmp1, 0700) == -1)
		goto out2;

	if (mkdir(tmp2, 0700) == -1)
		goto out1;

	if (lstat(tmp1, &st1) == -1)
		goto out;

	err = spawnl(LOG_DEBUG,
	    	     PATH_MOUNT, PATH_MOUNT, "-n", "--bind", tmp1, tmp2, NULL);

	if (err == 0 &&
	    lstat(tmp2, &st2) == 0 &&
	    st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino) {
		bind_works = 1;
	}

	debug(MODPREFIX "bind_works = %d\n", bind_works);
	spawnl(LOG_DEBUG,
	       PATH_UMOUNT, PATH_UMOUNT, "-n", tmp2, NULL);

      out:
	rmdir(tmp2);
      out1:
	free(tmp2);
	rmdir(tmp1);
      out2:
	free(tmp1);
	return 0;
}

int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *options, void *context)
{
	char *fullpath;
	int err;
	int i;

	fullpath = alloca(strlen(root) + name_len + 2);
	if (!fullpath) {
		error(MODPREFIX "alloca: %m");
		return 1;
	}

	if (name_len)
		sprintf(fullpath, "%s/%s", root, name);
	else
		sprintf(fullpath, "%s", root);

	i = strlen(fullpath);
	while (--i > 0 && fullpath[i] == '/')
		fullpath[i] = '\0';

	if (options == NULL || *options == '\0')
		options = "defaults";

	if (bind_works) {
		int status, existed = 1;

		debug(MODPREFIX "calling mkdir_path %s", fullpath);

		status = mkdir_path(fullpath, 0555);
		if (status && errno != EEXIST) {
			error(MODPREFIX "mkdir_path %s failed: %m", fullpath);
			return 1;
		}

		if (!status)
			existed = 0;

		if (is_mounted(_PATH_MOUNTED, fullpath)) {
			error(MODPREFIX 
			  "warning: %s is already mounted", fullpath);
			return 0;
		}

		debug(MODPREFIX
		      "calling mount --bind " SLOPPY " -o %s %s %s",
		      options, what, fullpath);

		err = spawnll(LOG_NOTICE,
			     PATH_MOUNT, PATH_MOUNT, "--bind",
			     SLOPPYOPT "-o", options,
			     what, fullpath, NULL);

		if (err) {
			if ((!ap.ghost && name_len) || !existed)
				rmdir_path(name);
			return 1;
		} else {
			debug(MODPREFIX "mounted %s type %s on %s",
				  what, fstype, fullpath);
			return 0;
		}
	} else {
		char *cp;
		char *basepath = alloca(strlen(fullpath) + 1);
		int status;
		struct stat st;

		strcpy(basepath, fullpath);
		cp = strrchr(basepath, '/');

		if (cp != NULL && cp != basepath)
			*cp = '\0';

		if ((status = stat(fullpath, &st)) == 0) {
			if (S_ISDIR(st.st_mode))
				rmdir(fullpath);
		} else {
			debug(MODPREFIX "calling mkdir_path %s", basepath);
			if (mkdir_path(basepath, 0555) && errno != EEXIST) {
				error(MODPREFIX "mkdir_path %s failed: %m",
				      basepath);
				return 1;
			}
		}

		if (symlink(what, fullpath) && errno != EEXIST) {
			error(MODPREFIX
			      "failed to create local mount %s -> %s",
			      fullpath, what);
			if (ap.ghost && !status)
				mkdir_path(fullpath, 0555);
			else
				rmdir_path(fullpath);

			return 1;
		} else {
			debug(MODPREFIX "symlinked %s -> %s", fullpath, what);
			return 0;
		}
	}
}

int mount_done(void *context)
{
	return 0;
}
