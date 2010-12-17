#ifndef _LISTMOUNT_H_
#define _LISTMOUNT_H_

#include "mount.h"

/* get the whole list of exports from a server */
exports get_export_list(char *hostname);

/* free all the exports in a list */
void export_list_free(exports list);

/* free a single exports data structure */
void exports_free(exports item);

/* determine whether the spec and the directory match in a wildcard */
int is_match(char *spec, char *dir);

/* removes exports which don't match spec from the list. */
exports prune_export_list(exports list, char *spec);

#endif /* _LISTMOUNT_H_ */
