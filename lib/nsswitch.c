#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rpcsvc/ypclnt.h>
#include <netdb.h>
#include "automount.h"

#define MODPREFIX "nsswitch: "

/*
 * Function which takes in a partial map name (ie. auto.misc), parses the
 * nsswitch.conf file and returns a valid map name (ie. yp:auto.misc or
 * file:/etc/auto.misc
 */
char *get_nsswitch_map(const char *loc)
{
	char buf[1024];
	char *ordering;
	const char *automount_str = "automount:";
	char *comment = NULL;
	FILE *nsswitch;
	int found_automount = 0;
	char *retval = NULL;
	int retsize = 0;

	debug(MODPREFIX "called nsswitch with: '%s'", loc);
	nsswitch = fopen(_PATH_NSSWITCH_CONF, "r");
	if (!nsswitch) {
		error(MODPREFIX "Unable to open %s", _PATH_NSSWITCH_CONF);
		return NULL;
	}

	while ((ordering = fgets((char *)buf, sizeof(buf), nsswitch))) {
		if ((comment = strchr(ordering,'#')))
			*comment = '\0';
		while (isspace(*ordering)) ordering++;
		if (!strncmp(ordering, automount_str, sizeof(automount_str))) {
			ordering += strlen(automount_str);
			found_automount = 1;
			break;
		}
	}

	fclose(nsswitch);

	if (!found_automount)
		return NULL;

	while (*ordering != '\0') {
		while (isspace(*ordering)) ordering++;
		if (!strncmp(ordering, "files", 5)) {
			switch (isfilemap(loc)) {
				case MAPTYPE_FILE:
					retsize = strlen(loc) + 11;
					retval = malloc(retsize);
					if (!retval)
						return NULL;
					snprintf(retval, retsize,
							"file:/etc/%s", loc);
					return retval;
				case MAPTYPE_PROGRAM:
					retsize = strlen(loc) + 14;
					retval = malloc(retsize);
					if (!retval)
						return NULL;
					snprintf(retval, retsize,
							"program:/etc/%s", loc);
					return retval;
				default: // filemap doesn't exist
					break;
			}

		} else if ((!strncmp(ordering, "yp", 2) ||
					!strncmp(ordering,"nis", 3)) &&
				isypmap(loc)) {
			retsize = strlen(loc) + 4;
			retval = malloc(retsize);
			snprintf(retval, retsize, "yp:%s", loc);
			return retval;
		}
		while (!isspace(*ordering) && (*ordering != '\0')) ordering++;
	}
	error(MODPREFIX "couldn't find map %s", loc);
	return retval;
}

/*
 * Function takes in a filename and tests if it exists in "/etc/"
 * Returns: MAPTYPE_FILE if it is not executable, MAPTYPE_PROGRAM if it
 * is executable and 0 if it doesn't exists or has incorrect permissions.
 */

int isfilemap(const char *loc)
{
	struct stat st;
	int ret = 0;	
	char *realfilemap;

	realfilemap = malloc(strlen(loc) + 6); /* '/etc/' + '\0' */
	if (!realfilemap) {
		crit(MODPREFIX "malloc failed.");
		return 0;
	}

	snprintf(realfilemap, strlen(loc) + 6, "/etc/%s", loc);

	ret = stat(realfilemap, &st);
	free (realfilemap);

	if (!ret) {
		if (st.st_uid != 0) {
			error(MODPREFIX "file /etc/%s exists but is not"
					" owned by root.", loc);
			return 0;
		} else if (st.st_mode & S_IRUSR) {
			if (st.st_mode & S_IXUSR) 
				return MAPTYPE_PROGRAM;
			else
				return MAPTYPE_FILE;
		}
	}
	return 0;
}

/*
 * Function takes in a yp map name and returns
 * 1 if it exists or 0 if it doesn't.
 *
 * Some of this code borrowed from ypcat
 */

int isypmap(const char *loc)
{
	int err;
	char *domainname;
	unsigned int order;

	if ((err = yp_get_default_domain(&domainname)) != YPERR_SUCCESS) {
		error(MODPREFIX "unable to get default yp domain");
		return 0;
	}
	if ((err = yp_order(domainname, loc, &order)) != YPERR_SUCCESS) {
		debug(MODPREFIX "unable to find map, %s in domain, %s",
		      loc, domainname);
		return 0;
	}

	return 1;
}
