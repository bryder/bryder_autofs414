#ident "$Id: lookup_program.c,v 1.7 2004/12/31 06:30:09 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  lookup_program.c - module for Linux automount to access an
 *                     automount map via a query program 
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *   Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
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
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MODULE_LOOKUP
#include "automount.h"

#define MAPFMT_DEFAULT "sun"

#define MODPREFIX "lookup(program): "

struct lookup_context {
	const char *mapname;
	struct parse_mod *parse;
};

int lookup_version = AUTOFS_LOOKUP_VERSION;	/* Required by protocol */

int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context)
{
	struct lookup_context *ctxt;

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
		crit(MODPREFIX "program map %s is not an absolute pathname",
		       ctxt->mapname);
		return 1;
	}

	if (access(ctxt->mapname, X_OK)) {
		crit(MODPREFIX "program map %s missing or not executable",
		       ctxt->mapname);
		return 1;
	}

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
	char *mapent, *mapp, *tmp;
	char errbuf[1024], *errp;
	char ch;
	int pipefd[2], epipefd[2];
	pid_t f;
	int files_left;
	int status;
	fd_set readfds, ourfds;
	enum state { st_space, st_map, st_done } state;
	int quoted = 0;
	int ret = 1;
	int max_fd;
	int distance;
	int alloci = 1;

	debug(MODPREFIX "looking up %s", name);

	mapent = (char *)malloc(MAPENT_MAX_LEN + 1);
	if (!mapent) {
		error(MODPREFIX "malloc: %s\n", strerror(errno));
		return 1;
	}

	/*
	 * We don't use popen because we don't want to run /bin/sh plus we
	 * want to send stderr to the syslog, and we don't use spawnl()
	 * because we need the pipe hooks
	 */

	if (pipe(pipefd)) {
		error(MODPREFIX "pipe: %m");
		goto out_free;
	}
	if (pipe(epipefd)) {
		close(pipefd[0]);
		close(pipefd[1]);
		goto out_free;
	}

	f = fork();
	if (f < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		close(epipefd[0]);
		close(epipefd[1]);
		error(MODPREFIX "fork: %m");
		goto out_free;
	} else if (f == 0) {
		reset_signals();
		close(pipefd[0]);
		close(epipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(epipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		close(epipefd[1]);
		execl(ctxt->mapname, ctxt->mapname, name, NULL);
		_exit(255);	/* execl() failed */
	}
	close(pipefd[1]);
	close(epipefd[1]);

	mapp = mapent;
	errp = errbuf;
	state = st_space;

	FD_ZERO(&ourfds);
	FD_SET(pipefd[0], &ourfds);
	FD_SET(epipefd[0], &ourfds);

	max_fd = pipefd[0] > epipefd[0] ? pipefd[0] : epipefd[0];

	files_left = 2;

	while (files_left != 0) {
		readfds = ourfds;
		if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0 && errno != EINTR)
			break;

		/* Parse maps from stdout */
		if (FD_ISSET(pipefd[0], &readfds)) {
			if (read(pipefd[0], &ch, 1) < 1) {
				FD_CLR(pipefd[0], &ourfds);
				files_left--;
				state = st_done;
			}

			if (!quoted && ch == '\\') {
				quoted = 1;
				continue;
			}

			switch (state) {
			case st_space:
				if (quoted || !isspace(ch)) {
					*mapp++ = ch;
					state = st_map;
				}
				break;
			case st_map:
				if (!quoted && ch == '\n') {
					*mapp = '\0';
					state = st_done;
					break;
				}

				/* We overwrite up to 3 characters, so we
				 * need to make sure we have enough room
				 * in the buffer for this. */
				/* else */
				if (mapp - mapent > 
				    ((MAPENT_MAX_LEN+1) * alloci) - 3) {
					/*
					 * Alloc another page for map entries.
					 */
					distance = mapp - mapent;
					tmp = realloc(mapent,
						      ((MAPENT_MAX_LEN + 1) * 
						       ++alloci));
					if (!tmp) {
						alloci--;
						error(MODPREFIX "realloc: %s\n",
						      strerror(errno));
						break;
					}
					mapent = tmp;
					mapp = tmp + distance;
				}
				/* 
				 * Eat \ quoting \n, otherwise pass it
				 * through for the parser
				 */
				if (quoted) {
					if (ch == '\n')
						*mapp++ = ' ';
					else {
						*mapp++ = '\\';
						*mapp++ = ch;
					}
				} else
					*mapp++ = ch;
				break;
			case st_done:
				/* Eat characters till there's no more output */
				break;
			}
		}
		quoted = 0;

		/* Deal with stderr */
		if (FD_ISSET(epipefd[0], &readfds)) {
			if (read(epipefd[0], &ch, 1) < 1) {
				FD_CLR(epipefd[0], &ourfds);
				files_left--;
			} else if (ch == '\n') {
				*errp = '\0';
				if (errbuf[0])
					error(">> %s", errbuf);
				errp = errbuf;
			} else {
				if (errp >= &errbuf[1023]) {
					*errp = '\0';
					error(">> %s", errbuf);
					errp = errbuf;
				}
				*(errp++) = ch;
			}
		}
	}

	if (mapp)
		*mapp = '\0';
	if (errp > errbuf) {
		*errp = '\0';
		error(">> %s", errbuf);
	}

	close(pipefd[0]);
	close(epipefd[0]);

	if (waitpid(f, &status, 0) != f) {
		error(MODPREFIX "waitpid: %m");
		goto out_free;
	}

	if (mapp == mapent || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		error(MODPREFIX "lookup for %s failed", name);
		goto out_free;
	}

	debug(MODPREFIX "%s -> %s", name, mapent);

	ret = ctxt->parse->parse_mount(root, name, name_len,
				       mapent, ctxt->parse->context);
out_free:
	free(mapent);
	return ret;
}

int lookup_done(void *context)
{
	struct lookup_context *ctxt = (struct lookup_context *) context;
	int rv = close_parse(ctxt->parse);
	free(ctxt);
	return rv;
}
