#ident "$Id: mount_generic.c,v 1.15 2005/01/10 13:28:29 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  mount_generic.c - module for Linux automountd to mount filesystems
 *                    for which no special magic is required
 *
 *   Copyright 1997-1999 Transmeta Corporation - All Rights Reserved
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

#define MODPREFIX "mount(generic): "

int mount_version = AUTOFS_MOUNT_VERSION;	/* Required by protocol */

int mount_init(void **context)
{
	return 0;
}

int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *options,
		void *context)
{
	char *fullpath;
	int err;
	int status, existed = 1;

	fullpath = alloca(strlen(root) + name_len + 2);
	if (!fullpath) {
		error(MODPREFIX "alloca: %m");
		return 1;
	}

	if (name_len)
		sprintf(fullpath, "%s/%s", root, name);
	else
		sprintf(fullpath, "%s", root);

	debug(MODPREFIX "calling mkdir_path %s", fullpath);

	status = mkdir_path(fullpath, 0555);
	if (status && errno != EEXIST) {
		error(MODPREFIX "mkdir_path %s failed: %m", fullpath);
		return 1;
	}

	if (!status)
		existed = 0;

	if (is_mounted(_PATH_MOUNTED, fullpath)) {
		error(MODPREFIX "warning: %s is already mounted", fullpath);
		return 0;
	}

	if (options && options[0]) {
		debug(MODPREFIX "calling mount -t %s " SLOPPY "-o %s %s %s",
		      fstype, options, what, fullpath);

		err = spawnll(LOG_NOTICE,
			     PATH_MOUNT, PATH_MOUNT, "-t", fstype,
			     SLOPPYOPT "-o", options, what, fullpath, NULL);
	} else {
		debug(MODPREFIX "calling mount -t %s %s %s",
		      fstype, what, fullpath);
		err = spawnll(LOG_NOTICE,
			     PATH_MOUNT, PATH_MOUNT, "-t", fstype,
			     what, fullpath, NULL);
	}
	unlink(AUTOFS_LOCK);

	if (err) {
		if ((!ap.ghost && name_len) || !existed)
			rmdir_path(name);

		error(MODPREFIX "failed to mount %s (type %s) on %s",
		      what, fstype, fullpath);

		return 1;
	} else {
		debug(MODPREFIX "mounted %s type %s on %s",
		      what, fstype, fullpath);
		return 0;
	}
}

int mount_done(void *context)
{
	return 0;
}
