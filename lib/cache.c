#ident "$Id: cache.c,v 1.12 2005/02/06 06:00:53 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  cache.c - mount entry cache management routines
 *
 *   Copyright 2002-2003 Ian Kent <raven@themaw.net> - All Rights Reserved
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
#include <ctype.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "automount.h"

extern int kproto_version;	/* Kernel protocol major version */
extern int kproto_sub_version;	/* Kernel protocol minor version */

#define HASHSIZE      27

struct ghost_context {
	const char *root;
	char *mapname;
	char direct_base[KEY_MAX_LEN + 1];
	char key[KEY_MAX_LEN + 1];
	char mapent[MAPENT_MAX_LEN + 1];
};

static struct mapent_cache *mapent_hash[HASHSIZE];

static unsigned long ent_check(struct ghost_context *gc, char **key, int ghost);

static char *cache_fullpath(const char *root, const char *key)
{
	int l;
	char *path;

	if (*key == '/') {
		l = strlen(key) + 1;
		if (l > KEY_MAX_LEN)
			return NULL;
		path = malloc(l);
		strcpy(path, key);
	} else {
		l = strlen(key) + 1 + strlen(root) + 1; 
		if (l > KEY_MAX_LEN)
			return NULL;
		path = malloc(l);
		sprintf(path, "%s/%s", root, key);
	}

	return path;
}

static unsigned int hash(const char *key)
{
	unsigned long hashval;
	char *s = (char *) key;

	for (hashval = 0; *s != '\0';)
		hashval += *s++;

	return hashval % HASHSIZE;
}

void cache_init(void)
{
	int i;

	cache_release();

	for (i = 0; i < HASHSIZE; i++)
		mapent_hash[i] = NULL;
}

struct mapent_cache *cache_lookup_first(void)
{
	struct mapent_cache *me = NULL;
	int i;

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (me != NULL)
			break;
	}
	return me;
}

struct mapent_cache *cache_lookup(const char *key)
{
	struct mapent_cache *me = NULL;

	for (me = mapent_hash[hash(key)]; me != NULL; me = me->next)
		if (strcmp(key, me->key) == 0)
			return me;

	me = cache_lookup_first();
	if (me != NULL) {
		/* Can't have wildcard in direct map */
		if (*me->key == '/')
			return NULL;

		for (me = mapent_hash[hash("*")]; me != NULL; me = me->next)
			if (strcmp("*", me->key) == 0)
				return me;
	}
	return NULL;
}

struct mapent_cache *cache_lookup_next(struct mapent_cache *me)
{
	struct mapent_cache *next = me->next;

	while (next != NULL) {
		if (!strcmp(me->key, next->key))
			return next;

		next = next->next;
	}
	return NULL;
}

struct mapent_cache *cache_partial_match(const char *prefix)
{
	struct mapent_cache *me = NULL;
	int len = strlen(prefix);
	int i;

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (me == NULL)
			continue;

		if (len < strlen(me->key) &&
		    (strncmp(prefix, me->key, len) == 0) && me->key[len] == '/')
			return me;

		me = me->next;
		while (me != NULL) {
			if (len < strlen(me->key) &&
			    strncmp(prefix, me->key, len) == 0 && me->key[len] == '/')
				return me;
			me = me->next;
		}
	}
	return NULL;
}

int cache_add(const char *root, const char *key, const char *mapent, time_t age)
{
	struct mapent_cache *me = NULL, *existing = NULL;
	char *pkey, *pent;
	unsigned int hashval = hash(key);

	if (dumpmap) {
		fprintf(stdout, "%s %s\n", key, mapent);
		return CHE_OK;
	}

	me = (struct mapent_cache *) malloc(sizeof(struct mapent_cache));
	if (!me)
		return CHE_FAIL;

	pkey = malloc(strlen(key) + 1);
	if (!pkey) {
		free(me);
		return CHE_FAIL;
	}

	pent = malloc(strlen(mapent) + 1);
	if (!pent) {
		free(me);
		free(pkey);
		return CHE_FAIL;
	}

	me->key = strcpy(pkey, key);
	me->mapent = strcpy(pent, mapent);
	me->age = age;

	/* 
	 * We need to add to the end if values exist in order to
	 * preserve the order in which the map was read on lookup.
	 */
	existing = cache_lookup(key);
	if (!existing || *existing->key == '*') {
		me->next = mapent_hash[hashval];
		mapent_hash[hashval] = me;
	} else {
		while (1) {
			struct mapent_cache *next;
		
			next = cache_lookup_next(existing);
			if (!next)
				break;

			existing = next;
		}
		me->next = existing->next;
		existing->next = me;
	}

	return CHE_OK;
}

int cache_update(const char *root, const char *key, const char *mapent, time_t age)
{
	struct mapent_cache *s, *me = NULL;
	char *pent;
	int ret = CHE_OK;

	if (dumpmap) {
		fprintf(stdout, "%s %s\n", key, mapent);
		return CHE_OK;
	}

	for (s = mapent_hash[hash(key)]; s != NULL; s = s->next)
		if (strcmp(key, s->key) == 0)
			me = s;

	if (!me) {
		ret = cache_add(root, key, mapent, age);
		if (!ret) {
			debug("cache_add: failed for %s", key);
			return CHE_FAIL;
		}
		ret = CHE_UPDATED;
	} else {
		if (strcmp(me->mapent, mapent) != 0) {
			pent = malloc(strlen(mapent) + 1);
			if (pent == NULL) {
				return CHE_FAIL;
			}
			free(me->mapent);
			me->mapent = strcpy(pent, mapent);
			ret = CHE_UPDATED;
		}
		me->age = age;
	}

	return ret;
}

int cache_delete(const char *root, const char *key, int rmpath)
{
	struct mapent_cache *me = NULL, *pred;
	char *path;
	unsigned int hashval = hash(key);

	me = mapent_hash[hashval];
	if (me == NULL)
		return CHE_FAIL;

	path = cache_fullpath(root, key);
	if (!path)
		return CHE_FAIL;

	if (is_mounted(_PATH_MOUNTED, path)) {
		free(path);
		return CHE_FAIL;
	}

	while (me->next != NULL) {
		pred = me;
		me = me->next;
		if (strcmp(key, me->key) == 0) {
			pred->next = me->next;
			free(me->key);
			free(me->mapent);
			free(me);
			me = pred;
		}
	}

	me = mapent_hash[hashval];
	if (strcmp(key, me->key) == 0) {
		mapent_hash[hashval] = me->next;
		free(me->key);
		free(me->mapent);
		free(me);
	}

	if (rmpath)
		rmdir_path(path);
	free(path);
	return CHE_OK;
}

void cache_clean(const char *root, time_t age)
{
	struct mapent_cache *me, *pred;
	char *path;
	int i;

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (!me)
			continue;

		while (me->next != NULL) {
			pred = me;
			me = me->next;

			path = cache_fullpath(root, me->key);
			if (!path)
				return;

			if (me->age < age) {
				pred->next = me->next;
				free(me->key);
				free(me->mapent);
				free(me);
				me = pred;
			}

			free(path);
		}

		me = mapent_hash[i];
		if (!me)
			continue;

		path = cache_fullpath(root, me->key);
		if (!path)
			return;

		if (me->age < age) {
			mapent_hash[i] = me->next;
			free(me->key);
			free(me->mapent);
			free(me);
		}

		free(path);
	}
}

void cache_release(void)
{
	struct mapent_cache *me, *next;
	int i;

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (me == NULL)
			continue;
		mapent_hash[i] = NULL;
		next = me->next;
		free(me->key);
		free(me->mapent);
		free(me);

		while (next != NULL) {
			me = next;
			next = me->next;
			free(me->key);
			free(me->mapent);
			free(me);
		}
	}
}

int cache_ghost(const char *root, int ghosted,
		const char *mapname, const char *type, struct parse_mod *parse)
{
	struct mapent_cache *me;
	struct ghost_context gc;
	char *pkey = NULL;
	char *fullpath;
	struct stat st;
	unsigned long match = 0;
	unsigned long map = LKP_INDIRECT;
	int i;

	chdir("/");

	memset(&gc, 0, sizeof(struct ghost_context));
	gc.root = root;
	gc.mapname = alloca(strlen(mapname) + 6);
	sprintf(gc.mapname, "%s:%s", type, mapname);

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];

		if (me == NULL)
			continue;

		while (me != NULL) {
			strcpy(gc.key, me->key);
			strcpy(gc.mapent, me->mapent);

			match = ent_check(&gc, &pkey, ghosted);

			if (match == LKP_ERR_FORMAT) {
				error("cache_ghost: entry in %s not valid map "
				      "format, key %s",
				       gc.mapname, gc.key);
			} else if (match == LKP_WILD) {
				if (*me->key == '/')
					error("cache_ghost: wildcard map key "
					      "not valid in direct map");
				me = me->next;
				continue;;
			}

			switch (match) {
			case LKP_MATCH:
				if (!ghosted)
					break;

				if (*gc.key == '/') {
					fullpath = alloca(strlen(gc.key) + 2);
					sprintf(fullpath, "%s", gc.key);
				} else {
					fullpath =
					    alloca(strlen(gc.key) + strlen(gc.root) + 3);
					sprintf(fullpath, "%s/%s", gc.root, gc.key);
				}

				if (stat(fullpath, &st) == -1 && errno == ENOENT) {
					if (mkdir_path(fullpath, 0555) < 0)
						warn("cache_ghost: mkdir_path %s "
						     "failed: %m",
						      fullpath);
				}
				break;

			case LKP_MOUNT:
				if (!is_mounted(_PATH_MOUNTED, gc.direct_base)) {
					debug("cache_ghost: attempting to mount map, "
					      "key %s",
					      gc.direct_base);
					parse->parse_mount("", gc.direct_base + 1,
							   strlen(gc.direct_base) - 1,
							   gc.mapent, parse->context);
				}
				break;
			}
			me = me->next;
		}
	}

	me = cache_lookup_first();
	if (!me)
		return LKP_FAIL;
	if (*me->key == '/')
		map = LKP_DIRECT;
	return map;
}

static unsigned long ent_check(struct ghost_context *gc, char **pkey, int ghosted)
{
	char *proot = (char *) gc->root;
	char *slash, *pk;
	size_t len;

	*pkey = gc->key;

	if (*gc->key == '*') {
		return LKP_WILD;
	}

	/* Indirect map ghost, return key */
	if (*gc->key != '/')
		return LKP_MATCH;

	/* Base path of direct map, each new dir needs to be mounted */
	if (!strncmp(gc->root, "/-", 2)) {
		slash = strchr(gc->key + 1, '/');

		if (*gc->key != '/' || !slash) {
			return LKP_ERR_FORMAT;
		}

		*slash = '\0';
		len = strlen(gc->key);
		if (strncmp(gc->direct_base, gc->key, len)) {
			strncpy(gc->direct_base, gc->key, len);
			*(gc->direct_base + len) = '\0';
			sprintf(gc->mapent, "-fstype=autofs %s", gc->mapname);
			return LKP_MOUNT;
		}
		return LKP_NEXT;
	}

	/* Direct map entry, pick out component of path */
	if (*gc->key == '/') {
		pk = gc->key;
		len = strlen(gc->root);
		if (!strncmp(gc->root, gc->key, len)) {
			len--;
			while ((*proot++ == *pk++) && len)
				len--;
			if (len || *pk++ != '/')
				return LKP_NOMATCH;
			slash = strchr(pk, '/');
			*pkey = pk;
			/* Path component, internal mount for lookup_mount */
			if (slash && (!ghosted ||
				      (kproto_version >= 4 && kproto_sub_version < 2))) {
				*slash = '\0';
				sprintf(gc->mapent, "-fstype=autofs %s", gc->mapname);
			}
			return LKP_MATCH;
		}
	}
	return LKP_NOMATCH;
}
