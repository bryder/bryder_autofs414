#ident "$Id: lookup_nisplus.c,v 1.4 2004/12/31 06:30:08 raven Exp $"
/*
 * lookup_nisplus.c
 *
 * Module for Linux automountd to access a NIS+ automount map
 */

#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/nis.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MAPFMT_DEFAULT "sun"

#define MODPREFIX "lookup(nisplus): "

struct lookup_context {
	const char *domainname;
	const char *mapname;
	struct parse_mod *parse;
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt;

	if (!(*context = ctxt = malloc(sizeof(struct lookup_context)))) {
		crit(MODPREFIX "%m");
		return 1;
	}

	if (argc < 1) {
		crit(MODPREFIX "No map name");
		return 1;
	}
	ctxt->mapname = argv[0];

	/* 
	 * nis_local_directory () returns a pointer to a static buffer.
	 * We don't need to copy or free it.
	 */
	ctxt->domainname = nis_local_directory();

	if (!mapfmt)
		mapfmt = MAPFMT_DEFAULT;

	return !(ctxt->parse = open_parse(mapfmt, MODPREFIX, argc - 1, argv + 1));
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	return LKP_NOTSUP;
}

int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	char tablename[strlen(name) + strlen(ctxt->mapname) +
		       strlen(ctxt->domainname) + 20];
	nis_result *result;
	int rv;

	debug(MODPREFIX "looking up %s", name);

	sprintf(tablename, "[key=%s],%s.org_dir.%s", name, ctxt->mapname,
		ctxt->domainname);

	result = nis_list(tablename, FOLLOW_PATH | FOLLOW_LINKS, NULL, NULL);
	if (result->status != NIS_SUCCESS && result->status != NIS_S_SUCCESS) {
		/* Try to get the "*" entry if there is one - note that we *don't*
		   modify "name" so & -> the name we used, not "*" */
		sprintf(tablename, "[key=*],%s.org_dir.%s", ctxt->mapname,
			ctxt->domainname);
		result = nis_list(tablename, FOLLOW_PATH | FOLLOW_LINKS, NULL, NULL);
	}
	if (result->status != NIS_SUCCESS && result->status != NIS_S_SUCCESS) {
		crit(MODPREFIX "lookup for %s failed: %s", name,
		       nis_sperrno(result->status));
		return 1;
	}

	debug(MODPREFIX "%s -> %s", name,
	       NIS_RES_OBJECT(result)->EN_data.en_cols.en_cols_val[1].ec_value.
	       ec_value_val);

	rv = ctxt->parse->parse_mount(root, name, name_len,
				      NIS_RES_OBJECT(result)->EN_data.en_cols.
				      en_cols_val[1].ec_value.ec_value_val,
				      ctxt->parse->context);
	return rv;
}

int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv = close_parse(ctxt->parse);
	free(ctxt);
	return rv;
}
