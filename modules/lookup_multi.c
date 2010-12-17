#ident "$Id: lookup_multi.c,v 1.7 2005/02/10 12:31:29 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  lookup_multi.c - module for Linux automount to seek multiple lookup
 *                   methods in succession
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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MODPREFIX "lookup(multi): "

struct module_info {
	int argc;
	const char *const *argv;
	struct lookup_mod *mod;
};

struct lookup_context {
	int n;
	const char **argl;
	struct module_info *m;
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

int lookup_init(const char *my_mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt;
	char *map, *mapfmt;
	int i, j, an;

	if (!(*context = ctxt = malloc(sizeof(struct lookup_context))))
		goto nomem;

	memset(ctxt, 0, sizeof(struct lookup_context));

	if (argc < 1) {
		crit(MODPREFIX "No map list");
		return 1;
	}

	ctxt->n = 1;				/* Always at least one map */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--"))	/* -- separates maps */
			ctxt->n++;
	}

	if (!(ctxt->m = malloc(ctxt->n * sizeof(struct module_info))) ||
	    !(ctxt->argl = malloc((argc + 1) * sizeof(const char *))))
		goto nomem;

	memset(ctxt->m, 0, ctxt->n * sizeof(struct module_info));

	memcpy(ctxt->argl, argv, (argc + 1) * sizeof(const char *));

	for (i = j = an = 0; ctxt->argl[an]; an++) {
		if (ctxt->m[i].argc == 0) {
			ctxt->m[i].argv = &ctxt->argl[an];
		}
		if (!strcmp(ctxt->argl[an], "--")) {
			ctxt->argl[an] = NULL;
			i++;
		} else {
			ctxt->m[i].argc++;
		}
	}

	for (i = 0; i < ctxt->n; i++) {
		if (!ctxt->m[i].argv[0]) {
			crit(MODPREFIX "missing module name");
			return 1;
		}
		map = strdup(ctxt->m[i].argv[0]);
		if (!map)
			goto nomem;

		if ((mapfmt = strchr(map, ',')))
			*(mapfmt++) = '\0';

		if (!(ctxt->m[i].mod = open_lookup(map, MODPREFIX,
						   mapfmt ? mapfmt : my_mapfmt,
						   ctxt->m[i].argc - 1,
						   ctxt->m[i].argv + 1)))
			return 1;
	}

	*context = ctxt;
	return 0;

      nomem:
	crit(MODPREFIX "malloc: %m");
	return 1;
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int i, ret, at_least_1 = 0;

	for (i = 0; i < ctxt->n; i++) {
		ret = ctxt->m[i].mod->lookup_ghost(root, ghost, now,
						   ctxt->m[i].mod->context);
		if (ret & LKP_FAIL)
			continue;

		at_least_1 = 1;	
	}

	if (!at_least_1)
		return LKP_FAIL;

	return LKP_INDIRECT;
}

int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int i;

	for (i = 0; i < ctxt->n; i++) {
		if (ctxt->m[i].mod->lookup_mount(root, name, name_len,
						 ctxt->m[i].mod->context) == 0)
			return 0;
	}
	return 1;		/* No module succeeded */
}

int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int i, rv = 0;

	for (i = 0; i < ctxt->n; i++) {
		rv = rv || close_lookup(ctxt->m[i].mod);
	}

	free(ctxt->argl);
	free(ctxt->m);
	free(ctxt);

	return rv;
}
