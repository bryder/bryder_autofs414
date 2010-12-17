#ident "$Id: parse_hesiod.c,v 1.3 2004/01/29 16:01:22 raven Exp $"
/*
 * parse_hesiod.c
 *
 * Module for Linux automountd to parse a hesiod filesystem entry.
 */

#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#define MODULE_PARSE
#include "automount.h"

#define MODPREFIX "parse(hesiod): "

int parse_version = AUTOFS_PARSE_VERSION;	/* Required by protocol */

#define HESIOD_LEN 512

/* Break out the fields in an AFS record of the form:
   "AFS /afs/athena/mit/tytso w /mit/tytso-afs" */
static int parse_afs(const char *filsysline, const char *name, int name_len,
		     char *source, int source_len, char *options, int options_len)
{
	const char *p;
	int i;

	p = filsysline;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Skip the filesystem type. */
	while (!isspace(*p))
		p++;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Isolate the source for this AFS fs. */
	for (i = 0; (!isspace(p[i]) && i < source_len); i++) {
		source[i] = p[i];
	}

	source[i] = 0;
	p += i;

	/* Skip whitespace. */
	while ((*p) && (isspace(*p)))
		p++;

	/* Isolate the source for this AFS fs. */
	for (i = 0; (!isspace(p[i]) && i < options_len); i++) {
		options[i] = p[i];
	}
	options[i] = 0;

	/* Hack for "r" or "w" options. */
	if (!strcmp(options, "r"))
		strcpy(options, "ro");

	if (!strcmp(options, "w"))
		strcpy(options, "rw");

	debug(MODPREFIX
	      "parsing AFS record gives '%s'->'%s' with options" " '%s'",
	      name, source, options);

	return 0;
}

/*
 * Break out the fields in an NFS record of the form:
 * "NFS /export/src nelson.tx.ncsu.edu w /ncsu/tx-src"
 */
static int parse_nfs(const char *filsysline, const char *name,
		     int name_len, char *source, int source_len,
		     char *options, int options_len)
{
	const char *p;
	char mount[HESIOD_LEN + 1];
	int i;

	p = filsysline;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Skip the filesystem type. */
	while (!isspace(*p))
		p++;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Isolate the remote mountpoint for this NFS fs. */
	for (i = 0; (!isspace(p[i]) && i < sizeof(mount)); i++) {
		mount[i] = p[i];
	}

	mount[i] = 0;
	p += i;

	/* Skip whitespace. */
	while ((*p) && (isspace(*p)))
		p++;

	/* Isolate the remote host. */
	for (i = 0; (!isspace(p[i]) && i < source_len); i++) {
		source[i] = p[i];
	}

	source[i] = 0;
	p += i;

	/* Append ":mountpoint" to the source to get "host:mountpoint". */
	strncat(source, ":", source_len);
	strncat(source, mount, source_len);

	/* Skip whitespace. */
	while ((*p) && (isspace(*p)))
		p++;

	/* Isolate the mount options. */
	for (i = 0; (!isspace(p[i]) && i < options_len); i++) {
		options[i] = p[i];
	}
	options[i] = 0;

	/* Hack for "r" or "w" options. */
	if (!strcmp(options, "r"))
		strcpy(options, "ro");

	if (!strcmp(options, "w"))
		strcpy(options, "rw");

	debug(MODPREFIX
	      "parsing NFS record gives '%s'->'%s' with options" "'%s'",
	      name, source, options);

	return 0;
}

/* Break out the fields in a generic record of the form:
   "UFS /dev/ra0g w /site" */
static int parse_generic(const char *filsysline, const char *name, int name_len,
			 char *source, int source_len, char *options, int options_len)
{
	const char *p;
	int i;

	p = filsysline;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Skip the filesystem type. */
	while (!isspace(*p))
		p++;

	/* Skip whitespace. */
	while (isspace(*p))
		p++;

	/* Isolate the source for this fs. */
	for (i = 0; (!isspace(p[i]) && i < source_len); i++) {
		source[i] = p[i];
	}

	source[i] = 0;
	p += i;

	/* Skip whitespace. */
	while ((*p) && (isspace(*p)))
		p++;

	/* Isolate the mount options. */
	for (i = 0; (!isspace(p[i]) && i < options_len); i++) {
		options[i] = p[i];
	}
	options[i] = 0;

	/* Hack for "r" or "w" options. */
	if (!strcmp(options, "r"))
		strcpy(options, "ro");

	if (!strcmp(options, "w"))
		strcpy(options, "rw");

	debug(MODPREFIX
	      "parsing generic record gives '%s'->'%s' with options '%s'",
	      name, source, options);

	return 0;
}

int parse_init(int argc, const char *const *argv, void **context)
{
	return 0;
}

int parse_done(void *context)
{
	return 0;
}

int parse_mount(const char *root, const char *name,
		int name_len, const char *mapent, void *context)
{
	char source[HESIOD_LEN + 1];
	char fstype[HESIOD_LEN + 1];
	char options[HESIOD_LEN + 1];
	char *q;
	const char *p;

	p = mapent;
	q = fstype;

	/* Skip any initial whitespace... */
	while (isspace(*p))
		p++;

	/* Isolate the filesystem type... */
	while (!isspace(*p)) {
		*q++ = tolower(*p++);
	}
	*q = 0;

	/* If it's an error message... */
	if (!strcasecmp(fstype, "err")) {
		error(MODPREFIX "%s", mapent);
		return 1;
	/* If it's an AFS fs... */
	} else if (!strcasecmp(fstype, "afs"))
		parse_afs(mapent, name, name_len,
			  source, sizeof(source), options, sizeof(options));
	/* If it's NFS... */
	else if (!strcasecmp(fstype, "nfs"))
		parse_nfs(mapent, name, name_len,
			  source, sizeof(source), options, sizeof(options));
	/* Punt. */
	else
		parse_generic(mapent, name, name_len, source, sizeof(source),
			      options, sizeof(options));

	debug(MODPREFIX "mount %s is type %s from %s", name, fstype, source);

	return do_mount(root, name, name_len, source, fstype, options);
}
