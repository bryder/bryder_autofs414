#ident "$Id: lookup_userhome.c,v 1.4 2004/12/31 06:30:09 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  lookup_userhome.c - module for Linux automount to generate symlinks
 *                      to user home directories
 *
 *   Copyright 1999 Transmeta Corporation - All Rights Reserved
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
#include <pwd.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MODPREFIX "lookup(userhome): "

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	return 0;		/* Nothing to do */
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	return LKP_NOTSUP;
}

int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	struct passwd *pw;

	debug(MODPREFIX "looking up %s", name);

	/* Get the equivalent username */
	pw = getpwnam(name);
	if (!pw) {
		info(MODPREFIX "not found: %s", name);
		return 1;	/* Unknown user or error */
	}

	/* Create the appropriate symlink */
	if (chdir(root)) {
		error(MODPREFIX "chdir failed: %m");
		return 1;
	}

	if (symlink(pw->pw_dir, name) && errno != EEXIST) {
		error(MODPREFIX "symlink failed: %m");
		return 1;
	}

	return 0;
}

int lookup_done(void *context)
{
	return 0;
}
