#ident "$Id: cat_path.c,v 1.4 2005/04/06 15:14:23 raven Exp $"
/* ----------------------------------------------------------------------- *
 *
 *  cat_path.c - boundary aware buffer management routines
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

#include <alloca.h>
#include <string.h>
#include <limits.h>
#include <syslog.h>
#include <ctype.h>

/*
 * sum = "dir/base" with attention to buffer overflows, and multiple
 * slashes at the joint are avoided.
 */
int cat_path(char *buf, size_t len, const char *dir, const char *base)
{
	char *d = (char *) dir;
	char *b = (char *) base;
	char *s = buf;
	size_t left = len;

	if ((*s = *d))
		while ((*++s = *++d) && --left) ;
	
	if (!left) {
		*s = '\0';
		return 0;
	}

	/* Now we have at least 1 left in output buffer */

	while (*--s == '/' && (left++ < len))
		*s = '\0';

	*++s = '/';
	left--;

	if (*b == '/') 
		while (*++b == '/');

	while (--left && (*++s = *b++)) ;

	if (!left) {
		*s = '\0';
		return 0;
	}

	return 1;
}

int _strlen(const char *str, int max)
{
	char *s = (char *) str;

	while (*s++ && max--) ;

	if (max < 0)
		return 0;
	
	return s - str - 1;
}

/* 
 * sum = "dir/base" with attention to buffer overflows, and multiple
 * slashes at the joint are avoided.  The length of base is specified
 * explicitly.
 */
int ncat_path(char *buf, size_t len,
	      const char *dir, const char *base, size_t blen)
{
	char name[PATH_MAX+1];
	int alen = _strlen(base, blen);

	if (blen > PATH_MAX || !alen)
		return 0;
	
	strncpy(name, base, alen);
	name[alen] = '\0';

	return cat_path(buf, len, dir, name);
}

