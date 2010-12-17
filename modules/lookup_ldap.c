#ident "$Id: lookup_ldap.c,v 1.21 2005/02/27 05:37:14 raven Exp $"
/*
 * lookup_ldap.c - Module for Linux automountd to access automount
 *		   maps in LDAP directories.
 *
 *   Copyright 2001-2003 Ian Kent <raven@themaw.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <lber.h>
#include <ldap.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MAPFMT_DEFAULT "sun"

#define MODPREFIX "lookup(ldap): "

/*
 *  Automount entries are stored hierarchically, with the map name as the base
 *  dn for searches on entries for that map.  Thus, to obtain the base dn for
 *  the master map, one would use the following filter:
 *    (&(objectclass=<map_object_class>)(<map_name_attr>="auto.master"))
 *  Once the base dn is obtained (using ldap_get_first_entry, followed by
 *  ldap_get_dn), the following filter will return all entries for the given
 *  map:
 *    (objectclass=<entry_object_class>)
 *  The attributes of interest are <entry_key_attr>, or the key, and
 *  <entry_value_attr> or the value portion of the automount map entry.
 */
struct autofs_schema {
	char *map_object_class;
	char *map_name_attr;

	char *entry_object_class;
	char *entry_key_attr;
	char *entry_value_attr;
};


#define NR_SCHEMAS	3
struct autofs_schema supported_schemas[NR_SCHEMAS] = {
	{ "automountMap", "automountMapName",
	  "automount", "automountKey", "automountInformation" },
	{ "automountMap", "ou", "automount", "cn", "automountInformation" },
	{ "nisMap", "nisMapName", "nisObject", "cn", "nisMapEntry" },
};

struct lookup_context {
	char *server, *base;
	int port;

	/* once we find a schema that works, save it for future lookups  -
	 * unless the use_old_ldap_lookup optoin is in effect in which case every schema is search every time.
	 */
	struct autofs_schema *schema;

	struct parse_mod *parse;
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

void set_schema(struct lookup_context *ctxt, struct autofs_schema *schema)
{
	if (!ap.use_old_ldap_lookup) 
		ctxt->schema = schema;
}


static LDAP *do_connect(struct lookup_context *ctxt, int *result_ldap)
{
	LDAP *ldap;
	int version = 3;
	int timeout = 8;
	int rv;

	/* Initialize the LDAP context. */
	ldap = ldap_init(ctxt->server, ctxt->port);
	if (!ldap) {
		crit(MODPREFIX "%s: couldn't initialize LDAP connection"
		     " to %s", __func__, ctxt->server ? ctxt->server : "default server");
		return NULL;
	}

	/* Use LDAPv3 */
	rv = ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &version);
	if (rv != LDAP_SUCCESS) {
		/* fall back to LDAPv2 */
		ldap_unbind(ldap);
		ldap = ldap_init(ctxt->server, ctxt->port);
		if (!ldap) {
			crit(MODPREFIX "%s: couldn't initialize LDAP for v2", __func__ );
			return NULL;
		} else {
			version = 2;
		}
	}

	/* Sane network connection timeout */
	rv = ldap_set_option(ldap, LDAP_OPT_NETWORK_TIMEOUT, &timeout);
	if (rv != LDAP_SUCCESS) {
		warn(MODPREFIX 
		     "%s: failed to set connection timeout to %d", __func__, timeout);
	}

	/* Connect to the server as an anonymous user. */
	if (version == 2)
		rv = ldap_simple_bind_s(ldap, ctxt->base, NULL);
	else
		rv = ldap_simple_bind_s(ldap, NULL, NULL);

	if (rv != LDAP_SUCCESS) {
		crit(MODPREFIX "couldn't bind to %s",
		     ctxt->server ? ctxt->server : "default server");
		*result_ldap = rv;
		return NULL;
	}

	return ldap;
}

/*
 * This initializes a context (persistent non-global data) for queries to
 * this module.  Return zero if we succeed.
 */
int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt = NULL;
	int l, rv;
	LDAP *ldap;
	char *ptr = NULL;

	/* If we can't build a context, bail. */
	ctxt = (struct lookup_context *) malloc(sizeof(struct lookup_context));
	*context = ctxt;
	if (ctxt == NULL) {
		crit(MODPREFIX "malloc: %m");
		return 1;
	}
	memset(ctxt, 0, sizeof(struct lookup_context));

	/* If a map type isn't explicitly given, parse it like sun entries. */
	if (mapfmt == NULL) {
		mapfmt = MAPFMT_DEFAULT;
	}

	/* Now we sanity-check by binding to the server temporarily. We have
	 * to be a little strange in here, because we want to provide for
	 * use of the "default" server, which is set in an ldap.conf file
	 * somewhere. */

	ctxt->server = NULL;
	ctxt->port = LDAP_PORT;
	ctxt->base = NULL;

	ptr = (char *) argv[0];

	if (!strncmp(ptr, "//", 2)) {
		char *s = ptr + 2;
		char *p = NULL, *q = NULL;

		/* Isolate the server's name and possibly port. But the : breaks
		   the SUN parser for submounts so we can't actually use it.
		 */
		if ((q = strchr(s, '/'))) {
			if ((p = strchr(s, ':'))) {
				l = p - s;
				p++;
				ctxt->port = atoi(p);
			} else {
				l = q - s;
			}

			ctxt->server = malloc(l + 1);
			memset(ctxt->server, 0, l + 1);
			memcpy(ctxt->server, s, l);

			ptr = q + 1;
		}
	} else if (strchr(ptr, ':') != NULL) {
		l = strchr(ptr, ':') - ptr;
		/* Isolate the server's name. */
		ctxt->server = malloc(l + 1);
		memset(ctxt->server, 0, l + 1);
		memcpy(ctxt->server, argv[0], l);
		ptr += l+1;
	}

	/* Isolate the base DN. */
	l = strlen(ptr);
	ctxt->base = malloc(l + 1);
	memset(ctxt->base, 0, l + 1);
	memcpy(ctxt->base, ptr, l);

	debug(MODPREFIX "%s: server = \"%s\", port = %d, base dn = \"%s\"", __func__,
		  ctxt->server ? ctxt->server : "(default)",
		  ctxt->port, ctxt->base);

	/* Initialize the LDAP context. */
	ldap = do_connect(ctxt, &rv);
	if (!ldap)
		return 1;

	/* Okay, we're done here. */
	ldap_unbind(ldap);

	/* Open the parser, if we can. */
	return !(ctxt->parse = open_parse(mapfmt, MODPREFIX, argc - 1, argv + 1));
}

static int read_one_map(LDAP *ldap, const char *root,
			struct autofs_schema *schema,			
			struct lookup_context *ctxt,
			time_t age, int *result_ldap)
{
	int rv, i, j, l, count, keycount;
	char *query;
	LDAPMessage *result, *e;
	char **keyValue = NULL;
	char **values = NULL;
	char *attrs[] = { schema->entry_key_attr,
			  schema->entry_value_attr,
			  NULL };
	const char *class = schema->entry_object_class,
		   *key = schema->entry_key_attr,
		   *type = schema->entry_value_attr;
	int found_entry = 0;

	if (ctxt == NULL) {
		crit(MODPREFIX "%s: context was NULL", __func__);
		return 0;
	}

	/* Build a query string. */
	l = strlen("(objectclass=)") + strlen(class) + 1;

	query = alloca(l);
	if (query == NULL) {
		crit(MODPREFIX "malloc: %m");
		return 0;
	}

	memset(query, '\0', l);
	if (sprintf(query, "(objectclass=%s)", class) >= l) {
		debug(MODPREFIX "error forming query string");
	}
	query[l - 1] = '\0';


	/* Look around. */
	debug(MODPREFIX "%s: searching for \"%s\" under \"%s\"", __func__, query, ctxt->base);

	rv = ldap_search_s(ldap, ctxt->base, LDAP_SCOPE_SUBTREE,
			   query, attrs, 0, &result);

	if ((rv != LDAP_SUCCESS) || !result) {
	        crit(MODPREFIX "%s: query failed for %s: %s", __func__, query, ldap_err2string(rv));
		*result_ldap = rv;
		return 0;
	}

	e = ldap_first_entry(ldap, result);
	if (!e) {
		debug(MODPREFIX "%s: query succeeded, no matches for %s", __func__, query);
		ldap_msgfree(result);
		return 0;
	} else
		debug(MODPREFIX "%s: examining entries", __func__ );

	while (e) {
		keyValue = ldap_get_values(ldap, e, key);

		if (!keyValue || !*keyValue) {
			e = ldap_next_entry(ldap, e);
			continue;
		}

		found_entry = 1;

		values = ldap_get_values(ldap, e, type);
		if (!values) {
			info(MODPREFIX "%s: no %s defined for %s", __func__, type, query);
			ldap_value_free(keyValue);
			e = ldap_next_entry(ldap, e);
			continue;
		}

		count = ldap_count_values(values);
		keycount = ldap_count_values(keyValue);
		for (i = 0; i < count; i++) {
			for (j = 0; j < keycount; j++) {
				if (*(keyValue[j]) == '/' &&
				    strlen(keyValue[j]) == 1)
					*(keyValue[j]) = '*';
				cache_add(root, keyValue[j], values[i], age);
			}
		}
		ldap_value_free(values);

		ldap_value_free(keyValue);
		e = ldap_next_entry(ldap, e);
	}

	debug(MODPREFIX "%s: done updating map", __func__ );

	/* Clean up. */
	ldap_msgfree(result);

	if (found_entry) 
		return 1;
	else
		return 0;
}

static int read_map(const char *root, struct lookup_context *ctxt,
		    time_t age, int *result_ldap)
{
	LDAP *ldap;
	int rv = LDAP_SUCCESS;
	int ret, i;

	/* Initialize the LDAP context. */
	ldap = do_connect(ctxt, &rv);
	if (!ldap) {
		if (rv != LDAP_SUCCESS)
			*result_ldap = rv;
		return 0;
	}

	/* all else fails read entire map */
	if (ctxt->schema) {
		ret = read_one_map(ldap, root, ctxt->schema, ctxt, age, &rv);
		if (ret == 1 && rv == LDAP_SUCCESS)
			goto ret_ok;
	} else {
		for (i = 0; i < NR_SCHEMAS; i++) {
			ret = read_one_map(ldap, root,
					   &supported_schemas[i],
					   ctxt, age, &rv);
			if (ret == 1 && rv == LDAP_SUCCESS) {
				set_schema(ctxt, &supported_schemas[i]);
				goto ret_ok;
			}
		}
	}
 
	ldap_unbind(ldap);
	if (result_ldap)
		*result_ldap = rv;

	return 0;

ret_ok:
	/* Clean stale entries from the cache */
	cache_clean(root, age);
	ldap_unbind(ldap);
	return 1;
}

int lookup_ghost(const char *root, int ghost, time_t now, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	struct mapent_cache *me;
	int status = 1, rv = LDAP_SUCCESS;
	char *mapname;
	time_t age = now ? now : time(NULL);

	chdir("/");

	if (!read_map(root, ctxt, age, &rv))
		switch (rv) {
		case LDAP_SIZELIMIT_EXCEEDED:
		case LDAP_UNWILLING_TO_PERFORM:
			return LKP_NOTSUP;
		default:
			return LKP_FAIL;
		}

	if (ctxt->server) {
		int len = strlen(ctxt->server) + strlen(ctxt->base) + 4;

		mapname = alloca(len);
		sprintf(mapname, "//%s/%s", ctxt->server, ctxt->base);
	} else {
		mapname = alloca(strlen(ctxt->base) + 1);
		sprintf(mapname, "%s", ctxt->base);
	}

	status = cache_ghost(root, ghost, mapname, "ldap", ctxt->parse);

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

static int lookup_one_schema(LDAP *ldap, const char *root, const char *qKey,
			     struct autofs_schema *schema,
			     struct lookup_context *ctxt)
{
	int rv, i, l, ql;
	time_t age = time(NULL);
	char *query;
	LDAPMessage *result, *e;
	char **values = NULL;
	char *attrs[] = { schema->entry_key_attr,
			  schema->entry_value_attr,
			  NULL };
	const char *class = schema->entry_object_class,
		   *key = schema->entry_key_attr,
		   *type = schema->entry_value_attr;
	struct mapent_cache *me = NULL;
	int ret = CHE_OK;

	if (ctxt == NULL) {
		crit(MODPREFIX "%s: context was NULL", __func__ );
		return 0;
	}

	/* Build a query string. */
	l = strlen("(&(objectclass=") + strlen(class) + strlen(")");
	l += strlen("(") + strlen(key) + strlen("=") 
				+ strlen(qKey) + strlen("))") + 1;

	query = alloca(l);
	if (query == NULL) {
		crit(MODPREFIX "%s: alloca returned NULL", __func__ );
		return 0;
	}

	/* Look around. */
	memset(query, '\0', l);
	ql = sprintf(query, "(&(objectclass=%s)(%s=%s))", class, key, qKey);
	if (ql >= l) {
		debug(MODPREFIX "%s: error forming query string", __func__);
		return 0;
	}
	query[l - 1] = '\0';

	debug(MODPREFIX "%s: searching for \"%s\" under \"%s\"", __func__, query, ctxt->base);

	rv = ldap_search_s(ldap, ctxt->base, LDAP_SCOPE_SUBTREE,
			   query, attrs, 0, &result);

	if ((rv != LDAP_SUCCESS) || !result) {
		crit(MODPREFIX "%s: query failed for %s", __func__, query);
		return 0;
	}

	debug(MODPREFIX "%s: getting first entry for %s=\"%s\"", __func__, key, qKey);

	e = ldap_first_entry(ldap, result);
	if (!e) {
		debug(MODPREFIX "%s: got answer, but no first entry for %s", __func__, query);
		ldap_msgfree(result);
		return CHE_MISSING;
	}

	debug(MODPREFIX "%s: examining first entry", __func__);

	values = ldap_get_values(ldap, e, type);
	if (!values) {
		debug(MODPREFIX "%s: no %s defined for %s", __func__, type, query);
		ldap_msgfree(result);
		return CHE_MISSING;
	}

	/* Compare cache entry against LDAP */
	for (i = 0; values[i]; i++) {
		me = cache_lookup(qKey);
		while (me && (strcmp(me->mapent, values[i]) != 0))
			me = cache_lookup_next(me);
		if (!me)
			break;
	}

	if (!me) {
		cache_delete(root, qKey, 0);

		for (i = 0; values[i]; i++) {	
			rv = cache_add(root, qKey, values[i], age);
			if (!rv)
				return 0;
		}

		ret = CHE_UPDATED;
	}

	/* Clean up. */
	ldap_value_free(values);
	ldap_msgfree(result);

	return ret;
}

static int lookup_one(LDAP *ldap, const char *root, const char *qKey,
		      struct lookup_context *ctxt)
{
	int ret, i;

	if (ctxt->schema) {
		ret = lookup_one_schema(ldap, root, qKey, ctxt->schema, ctxt);
		debug("lookup_one with schema %s,%s,%s returns %d\n",
		      ctxt->schema->entry_key_attr,
		      ctxt->schema->entry_object_class,
		      ctxt->schema->entry_value_attr, ret);
	} else {
		for (i = 0; i < NR_SCHEMAS; i++) {
			ret = lookup_one_schema(ldap, root, qKey,
					 &supported_schemas[i], ctxt);
			debug("lookup_one with schema %d returns %d\n",i, ret);
			if (ret != CHE_FAIL) {
				set_schema(ctxt, &supported_schemas[i]);
				break;
			}
		}
	}

	return ret;
}

static int lookup_wild_schema(LDAP *ldap, const char *root,
			      struct autofs_schema *schema,
			      struct lookup_context *ctxt)
{
	int rv, i, l, ql;
	time_t age = time(NULL);
	char *query;
	LDAPMessage *result, *e;
	char **values = NULL;
	char *attrs[] = { schema->entry_key_attr,
			  schema->entry_value_attr,
			  NULL };
	const char *class = schema->entry_object_class,
		   *key = schema->entry_key_attr,
		   *type = schema->entry_value_attr;
	struct mapent_cache *me = NULL;
	int ret = CHE_OK;
	char qKey[KEY_MAX_LEN + 1];
	int qKey_len;

	if (ctxt == NULL) {
		crit(MODPREFIX "%s: context was NULL", __func__);
		return 0;
	}

	strcpy(qKey, "/");
	qKey_len = 1;

	/* Build a query string. */
	l = strlen("(&(objectclass=") + strlen(class) + strlen(")");
	l += strlen("(") + strlen(key) + strlen("=") + qKey_len + strlen("))") + 1;

	query = alloca(l);
	if (query == NULL) {
		crit(MODPREFIX "%s: malloc failed",__func__);
		return 0;
	}

	/* Look around. */
	memset(query, '\0', l);
	ql = sprintf(query, "(&(objectclass=%s)(%s=%s))", class, key, qKey);
	if (ql >= l) {
		debug(MODPREFIX "%s: error forming query string", __func__);
		return 0;
	}
	query[l - 1] = '\0';

	debug(MODPREFIX "%s: searching for \"%s\" under \"%s\"", __func__, query, ctxt->base);

	rv = ldap_search_s(ldap, ctxt->base, LDAP_SCOPE_SUBTREE,
			   query, attrs, 0, &result);

	if ((rv != LDAP_SUCCESS) || !result) {
		crit(MODPREFIX "%s: query failed for %s", __func__, query);
		return 0;
	}

	debug(MODPREFIX "%s: getting first entry for %s=\"%s\"", __func__, key, qKey);

	e = ldap_first_entry(ldap, result);
	if (!e) {
		debug(MODPREFIX "%s: got answer, but no first entry for %s", __func__, query);
		ldap_msgfree(result);
		return CHE_MISSING;
	}

	debug(MODPREFIX "%s: examining first entry", __func__);

	values = ldap_get_values(ldap, e, type);
	if (!values) {
		debug(MODPREFIX "%s: no %s defined for %s", __func__, type, query);
		ldap_msgfree(result);
		return CHE_MISSING;
	}

	/* Compare cache entry against LDAP */
	for (i = 0; values[i]; i++) {
		me = cache_lookup("*");
		while (me && (strcmp(me->mapent, values[i]) != 0))
			me = cache_lookup_next(me);
		if (!me)
			break;
	}

	if (!me) {
		cache_delete(root, "*", 0);

		for (i = 0; values[i]; i++) {	
			rv = cache_add(root, "*", values[i], age);
			if (!rv)
				return 0;
		}

		ret = CHE_UPDATED;
	}

	/* Clean up. */
	ldap_value_free(values);
	ldap_msgfree(result);

	return ret;
}


static int lookup_wild(LDAP *ldap, const char *root,
		       struct lookup_context *ctxt)
{
	int ret, i;

	if (ctxt->schema)
		return lookup_wild_schema(ldap, root, ctxt->schema, ctxt);


	for (i = 0; i < NR_SCHEMAS; i++) {
		ret = lookup_wild_schema(ldap, root,
					 &supported_schemas[i], ctxt);
		if (ret != CHE_FAIL) {
			set_schema(ctxt, &supported_schemas[i]);
			break;
		}
	}

	return ret;
}

/* lookup_mount returns 1 if there was some kind of error */
int lookup_mount(const char *root, const char *name, int name_len, void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	LDAP *ldap;
	int ret;
	char key[KEY_MAX_LEN + 1];
	int key_len;
	char mapent[MAPENT_MAX_LEN + 1];
	char *mapname;
	struct mapent_cache *me;
	time_t now = time(NULL);
	time_t t_last_read;
	int need_hup = 0;

	if (ap.type == LKP_DIRECT)
		key_len = snprintf(key, KEY_MAX_LEN, "%s/%s", root, name);
	else
		key_len = snprintf(key, KEY_MAX_LEN, "%s", name);

	if (key_len > KEY_MAX_LEN)
		return 1;


	/* Initialize the LDAP context. */
	ldap = do_connect(ctxt, NULL);
	if (!ldap)
		return 0;
	
	ret = lookup_one(ldap, root, key, ctxt);
	if (ret == CHE_FAIL) {
		ldap_unbind(ldap);
		return 1;
	}

	me = cache_lookup_first();
	t_last_read = me ? now - me->age : ap.exp_runfreq + 1;

	if (t_last_read > ap.exp_runfreq) 
		if (ret & (CHE_MISSING | CHE_UPDATED))
			need_hup = 1;


	if (ret == CHE_MISSING) {
		int wild = CHE_MISSING;

		/* Maybe update wild card map entry */
		if (ap.type == LKP_INDIRECT) {
			ret = lookup_wild(ldap, root, ctxt);
			wild = (ret & (CHE_MISSING | CHE_FAIL));

			if (ret & CHE_MISSING)
				cache_delete(root, "*", 0);
		}

		if (cache_delete(root, key, 0) && wild)
			rmdir_path(key);
	}
	ldap_unbind(ldap);

	me = cache_lookup(key);
	if (me) {
		/* Try each of the LDAP entries in sucession. */
		while (me) {
			sprintf(mapent, me->mapent);

			debug(MODPREFIX "%s: %s -> %s", __func__, key, mapent);
			ret = ctxt->parse->parse_mount(root, name, name_len,
						  mapent, ctxt->parse->context);
			me = cache_lookup_next(me);
		}
	} else {
		/* path component, do submount */
		me = cache_partial_match(key);
		if (me) {
			if (ctxt->server) {
				int len = strlen(ctxt->server) +
					    strlen(ctxt->base) + 2 + 1 + 1;
				mapname = alloca(len);
				sprintf(mapname, "//%s/%s", ctxt->server, ctxt->base);
			} else {
				mapname = alloca(strlen(ctxt->base) + 1);
				sprintf(mapname, "%s", ctxt->base);
			}
			sprintf(mapent, "-fstype=autofs ldap:%s", mapname);

			debug(MODPREFIX "%s: %s -> %s", __func__, key, mapent);
			ret = ctxt->parse->parse_mount(root, name, name_len,
						  mapent, ctxt->parse->context);
		}
	}

	/* Have parent update its map */
	if (need_hup)
		kill(getppid(), SIGHUP);

	return ret;
}

/*
 * This destroys a context for queries to this module.  It releases the parser
 * structure (unloading the module) and frees the memory used by the context.
 */
int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv = close_parse(ctxt->parse);
	free(ctxt->server);
	free(ctxt->base);
	free(ctxt);
	return rv;
}
