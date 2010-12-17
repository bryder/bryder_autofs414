#ident "$Id: lookup_file.c,v 1.16 2005/04/06 15:14:23 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  lookup_file.c - module for Linux automount to query a flat file map
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *   Copyright 2001-2003 Ian Kent <raven@themaw.net>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <syslog.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MAPFMT_DEFAULT "sun"

#define MODPREFIX "lookup(file): "

typedef enum {
	st_begin, st_compare, st_star, st_badent, st_entspc, st_getent
} LOOKUP_STATE;

typedef enum { got_nothing, got_star, got_real } FOUND_STATE;
typedef enum { esc_none, esc_char, esc_val } ESCAPES;


struct lookup_context {
	const char *mapname;
	time_t mtime;
	struct parse_mod *parse;
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt;
	struct stat st;

	if (!(*context = ctxt = malloc(sizeof(struct lookup_context)))) {
		crit(MODPREFIX "malloc: %m");
		return 1;
	}

	if (argc < 1) {
		crit(MODPREFIX "No map name");
		return 1;
	}

	ctxt->mapname = argv[0];

	if (ctxt->mapname[0] != '/') {
		crit(MODPREFIX "file map %s is not an absolute pathname",
		       ctxt->mapname);
		return 1;
	}

	if (access(ctxt->mapname, R_OK)) {
		crit(MODPREFIX "file map %s missing or not readable",
		       ctxt->mapname);
		return 1;
	}

	if (stat(ctxt->mapname, &st)) {
		crit(MODPREFIX "file map %s, could not stat",
		       ctxt->mapname);
		return 1;
	}
		
	ctxt->mtime = st.st_mtime;

	if (!mapfmt)
		mapfmt = MAPFMT_DEFAULT;

	cache_init();

	return !(ctxt->parse = open_parse(mapfmt, MODPREFIX, argc - 1, argv + 1));
}

static int read_one(FILE *f, char *key, char *mapent)
{
	char *kptr, *p;
	int mapent_len, key_len;
	int ch, nch;
	LOOKUP_STATE state;
	FOUND_STATE getting, gotten;
	ESCAPES escape;

	kptr = key;
	p = NULL;
	mapent_len = key_len = 0;
	state = st_begin;
	memset(key, 0, KEY_MAX_LEN + 1);
	memset(mapent, 0, MAPENT_MAX_LEN + 1);
	getting = gotten = got_nothing;
	escape = esc_none;

	/* This is ugly.  We can't just remove \ escape sequences in the value
	   portion of an entry, because the parsing routine may need it. */

	while ((ch = getc(f)) != EOF) {
		switch (escape) {
		case esc_none:
			if (ch == '\\') {
				/* Handle continuation lines */
				if ((nch = getc(f)) == '\n')
					continue;
				ungetc(nch, f);
				escape = esc_char;
			}
			break;

		case esc_char:
			escape = esc_val;
			break;

		case esc_val:
			escape = esc_none;
			break;
		}

		switch (state) {
		case st_begin:
			if (!escape) {
				if (isspace(ch));
				else if (ch == '#')
					state = st_badent;
				else if (ch == '*') {
					state = st_star;
					*(kptr++) = ch;
					key_len++;
				} else {
					state = st_compare;
					*(kptr++) = ch;
					key_len++;
				}
			} else if (escape == esc_char);
			else
				state = st_badent;
			break;

		case st_compare:
			if (ch == '\n')
				state = st_begin;
			else if (isspace(ch) && !escape) {
				getting = got_real;
				state = st_entspc;
			} else if (escape == esc_char);
			else {
				if (key_len == KEY_MAX_LEN) {
					state = st_badent;
					error(MODPREFIX "Map key \"%s...\" "
					      "is too long.  The maximum key "
					      "length is %d", key,
					      KEY_MAX_LEN);
				} else {
					*(kptr++) = ch;
					key_len++;
				}
			}
			break;

		case st_star:
			if (ch == '\n')
				state = st_begin;
			else if (isspace(ch) && gotten < got_star && !escape) {
				getting = got_star;
				state = st_entspc;
			} else if (escape != esc_char)
				state = st_badent;
			break;

		case st_badent:
			if (ch == '\n')
				state = st_begin;
			break;

		case st_entspc:
			if (ch == '\n')
				state = st_begin;
			else if (!isspace(ch) || escape) {
				state = st_getent;
				p = mapent;
				gotten = getting;
				*(p++) = ch;
				mapent_len = 1;
			}
			break;

		case st_getent:
			if (ch == '\n') {
				state = st_begin;
				if (gotten == got_real || gotten == getting)
					goto got_it;
			} else if (mapent_len < MAPENT_MAX_LEN) {
				mapent_len++;
				*(p++) = ch;
				nch = getc(f);
				if (nch == EOF &&
				   (gotten == got_real || gotten == getting))
				   	goto got_it;
				ungetc(nch, f);
			} else {
				error(MODPREFIX "Map entry \"%s...\" for key "
				      "\"%s\" is too long.  The maximum entry"
				      " size is %d", mapent, key,
				      MAPENT_MAX_LEN);
				state = st_badent;
			}
			break;
		}
		continue;

	      got_it:
		if (gotten == got_nothing)
			goto next;

		return 1;

	      next:
		kptr = key;
		p = NULL;
		mapent_len = key_len = 0;
		memset(key, 0, KEY_MAX_LEN + 1);
		memset(mapent, 0, MAPENT_MAX_LEN + 1);
		getting = gotten = got_nothing;
		escape = esc_none;
	}

	return 0;
}

static int read_map(const char *root, time_t now, struct lookup_context *ctxt)
{
	char key[KEY_MAX_LEN + 1];
	char mapent[MAPENT_MAX_LEN + 1];
	char *mapname;
	FILE *f;
	int  entry;
	time_t age = now ? now : time(NULL);

	mapname = alloca(strlen(ctxt->mapname) + 6);
	sprintf(mapname, "file:%s", ctxt->mapname);

	f = fopen(ctxt->mapname, "r");
	if (!f) {
		error(MODPREFIX "could not open map file %s", ctxt->mapname);
		return 0;
	}

	while(1) {
		entry = read_one(f, key, mapent);
		if (entry)
			cache_add(root, key, mapent, age);

		if (feof(f))
			break;
	}

	fclose(f);

	/* Clean stale entries from the cache */
	cache_clean(root, age);

	return 1;
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	struct mapent_cache *me;
	struct stat st;
	int status = 1;

	if (!read_map(root, now, ctxt))
		return LKP_FAIL;

	if (stat(ctxt->mapname, &st)) {
		crit(MODPREFIX "file map %s, could not stat",
		       ctxt->mapname);
		return 1;
	}
		
	ctxt->mtime = st.st_mtime;

	status = cache_ghost(root, ghost, ctxt->mapname, "file", ctxt->parse);

	me = cache_lookup_first();
	/* me NULL => empty map */
	if (me == NULL)
		return LKP_FAIL;

	if (*me->key == '/' && *(root + 1) != '-') {
		me = cache_partial_match(root);
		/* 
		 * me NULL => no entries for this direct mount
		 * root or indirect map
		 */
		if (me == NULL)
			return LKP_FAIL | LKP_INDIRECT;
	}

	return status;
}

static int lookup_one(const char *root,
		      const char *key, int key_len,
		      struct lookup_context *ctxt)
{
	char mkey[KEY_MAX_LEN + 1];
	char mapent[MAPENT_MAX_LEN + 1];
	char *mapname;
	FILE *f;
	int entry;
	time_t age = time(NULL);

	mapname = alloca(strlen(ctxt->mapname) + 6);
	sprintf(mapname, "file:%s", ctxt->mapname);

	f = fopen(ctxt->mapname, "r");
	if (!f) {
		error(MODPREFIX "could not open map file %s", ctxt->mapname);
		return 0;
	}

	while(1) {
		entry = read_one(f, mkey, mapent);
		if (entry)
			if (strncmp(mkey, key, key_len) == 0) {
				fclose(f);
				return cache_update(root, key, mapent, age);
			}

		if (feof(f))
			break;
	}

	fclose(f);

	return CHE_MISSING;
}

static int lookup_wild(const char *root, struct lookup_context *ctxt)
{
	char mkey[KEY_MAX_LEN + 1];
	char mapent[MAPENT_MAX_LEN + 1];
	char *mapname;
	FILE *f;
	int entry;
	time_t age = time(NULL);

	mapname = alloca(strlen(ctxt->mapname) + 6);
	sprintf(mapname, "file:%s", ctxt->mapname);

	f = fopen(ctxt->mapname, "r");
	if (!f) {
		error(MODPREFIX "could not open map file %s", ctxt->mapname);
		return 0;
	}

	while(1) {
		entry = read_one(f, mkey, mapent);
		if (entry)
			if (strncmp(mkey, "*", 1) == 0) {
				fclose(f);
				return cache_update(root, "*", mapent, age);
			}

		if (feof(f))
			break;
	}

	fclose(f);

	return CHE_MISSING;
}

int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	struct stat st;
	char key[KEY_MAX_LEN + 1];
	int key_len;
	char mapent[MAPENT_MAX_LEN + 1];
	struct mapent_cache *me;
	time_t now = time(NULL);
	time_t t_last_read;
	int need_hup = 0;
	int ret = 1;

	if (stat(ctxt->mapname, &st)) {
		crit(MODPREFIX "file map %s, could not stat", ctxt->mapname);
		return 1;
	}

	if (ap.type == LKP_DIRECT)
		key_len = snprintf(key, KEY_MAX_LEN, "%s/%s", root, name);
	else
		key_len = snprintf(key, KEY_MAX_LEN, "%s", name);

	if (key_len > KEY_MAX_LEN)
		return 1;

	me = cache_lookup_first();
	t_last_read = me ? now - me->age : ap.exp_runfreq + 1;

	/* only if it has been modified */
	if (st.st_mtime > ctxt->mtime) {
		ret = lookup_one(root, key, key_len, ctxt);
		if (!ret)
			return 1;

		debug("ret = %d", ret);

		if (t_last_read > ap.exp_runfreq)
			if (ret & (CHE_UPDATED | CHE_MISSING))
				need_hup = 1;

		if (ret == CHE_MISSING) {
			int wild = CHE_MISSING;

			/* Maybe update wild card map entry */
			if (ap.type == LKP_INDIRECT) {
				wild = lookup_wild(root, ctxt);
				if (wild == CHE_MISSING)
					cache_delete(root, "*", 0);
			}

			if (cache_delete(root, key, 0) &&
					wild & (CHE_MISSING | CHE_FAIL))
				rmdir_path(key);
		}
	}

	me = cache_lookup(key);
	if (me == NULL) {
		/* path component, do submount */
		me = cache_partial_match(key);
		if (me)
			sprintf(mapent, "-fstype=autofs file:%s", ctxt->mapname);
	} else
		sprintf(mapent, me->mapent);

	if (me) {
		debug(MODPREFIX "%s -> %s", key, mapent);
		ret = ctxt->parse->parse_mount(root, name, name_len,
						  mapent, ctxt->parse->context);
	}

	/* Have parent update its map ? */
	if (need_hup)
		kill(getppid(), SIGHUP);

	return ret;
}

int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv = close_parse(ctxt->parse);
	free(ctxt);
	cache_release();
	return rv;
}
