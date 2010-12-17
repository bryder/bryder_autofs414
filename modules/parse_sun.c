#ident "$Id: parse_sun.c,v 1.28 2005/04/05 12:42:42 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  parse_sun.c - module for Linux automountd to parse a Sun-format
 *                automounter map
 * 
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *   Copyright 2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *   Copyright 2004, 2005 Ian Kent <raven@themaw.net>
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
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <syslog.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>

#define MODULE_PARSE
#include "automount.h"

#define MODPREFIX "parse(sun): "

int parse_version = AUTOFS_PARSE_VERSION;	/* Required by protocol */

static struct mount_mod *mount_nfs = NULL;
static int init_ctr = 0;

struct substvar {
	char *def;		/* Define variable */
	char *val;		/* Value to replace with */
	struct substvar *next;
};

struct parse_context {
	char *optstr;		/* Mount options */
	struct substvar *subst;	/* $-substitutions */
	int slashify_colons;	/* Change colons to slashes? */
};

struct multi_mnt {
	char *path;
	char *options;
	char *location;
	struct multi_mnt *next;
};

struct utsname un;
char processor[65];		/* Not defined on Linux, so we make our own */

/* Predefined variables: tail of link chain */
static struct substvar
	sv_arch   = {"ARCH",   un.machine,  NULL },
	sv_cpu    = {"CPU",    processor,   &sv_arch},
	sv_host   = {"HOST",   un.nodename, &sv_cpu},
	sv_osname = {"OSNAME", un.sysname,  &sv_host},
	sv_osrel  = {"OSREL",  un.release,  &sv_osname},
	sv_osvers = {"OSVERS", un.version,  &sv_osrel
};

/* Default context pattern */

static char *child_args = NULL;
static struct parse_context default_context = {
	NULL,			/* No mount options */
	&sv_osvers,		/* The substvar predefined variables */
	1			/* Do slashify_colons */
};

/* Free all storage associated with this context */
static void kill_context(struct parse_context *ctxt)
{
	struct substvar *sv, *nsv;

	sv = ctxt->subst;

	while (sv != &sv_osvers) {
		nsv = sv->next;
		free(sv);
		sv = nsv;
	}

	if (ctxt->optstr)
		free(ctxt->optstr);

	free(ctxt);
}


/* Find the $-variable matching a certain string fragment */
static const struct substvar *findvar(const struct substvar *sv, const char *str, int len)
{
#ifdef ENABLE_EXT_ENV
	/* holds one env var */
	static struct substvar sv_env = { NULL, NULL,  NULL };
	static char *substvar_env;
	char etmp[512];
#endif

	while (sv) {
		if (!strncmp(str, sv->def, len) && sv->def[len] == '\0')
			return sv;
		sv = sv->next;
	}

#ifdef ENABLE_EXT_ENV
	/* builtin map failed, try the $ENV */
	memcpy(etmp, str, len);
	etmp[len]='\0';

	if ((substvar_env=getenv(etmp)) != NULL) {
		sv_env.val = substvar_env;
		return(&sv_env);
	}
#endif

	return NULL;
}

/* 
 * $- and &-expand a Sun-style map entry and return the length of the entry.
 * If "dst" is NULL, just count the length.
 */
int expandsunent(const char *src, char *dst, const char *key,
		 const struct substvar *svc, int slashify_colons)
{
	const struct substvar *sv;
	int len, l, seen_colons;
	const char *p;
	char ch;

	len = 0;
	seen_colons = 0;

	while ((ch = *src++)) {
		switch (ch) {
		case '&':
			l = strlen(key);
			if (dst) {
				strcpy(dst, key);
				dst += l;
			}
			len += l;
			break;

		case '$':
			if (*src == '{') {
				p = strchr(++src, '}');
				if (!p) {
					/* Ignore rest of string */
					if (dst)
						*dst = '\0';
					return len;
				}
				sv = findvar(svc, src, p - src);
				if (sv) {
					l = strlen(sv->val);
					if (dst) {
						strcpy(dst, sv->val);
						dst += l;
					}
					len += l;
				}
				src = p + 1;
			} else {
				p = src;
				while (isalnum(*p) || *p == '_')
					p++;
				sv = findvar(svc, src, p - src);
				if (sv) {
					l = strlen(sv->val);
					if (dst) {
						strcpy(dst, sv->val);
						dst += l;
					}
					len += l;
				}
				src = p;
			}
			break;

		case '\\':
			len++;
			if (dst)
				*dst++ = ch;

			if (*src) {
				len++;
				if (dst)
					*dst++ = *src;
				src++;
			}
			break;

		case ':':
			if (dst)
				*(dst++) = 
				  (seen_colons && slashify_colons) ? '/' : ':';
			len++;
			seen_colons = 1;
			break;

		default:
			if (isspace(ch))
				seen_colons = 0;

			if (dst)
				*(dst++) = ch;
			len++;
			break;
		}
	}
	if (dst)
		*dst = '\0';
	return len;
}

/*
 * Skip whitespace in a string; if we hit a #, consider the rest of the
 * entry a comment.
 */
const char *skipspace(const char *whence)
{
	while (1) {
		switch (*whence) {
		case ' ':
		case '\b':
		case '\t':
		case '\n':
		case '\v':
		case '\f':
		case '\r':
			whence++;
			break;
		case '#':	/* comment: skip to end of string */
			while (*whence != '\0')
				whence++;
			/* FALLTHROUGH */

		default:
			return whence;
		}
	}
}

/*
 * Check a string to see if a colon appears before the next '/'.
 */
int check_colon(const char *str)
{
	char *ptr = (char *) str;

	while (*ptr && *ptr != ':' && *ptr != '/') {
		ptr++;
	}

	if (!*ptr || *ptr == '/')
		return 0;

	return 1;
}

/* Get the length of a chunk delimitered by whitespace */
int chunklen(const char *whence, int expect_colon)
{
	int n = 0;
	int quote = 0;

	for (; *whence; whence++, n++) {
		switch (*whence) {
		case '\\':
			if( quote ) {
				break;
			} else {
				quote = 1;
				continue;
			}
		case ':':
			if (expect_colon)
				expect_colon = 0;
			continue;
		case ' ':
		case '\t':
			/* Skip space or tab if we expect a colon */
			if (expect_colon)
				continue;
		case '\b':
		case '\n':
		case '\v':
		case '\f':
		case '\r':
		case '#':
		case '\0':
			if (!quote)
				return n;
			/* FALLTHROUGH */
		default:
			break;
		}
		quote = 0;
	}

	return n;
}

/*
 * Compare str with pat.  Return 0 if compare equal or
 * str is an abbreviation of pat of no less than mchr characters.
 */
int strmcmp(const char *str, const char *pat, int mchr)
{
	int nchr = 0;

	while (*str == *pat) {
		if (!*str)
			return 0;
		str++;
		pat++;
		nchr++;
	}

	if (!*str && nchr > mchr)
		return 0;

	return *pat - *str;
}

int parse_init(int argc, const char *const *argv, void **context)
{
	struct parse_context *ctxt;
	struct substvar *sv;
	char *noptstr;
	const char *xopt;
	int optlen, len;
	int i, bval;

	/* Get processor information for predefined escapes */

	if (!init_ctr) {
		uname(&un);
		/* uname -p is not defined on Linux.  Make it the same as uname -m,
		   except make it return i386 on all x86 (x >= 3) */
		strcpy(processor, un.machine);
		if (processor[0] == 'i' && processor[1] >= '3' &&
		    !strcmp(processor + 2, "86"))
			processor[1] = '3';
	}

	/* Set up context and escape chain */

	if (!(ctxt = (struct parse_context *) malloc(sizeof(struct parse_context)))) {
		crit(MODPREFIX "malloc: %m");
		return 1;
	}
	*context = (void *) ctxt;

	*ctxt = default_context;
	optlen = 0;

	/* Look for options and capture, and create new defines if we need to */

	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-' &&
		   (argv[i][1] == 'D' || argv[i][1] == '-') ) {
			switch (argv[i][1]) {
			case 'D':
				sv = malloc(sizeof(struct substvar));
				if (!sv) {
					error(MODPREFIX "malloc: %m");
					break;
				}
				if (argv[i][2])
					sv->def = strdup(argv[i] + 2);
				else if (++i < argc)
					sv->def = strdup(argv[i]);
				else {
					free(sv);
					break;
				}

				if (!sv->def) {
					error(MODPREFIX "strdup: %m");
					free(sv);
				} else {
					sv->val = strchr(sv->def, '=');
					if (sv->val)
						*(sv->val++) = '\0';
					else
						sv->val = "";
					/* we use 5 for the "-D", the "=", and the null */
					if (child_args) {
						child_args = realloc(child_args, strlen(child_args) + strlen(sv->def) + strlen(sv->val) + 5);
						strcat(child_args, ",");
					}
					else { /* No comma, so only +4 */
						child_args = malloc(strlen(sv->def) + strlen(sv->val) + 4);
						*child_args = '\0';
					}
					strcat(child_args, "-D");
					strcat(child_args, sv->def);
					strcat(child_args, "=");
					strcat(child_args, sv->val);
					sv->next = ctxt->subst;
					ctxt->subst = sv;
				}
				break;

			case '-':
				if (!strncmp(argv[i] + 2, "no-", 3)) {
					xopt = argv[i] + 5;
					bval = 0;
				} else {
					xopt = argv[i] + 2;
					bval = 1;
				}

				if (!strmcmp(xopt, "slashify-colons", 1))
					ctxt->slashify_colons = bval;
				else
					error(MODPREFIX "unknown option: %s",
					      argv[i]);

				break;

			default:
				error(MODPREFIX "unknown option: %s", argv[i]);
				break;
			}
		} else {
			int offset = (argv[i][0] == '-' ? 1 : 0);
			len = strlen(argv[i] + offset);
			if (ctxt->optstr) {
				noptstr =
				    (char *) realloc(ctxt->optstr, optlen + len + 2);
				if (!noptstr)
					break;
				noptstr[optlen] = ',';
				strcpy(noptstr + optlen + 1, argv[i] + offset);
				optlen += len + 1;
			} else {
				noptstr = (char *) malloc(len + 1);
				strcpy(noptstr, argv[i] + offset);
				optlen = len;
			}
			if (!noptstr) {
				kill_context(ctxt);
				crit(MODPREFIX "%m");
				return 1;
			}
			ctxt->optstr = noptstr;
			debug(MODPREFIX "init gathered options: %s",
			      ctxt->optstr);
		}
	}

	/* We only need this once.  NFS mounts are so common that we cache
	   this module. */
	if (!mount_nfs)
		if ((mount_nfs = open_mount("nfs", MODPREFIX))) {
			init_ctr++;
			return 0;
		} else {
			kill_context(ctxt);
			return 1;
	} else {
		init_ctr++;
		return 0;
	}
}

static char *dequote(const char *str, int strlen)
{
	char *ret = malloc(strlen + 1);
	char *cp = ret;
	const char *scp;
	int origlen = strlen;
	int quote = 0;

	if (ret == NULL)
		return NULL;

	for (scp = str; strlen > 0 && *scp; scp++, strlen--) {
		if (*scp == '\\' && !quote ) {
			quote = 1;
			continue;
		}
		quote = 0;
		*cp++ = *scp;
	}
	*cp = '\0';

	debug(MODPREFIX "dequote(\"%.*s\") -> %s", origlen, str, ret);

	return ret;
}

static const char *parse_options(const char *str, char **ret)
{
	const char *cp = str;
	int len;

	if (*cp++ != '-')
		return str;

	if (*ret != NULL)
		free(*ret);

	*ret = dequote(cp, len = chunklen(cp, 0));

	return cp + len;
}

static char *concat_options(char *left, char *right)
{
	char *ret;

	if (left == NULL || *left == '\0') {
		free(left);
		ret = strdup(right);
		return ret;
	}

	if (right == NULL || *right == '\0') {
		free(right);
		return strdup(left);
	}

	ret = malloc(strlen(left) + strlen(right) + 2);

	if (ret == NULL) {
		error(MODPREFIX "concat_options malloc: %m");
		return NULL;
	}

	sprintf(ret, "%s,%s", left, right);

	free(left);
	free(right);

	return ret;
}

static int sun_mount(const char *root, const char *name, int namelen,
		     const char *loc, int loclen, const char *options)
{
	char *fstype = "nfs";	/* Default filesystem type */
	int nonstrict = 1;
	int rv;
	char *mountpoint;
	char *what;
	char *nsswitch_map;
	int newmaplen = 0;

	if (*options == '\0')
		options = NULL;

	if (options) {
		char *noptions;
		const char *comma;
		char *np;
		int len = strlen(options) + 1;

		noptions = np = alloca(len);
		*np = '\0';

		/* Extract fstype= pseudo option */
		for (comma = options; *comma != '\0';) {
			const char *cp;

			while (*comma == ',')
				comma++;

			cp = comma;

			while (*comma != '\0' && *comma != ',')
				comma++;

			if (strncmp("fstype=", cp, 7) == 0) {
				int typelen = comma - (cp + 7);
				fstype = alloca(typelen + 1);
				memcpy(fstype, cp + 7, typelen);
				fstype[typelen] = '\0';
			} else if (strncmp("strict", cp, 6) == 0) {
				nonstrict = 0;
			} else if (strncmp("nonstrict", cp, 9) == 0) {
				nonstrict = 1;
			} else if (strncmp("bg", cp, 2) == 0 ||
				   strncmp("nofg", cp, 4) == 0) {
				continue;
			} else {
				memcpy(np, cp, comma - cp + 1);
				np += comma - cp + 1;
			}
		}

		if (np > noptions + len) {
			warn(MODPREFIX "options string truncated");
			np[len] = '\0';
		} else
			*(np - 1) = '\0';

		options = noptions;
	}


	if (child_args && !strcmp(fstype, "autofs")) {
		char *noptions;

		if (!options) {
			noptions = alloca(strlen(child_args) + 1);
			*noptions = '\0';
		} else {
			noptions = alloca(strlen(options) + strlen(child_args) + 2);

			if (noptions) {
				strcpy(noptions, options);
				strcat(noptions, ",");
			}
		}

		if (noptions) {
			strcat(noptions, child_args);
			options = noptions;
		} else {
			error(MODPREFIX "alloca failed for options");
		}
	}

	while (*name == '/') {
		name++;
		namelen--;
	}

	mountpoint = alloca(namelen + 1);
	sprintf(mountpoint, "%.*s", namelen, name);

	what = alloca(loclen + 1);
	memcpy(what, loc, loclen);
	what[loclen] = '\0';

	/*
	 * If we have an autofs map that doesn't contain a ':' then we need
	 * to detect what type of map it is.
	 */
	if (!strcmp(fstype, "autofs") && strchr(loc, ':') == NULL) {
		nsswitch_map = get_nsswitch_map(loc);
		if (!nsswitch_map) {
			error(MODPREFIX "unable to find map %s",loc);
			return 1;
		}
		newmaplen = strlen(nsswitch_map);
		what = alloca(newmaplen + 1);
		memcpy(what, nsswitch_map, newmaplen);
		what[newmaplen] = '\0';
		free (nsswitch_map);
	} else {
		what = alloca(loclen + 1);
		memcpy(what, loc, loclen);
		what[loclen] = '\0';
	}

	debug(MODPREFIX
	    "mounting root %s, mountpoint %s, what %s, fstype %s, options %s\n",
	    root, mountpoint, what, fstype, options);

	/* A malformed entry of the form key /xyz will trigger this case */
	if (!what || *what == '\0')
		return 1;

	if (!strcmp(fstype, "nfs")) {
		rv = mount_nfs->mount_mount(root, mountpoint, strlen(mountpoint),
					    what, fstype, options, mount_nfs->context);
	} else {
		/* Generic mount routine */
		rv = do_mount(root, mountpoint, strlen(mountpoint), what, fstype,
			      options);
	}

	if (nonstrict && rv)
		return -rv;

	return rv;
}

static int key_exists(struct multi_mnt *list, char *path, int pathlen)
{
	struct multi_mnt *mmptr = list;

	while (mmptr && pathlen == strlen(mmptr->path)) {
		if (!strncmp(mmptr->path, path, pathlen))
			return 1;
		mmptr = mmptr->next;
	}
	return 0;
}

/*
 * Build list of mounts in shortest -> longest order.
 * Pass in list head and return list head.
 */
struct multi_mnt *multi_add_list(struct multi_mnt *list,
				 char *path, char *options, char *location)
{
	struct multi_mnt *mmptr, *new, *old = NULL;
	int plen;

	if (!path || !options || !location)
		return NULL;

	new = malloc(sizeof(struct multi_mnt));
	if (!new)
		return NULL;

	new->path = path;
	new->options = options;
	new->location = location;

	plen = strlen(path);
	mmptr = list;
	while (mmptr) {
		if (plen <= strlen(mmptr->path))
			break;
		old = mmptr;
		mmptr = mmptr->next;
	}

	/* if a multimount entry has duplicate keys, it is invalid */
	if (key_exists(mmptr, path, plen)) {
		free(new);
		return NULL;
	}

	if (old)
		old->next = new;
	new->next = mmptr;

	return old ? list : new;
}

void multi_free_list(struct multi_mnt *list)
{
	struct multi_mnt *next;

	if (!list)
		return;

	next = list;
	while (next) {
		struct multi_mnt *this = next;

		next = this->next;

		if (this->path)
			free(this->path);

		if (this->options)
			free(this->options);

		if (this->location)
			free(this->location);

		free(this);
	}
}

/*
 * Scan map entry looking for evidence it has multiple key/mapent
 * pairs.
 */
static int check_is_multi(const char *mapent)
{
	const char *p = (char *) mapent;
	int multi = 0;
	int not_first_chunk = 0;

	if (!p) {
		crit("check_is_multi: unexpected NULL map entry pointer");
		return 0;
	}
	
	/* If first character is "/" it's a multi-mount */
	if (*p == '/')
		return 1;

	while (*p) {
		p = skipspace(p);

		/*
		 * After the first chunk there can be additional
		 * locations (possibly not multi) or possibly an
		 * options string if the first entry includes the
		 * optional '/' (is multi). Following this any
		 * path that begins with '/' indicates a mutil-mount
		 * entry.
		 */
		if (not_first_chunk) {
			if (*p == '/' || *p == '-') {
				multi = 1;
				break;
			}
		}

		while (*p == '-') {
			p += chunklen(p, 0);
			p = skipspace(p);
		}

		/*
		 * Expect either a path or location
		 * after which it's a multi mount.
		 */
		p += chunklen(p, check_colon(p));
		not_first_chunk++;
	}

	return multi;
}

/*
 * syntax is:
 *	[-options] location [location] ...
 *	[-options] [mountpoint [-options] location [location] ... ]...
 */
int parse_mount(const char *root, const char *name,
		int name_len, const char *mapent, void *context)
{
	struct parse_context *ctxt = (struct parse_context *) context;
	char *pmapent, *options;
	const char *p;
	int mapent_len, rv;
	int optlen;

	mapent_len = expandsunent(mapent, NULL, name, ctxt->subst, ctxt->slashify_colons);
	pmapent = alloca(mapent_len + 1);
	if (!pmapent) {
		error(MODPREFIX "alloca: %m");
		return 1;
	}
	pmapent[mapent_len] = '\0';

	expandsunent(mapent, pmapent, name, ctxt->subst, ctxt->slashify_colons);

	debug(MODPREFIX "expanded entry: %s", pmapent);

	options = strdup(ctxt->optstr ? ctxt->optstr : "");
	if (!options) {
		error(MODPREFIX "strdup: %m");
		return 1;
	}
	optlen = strlen(options);

	p = skipspace(pmapent);

	/* Deal with 0 or more options */
	if (*p == '-') {
		do {
			char *noptions = NULL;

			p = parse_options(p, &noptions);
			options = concat_options(options, noptions);

			if (options == NULL) {
				error(MODPREFIX "concat_options: %m");
				return 1;
			}
			p = skipspace(p);
		} while (*p == '-');
	}

	debug(MODPREFIX "gathered options: %s", options);

	if (check_is_multi(p)) {
		struct multi_mnt *list, *head = NULL;
		char *multi_root;
		int l;

		multi_root = alloca(strlen(root) + name_len + 2);
		if (!multi_root) {
			error(MODPREFIX "alloca: %m");
			free(options);
			return 1;
		}

		strcpy(multi_root, root);
		strcat(multi_root, "/");
		strcat(multi_root, name);

		/* It's a multi-mount; deal with it */
		do {
			char *myoptions = strdup(options);
			char *path, *loc;

			if (myoptions == NULL) {
				error(MODPREFIX "multi strdup: %m");
				free(options);
				multi_free_list(head);
				return 1;
			}

			if (*p != '/') {
				l = 0;
				path = dequote("/", 1);
			} else
				 path = dequote(p, l = chunklen(p, 0));

			if (!path) {
				error(MODPREFIX "out of memory");
				free(myoptions);
				free(options);
				multi_free_list(head);
				return 1;
			}

			p += l;
			p = skipspace(p);

			/* Local options are appended to per-map options */
			if (*p == '-') {
				do {
					char *newopt = NULL;

					p = parse_options(p, &newopt);
					myoptions = concat_options(myoptions, newopt);

					if (myoptions == NULL) {
						error(MODPREFIX
						    "multi concat_options: %m");
						free(options);
						free(path);
						multi_free_list(head);
						return 1;
					}
					p = skipspace(p);
				} while (*p == '-');
			}

			/* Skip over colon escape */
			if (*p == ':')
				p++;

			loc = dequote(p, l = chunklen(p, check_colon(p)));
			if (!loc) {
				error(MODPREFIX "out of memory");
				free(path);
				free(myoptions);
				free(options);
				multi_free_list(head);
				return 1;
			}

			p += l;
			p = skipspace(p);

			while (*p && *p != '/') {
				char *ent;

				ent = dequote(p, l = chunklen(p, check_colon(p)));
				if (!ent) {
					error(MODPREFIX "out of memory");
					free(path);
					free(myoptions);
					free(options);
					multi_free_list(head);
					return 1;
				}

				loc = realloc(loc, strlen(loc) + l + 2);
				if (!loc) {
					error(MODPREFIX "out of memory");
					free(ent);
					free(path);
					free(myoptions);
					free(options);
					multi_free_list(head);
					return 1;
				}

				strcat(loc, " ");
				strcat(loc, ent);

				free(ent);

				p += l;
				p = skipspace(p);
			}

			list = head;
			head = multi_add_list(list, path, myoptions, loc);
			if (!head) {
				free(loc);
				free(path);
				free(options);
				free(myoptions);
				multi_free_list(list);
				return 1;
			}
		} while (*p == '/');

		list = head;
		while (list) {
			debug(MODPREFIX
			      "multimount: %.*s on %.*s with options %s",
			      strlen(list->location), list->location,
			      strlen(list->path), list->path, list->options);

			rv = sun_mount(multi_root,
				       list->path, strlen(list->path),
				       list->location, strlen(list->location),
				       list->options);

			/* Convert non-strict failure into success */
			if (rv < 0) {
				rv = 0;
				debug("parse_mount: ignoring failure of non-strict mount");
			} else if (rv > 0)
				break;

			list = list->next;
		}

		multi_free_list(head);

		free(options);
		return rv;
	} else {
		/* Normal (non-multi) entries */
		char *loc;
		int loclen;
		int l;

		if (*p == ':')
			p++;	/* Sun escape for entries starting with / */

		loc = dequote(p, l = chunklen(p, check_colon(p)));
		if (!loc) {
			error(MODPREFIX "out of memory");
			free(options);
			return 1;
		}

		p += l;
		p = skipspace(p);

		while (*p) {
			char *ent;

			ent = dequote(p, l = chunklen(p, check_colon(p)));
			if (!ent) {
				error(MODPREFIX "out of memory");
				free(options);
				return 1;
			}

			loc = realloc(loc, strlen(loc) + l + 2);
			if (!loc) {
				error(MODPREFIX "out of memory");
				free(ent);
				free(options);
				return 1;
			}

			strcat(loc, " ");
			strcat(loc, ent);

			free(ent);

			p += l;
			p = skipspace(p);
		}

		loclen = strlen(loc);
		if (loclen == 0) {
			error(MODPREFIX "entry %s is empty!", name);
			free(loc);
			free(options);
			return 1;
		}

		debug(MODPREFIX "core of entry: options=%s, loc=%.*s",
		      options, loclen, loc);

		rv = sun_mount(root, name, name_len, loc, loclen, options);
		/* non-strict failure to normal failure for ordinary mount */
		if (rv < 0)
			rv = -rv;
			
		free(loc);
		free(options);
	}

	return rv;
}

int parse_done(void *context)
{
	int rv = 0;
	struct parse_context *ctxt = (struct parse_context *) context;

	if (--init_ctr == 0) {
		rv = close_mount(mount_nfs);
		mount_nfs = NULL;
	}
	kill_context(ctxt);
	return rv;
}
