#ident "$Id: mount_ext2.c,v 1.18 2005/01/10 13:28:29 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  mount_ext2.c - module for Linux automountd to mount ext2 filesystems
 *                 after running fsck on them.
 *
 *   Copyright 1998 Transmeta Corporation - All Rights Reserved
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

#define MODPREFIX "mount(ext2): "

int mount_version = AUTOFS_MOUNT_VERSION;	/* Required by protocol */

int mount_init(void **context)
{
	return 0;
}

int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *options, void *context)
{
	char *fullpath;
	const char *p, *p1;
	int err, ro = 0;
	const char *fsck_prog;
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
		error(MODPREFIX 
		  "warning: %s is already mounted", fullpath);
		return 0;
	}

	if (options && options[0]) {
		for (p = options; (p1 = strchr(p, ',')); p = p1)
			if (!strncmp(p, "ro", p1 - p) && ++p1 - p == sizeof("ro"))
				ro = 1;
		if (!strcmp(p, "ro"))
			ro = 1;
	}

#ifdef HAVE_E3FSCK
	if (!strcmp(fstype,"ext3") || !strcmp(fstype,"auto"))
		fsck_prog = PATH_E3FSCK;
	else
		fsck_prog = PATH_E2FSCK;
#else
	fsck_prog = PATH_E2FSCK;
#endif
	if (ro) {
		debug(MODPREFIX "calling %s -n %s", fsck_prog, what);
		err = spawnl(LOG_DEBUG, fsck_prog, fsck_prog, "-n", what, NULL);
	} else {
		debug(MODPREFIX "calling %s -p %s", fsck_prog, what);
		err = spawnl(LOG_DEBUG, fsck_prog, fsck_prog, "-p", what, NULL);
	}

	if (err & ~6) {
		error(MODPREFIX "%s: filesystem needs repair, won't mount",
		      what);
		return 1;
	}

	if (options) {
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
