/*
 * Adapted from "showmount"
*/

#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <memory.h>
#include <stdlib.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>

#include "mount.h"

#define MAXHOSTLEN 256

char *program_name;

/*
from mount.x:
typedef string name<MNTNAMLEN>;
typedef string dirpath<MNTPATHLEN>;

typedef struct groupnode *groups;
struct groupnode {
	name gr_name;
	groups gr_next;
};

typedef struct exportnode *exports;
struct exportnode {
	dirpath ex_dir;
	groups ex_groups;
	exports ex_next;
};

so dirpath and name are strings (char *), \0 terminated of course
groups is a list of names
exports is a list of dirpath and groups
*/

/* returns NULL on error or no exports available */
exports get_export_list(char *hostname)
{
	struct hostent *hp;
	struct sockaddr_in server_addr;
	int msock;
	CLIENT *mclient;
	exports exportlist;
	enum clnt_stat clnt_stat;
	struct timeval total_timeout;
	struct timeval pertry_timeout;

	/* get the servers address info all squared away */
	if (inet_aton(hostname, (struct in_addr *) &server_addr.sin_addr.s_addr)) {
		server_addr.sin_family = AF_INET;
	} else {
		if ((hp = gethostbyname(hostname)) == NULL) {
			fprintf(stderr, "%s: can't get address for %s\n",
				program_name, hostname);
			return (NULL);
		}
		server_addr.sin_family = AF_INET;
		memcpy(&server_addr.sin_addr, hp->h_addr, hp->h_length);
	}

	/* create a client object.
	 * first try a UDP client. if not
	 * possible, then fall back to * using UDP*/
	server_addr.sin_port = 0;
	msock = RPC_ANYSOCK;
	if ((mclient = clnttcp_create(&server_addr,
				      MOUNTPROG, MOUNTVERS, &msock, 0, 0)) == NULL) {
		server_addr.sin_port = 0;
		msock = RPC_ANYSOCK;
		pertry_timeout.tv_sec = 3;
		pertry_timeout.tv_usec = 0;
		if ((mclient = clntudp_create(&server_addr,
					      MOUNTPROG, MOUNTVERS, pertry_timeout,
					      &msock)) == NULL) {
			clnt_pcreateerror("mount clntudp_create");
			return (NULL);
		}
	}
	mclient->cl_auth = authunix_create_default();
	total_timeout.tv_sec = 20;
	total_timeout.tv_usec = 0;

	/* Ok, get a list of exports from the server */
	memset(&exportlist, '\0', sizeof(exportlist));
	clnt_stat = clnt_call(mclient, MOUNTPROC_EXPORT,
			      (xdrproc_t) xdr_void, NULL,
			      (xdrproc_t) xdr_exports, (caddr_t) & exportlist,
			      total_timeout);
	if (clnt_stat != RPC_SUCCESS) {
		clnt_perror(mclient, "rpc mount export");
		return (NULL);
	}

	return (exportlist);
}

void exports_free(exports item)
{
	groups grp;
	groups tmp;
	if (item->ex_dir)
		free(item->ex_dir);

	grp = item->ex_groups;
	while (grp) {
		if (grp->gr_name)
			free(grp->gr_name);
		tmp = grp;
		grp = grp->gr_next;
		free(tmp);
	}

	free(item);
}

void export_list_free(exports list)
{
	exports tmp;

	while (list) {
		tmp = list;
		list = list->ex_next;
		exports_free(tmp);
	}
	return;
}

/* return true if dir fits spec, false otherwise */
/*
  so this is what we want to match:
     start&end
  either start or end could be null
*/

int is_match(char *spec, char *dir)
{
	int stat = 0;
	char *start = NULL;	/* a copy of spec because we alter it. */
	char *end = NULL;
	char *orig = NULL;
	int startlen, endlen, backlen;
	char *d, *e;

	//printf("is_match: testing %s\n", dir);
	/* is it a lone '&'? */
	if (strlen(spec) == 1) {
		if (*spec == '&')
			return (1);
		else
			return (0);
	}

	orig = start = strdup(spec);
	end = strchr(start, '&');
	*end = '\0';
	end++;
	//printf("is_match: start = \"%s\", end = \"%s\"\n", start, end);
	startlen = strlen(start);
	/* does the beginning match? */
	if (strncmp(start, dir, startlen) != 0)
		goto done;

	//printf("is_match: passes the start test\n");
	if (*end == '\0') {
		//printf("is_match: ok *end == \\0\n");
		goto good;
	}

	/* ok, the end part is funky because we need to go backwards... */
	/* there's prolly better ways to do this but... */
	endlen = strlen(end);
	e = &end[endlen - 1];

	backlen = strlen(dir);
	d = &dir[backlen - 1];
	backlen = backlen - startlen;

	if (endlen > backlen)
		goto done;

	//printf("is_match: looping: ");
	while (*e != '\0') {
		if (*e != *d) {
			//printf("fails the end test\n");
			goto done;
		}
		//printf("%c.", *e);
		e--;
		d--;
	}

      good:
	//printf("\n");
	//printf("passes the end test\n");
	stat = 1;

      done:
	//printf("done...\n");
	if (orig)
		free(orig);

	return (stat);
}

/* returns NULL on error or no exports available */
exports prune_export_list(exports list, char *spec)
{
	exports exl = NULL;
	exports prev = NULL;	/* keep this around for deletion */
	exports head = list;

	exl = list;
	prev = NULL;
	while (exl) {
		/* check it here, if we need to prune it: */
		if (!is_match(spec, exl->ex_dir)) {
			/* delete the entry from the list here: */
			if (prev == NULL) {
				/* only if we're deleting the head entry */
				prev = exl;
				head = exl = exl->ex_next;
				exports_free(prev);
				prev = NULL;
				continue;
			} else {
				exl = exl->ex_next;
				free(prev->ex_next);
				prev->ex_next = exl;
				continue;
			}
		}
		/* no deletion, iterate */
		prev = exl;
		exl = exl->ex_next;
	}
	return (head);
}

#if 0
/* usage: listmount server */
int main(int argc, char **argv)
{
	char *hostname;
	exports exportlist = NULL, exl;
	groups grouplist;
	int n;
	int maxlen;
	char *wildcard = NULL;

	program_name = argv[0];

	if (argc != 1 && argc != 2 && argc != 3) {
		printf("USAGE: %s SERVER [MASK]\n", program_name);
		printf("List all the NFS mounts on SERVER which we are"
		       " allowed to access.\n");
		printf("Optional MASK is a mountpoint specification with "
		       "a wildcard (&), ex: /vol/vol1/& will only show "
		       "those mountpoints starting with /vol/vol1\n");
		printf("A spec of /vol/vol1/&/foo will show all mountpoints "
		       "starting with /vol/vol1 and ending in /foo\n");
		return (1);
	}

	hostname = argv[1];
	if (argc == 3)
		wildcard = argv[2];

	printf("Export list for %s:\n", hostname);
	exportlist = get_export_list(hostname);

	if (wildcard)
		exportlist = prune_export_list(exportlist, wildcard);

	maxlen = 0;
	for (exl = exportlist; exl; exl = exl->ex_next) {
		if ((n = strlen(exl->ex_dir)) > maxlen)
			maxlen = n;
	}
	while (exportlist) {
		printf("%-*s ", maxlen, exportlist->ex_dir);
		grouplist = exportlist->ex_groups;
		if (grouplist)
			while (grouplist) {
				printf("%s%s", grouplist->gr_name,
				       grouplist->gr_next ? "," : "");
				grouplist = grouplist->gr_next;
		} else
			printf("(everyone)");
		printf("\n");
		exportlist = exportlist->ex_next;
	}

	return (0);
}
#endif
