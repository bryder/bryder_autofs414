#ident "$Id: lookup_hesiod.c,v 1.5 2004/12/31 06:30:08 raven Exp $"
/*
 * lookup_hesiod.c
 *
 * Module for Linux automountd to access automount maps in hesiod filsys
 * entries.
 *
 */

#include <sys/types.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <hesiod.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MAPFMT_DEFAULT "hesiod"

#define MODPREFIX "lookup(hesiod): "
#define HESIOD_LEN 512

struct lookup_context {
	struct parse_mod *parser;
#ifdef HESIOD_INTERFACES
	void *hesiod_context;
#endif
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

/* This initializes a context (persistent non-global data) for queries to
   this module. */
int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt = NULL;

	/* If we can't build a context, bail. */
	if ((*context = ctxt = (struct lookup_context *)
	     malloc(sizeof(struct lookup_context))) == NULL) {
		crit(MODPREFIX "malloc: %m");
		return 1;
	}

	/* Initialize the resolver. */
	res_init();

#ifdef HESIOD_INTERFACES
	/* Initialize the hesiod context. */
	if (hesiod_init(&(ctxt->hesiod_context)) != 0) {
		crit(MODPREFIX "hesiod_init(): %m");
		return 1;
	}
#endif
	/* If a map type isn't explicitly given, parse it as hesiod entries. */
	if (!mapfmt)
		mapfmt = MAPFMT_DEFAULT;

	/* Open the parser, if we can. */
	return !(ctxt->parser = open_parse(mapfmt, MODPREFIX, argc - 1, argv + 1));
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	return LKP_NOTSUP;
}

/* Lookup and act on a filesystem name.  In this case, lookup the "filsys"
   record in hesiod.  If it's an AFS or NFS filesyste, parse it out.  If
   it's an ERR filesystem, it's an error message we should log.  Otherwise,
   assume it's something we know how to deal with already (generic). */
int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	char **hes_result;
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv;
	char **record, *best_record = NULL, *p;
	int priority, lowest_priority = INT_MAX;	

	debug(MODPREFIX "looking up root=\"%s\", name=\"%s\"", root, name);

	chdir("/");		/* If this is not here the filesystem stays
				   busy, for some reason... */

#ifdef HESIOD_INTERFACES
	hes_result = hesiod_resolve(ctxt->hesiod_context, name, "filsys");
#else
	hes_result = hes_resolve(name, "filsys");
#endif

	if (!hes_result || !hes_result[0]) {
		warn(MODPREFIX "entry \"%s\" not found in map\n", name);
		return 1;
	}

	/* autofs doesn't support falling back to alternate records, so just
	   find the record with the lowest priority and hope it works.
	   -- Aaron Ucko <amu@alum.mit.edu> 2002-03-11 */
	for (record = hes_result; *record; ++record) {
	    p = strrchr(*record, ' ');
	    if ( p && isdigit(p[1]) ) {
		priority = atoi(p+1);
	    } else {
		priority = INT_MAX - 1;
	    }
	    if (priority < lowest_priority) {
		lowest_priority = priority;
		best_record = *record;
	    }
	}

	debug(MODPREFIX "lookup for \"%s\" gave \"%s\"", name, best_record);

	rv = ctxt->parser->parse_mount(root, name, name_len, best_record,
				       ctxt->parser->context);
#ifdef HESIOD_INTERFACES
	hesiod_free_list(ctxt->hesiod_context, hes_result);
#else
	free(hes_result);
#endif
	return rv;
}

/* This destroys a context for queries to this module.  It releases the parser
   structure (unloading the module) and frees the memory used by the context. */
int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv = close_parse(ctxt->parser);
#ifdef HESIOD_INTERFACES
	hesiod_end(ctxt->hesiod_context);
#endif
	free(ctxt);
	return rv;
}
