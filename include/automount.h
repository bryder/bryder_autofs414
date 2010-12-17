#ident "$Id: automount.h,v 1.15 2005/01/26 13:03:02 raven Exp $"
/*
 * automount.h
 *
 * Header file for automounter modules
 *
 */

#ifndef AUTOMOUNT_H
#define AUTOMOUNT_H

#include <sys/types.h>
#include <paths.h>
#include <limits.h>
#include <time.h>
#include "config.h"

/* OpenBSD re-entrant syslog */
#include "syslog.h"

/* We MUST have the paths to mount(8) and umount(8) */
#ifndef HAVE_MOUNT
#error Failed to locate mount(8)!
#endif

#ifndef HAVE_UMOUNT
#error Failed to locate umount(8)!
#endif

/* The -s (sloppy) option to mount is good, if we have it... */

#ifdef HAVE_SLOPPY_MOUNT
#define SLOPPYOPT "-s",		/* For use in spawnl() lists */
#define SLOPPY    "-s "		/* For use in strings */
#else
#define SLOPPYOPT
#define SLOPPY
#endif

#define AUTOFS_SUPER_MAGIC 0x00000187L

#define DEFAULT_TIMEOUT (5*60)			/* 5 minutes */
#define AUTOFS_LOCK	"/var/lock/autofs"	/* To serialize access to mount */
#define MOUNTED_LOCK	_PATH_MOUNTED "~"	/* mounts' lock file */
#define MTAB_NOTUPDATED 0x1000			/* mtab succeded but not updated */
#define NOT_MOUNTED     0x0100			/* path notmounted */

/* Constants for lookup modules */

#define LKP_FAIL	0x0001

#define LKP_INDIRECT	0x0002
#define LKP_DIRECT	0x0004
#define LKP_NOMATCH	0x0008
#define LKP_MATCH	0x0010
#define LKP_NEXT	0x0020
#define LKP_MOUNT	0x0040
#define LKP_WILD	0x0080

#define LKP_LOOKUP	0x0100
#define LKP_GHOST	0x0200
#define LKP_REREAD	0x0400

#define LKP_ERR_FORMAT	0x1000
#define LKP_ERR_MOUNT	0x2000
#define LKP_NOTSUP	0x4000

#ifdef DEBUG
#define DB(x)           do { x; } while(0)
#else
#define DB(x)           do { } while(0)
#endif

/*
 * State machine for daemon
 * 
 * READY - reads from pipe; performs mount/umount operations
 * PRUNE - generates prune events in subprocess; reads from pipe
 * READMAP - read read map for maps taht use cache
 * EXPIRE - generates expire events in subprocess; reads from pipe
 * SHUTDOWN_PENDING - as prune, but goes to SHUTDOWN when done
 * SHUTDOWN - unmount autofs, exit
 *
 * Signals TERM, USR1, USR2, HUP and ALRM are blocked in all states except
 * READY.  SIGCHLD is blocked when protecting the manipulating mount list.
 */
enum states {
	ST_INVAL = -1,
	ST_INIT,
	ST_READY,
	ST_EXPIRE,
	ST_PRUNE,
	ST_READMAP,
	ST_SHUTDOWN_PENDING,
	ST_SHUTDOWN,
};

struct pending_mount {
	pid_t pid;		/* Which process is mounting for us */
	unsigned long wait_queue_token;	/* Associated kernel wait token */
	volatile struct pending_mount *next;
};

struct autofs_point {
	char *path;			/* Mount point name */
	int pipefd;			/* File descriptor for pipe */
	int ioctlfd;			/* File descriptor for ioctls */
	dev_t dev;			/* "Device" number assigned by kernel */
	char *maptype;			/* Type of map "file", "NIS", etc */
	unsigned int type;		/* Type of map direct or indirect */
	time_t exp_timeout;		/* Timeout for expiring mounts */
	time_t exp_runfreq;		/* Frequency for polling for timeouts */
	unsigned ghost;			/* Enable/disable gohsted directories */
	volatile pid_t exp_process;		/* Process that is currently expiring */
	volatile struct pending_mount *mounts;	/* Pending mount queue */
	struct lookup_mod *lookup;		/* Lookup module */
	enum states state;
	int state_pipe[2];
	unsigned dir_created;		/* Was a directory created for this
					   mount? */
	unsigned random_multimount;	/* use random policy when selecting a
					 * host from which to mount */
	unsigned use_old_ldap_lookup;   /* query all schemas every time instead of sticking with the first one found */

	unsigned ignore_stupid_paths;   /* Ignores mount keys that will never occur where bryder works and which slow everything down or lead to 'interesting' results 
					 * See automount.c:is_path_stupid for details
					 */

	unsigned max_nfs_mount_retries; /* number of times to retry a failed nfs mount if it returns specified error messages (see mount_nfs.c for the errors */
	unsigned nfs_mount_retry_pause; /* Time in seconds to pause between retrying nfs mounts */
        
 
};

extern struct autofs_point ap; 

/* Standard function used by daemon or modules */

int aquire_lock(void);
void release_lock(void);
int spawnl(int logpri, const char *prog, ...);
#ifdef ENABLE_MOUNT_LOCKING
int spawnll(int logpri, const char *prog, ...);
#else
#define spawnll	spawnl
#endif
int spawnv(int ogpri, const char *prog, const char *const *argv);
void reset_signals(void);
void ignore_signals(void);
void discard_pending(int sig);
int signal_children(int sig);
int do_mount(const char *root, const char *name, int name_len,
	     const char *what, const char *fstype, const char *options);
int mkdir_path(const char *path, mode_t mode);
int rmdir_path(const char *path);

/* Prototype for module functions */

/* lookup module */

#define AUTOFS_LOOKUP_VERSION 4

#define KEY_MAX_LEN    NAME_MAX
#define MAPENT_MAX_LEN 4095

#ifdef MODULE_LOOKUP
int lookup_init(const char *mapfmt, int argc, const char *const *argv, void **context);
int lookup_ghost(const char *, int, time_t, void *);
int lookup_mount(const char *, const char *, int, void *);
int lookup_done(void *);
#endif
typedef int (*lookup_init_t) (const char *, int, const char *const *, void **);
typedef int (*lookup_ghost_t) (const char *, int, time_t, void *);
typedef int (*lookup_mount_t) (const char *, const char *, int, void *);
typedef int (*lookup_done_t) (void *);

struct lookup_mod {
	lookup_init_t lookup_init;
	lookup_ghost_t lookup_ghost;
	lookup_mount_t lookup_mount;
	lookup_done_t lookup_done;
	void *dlhandle;
	void *context;
};

struct lookup_mod *open_lookup(const char *name, const char *err_prefix,
			       const char *mapfmt, int argc, const char *const *argv);
int close_lookup(struct lookup_mod *);

/* parse module */

#define AUTOFS_PARSE_VERSION 3

#ifdef MODULE_PARSE
int parse_init(int argc, const char *const *argv, void **context);
int parse_mount(const char *root, const char *name,
		int name_len, const char *mapent, void *context);
int parse_done(void *);
#endif
typedef int (*parse_init_t) (int, const char *const *, void **);
typedef int (*parse_mount_t) (const char *, const char *, int, const char *, void *);
typedef int (*parse_done_t) (void *);

struct parse_mod {
	parse_init_t parse_init;
	parse_mount_t parse_mount;
	parse_done_t parse_done;
	void *dlhandle;
	void *context;
};

struct parse_mod *open_parse(const char *name, const char *err_prefix,
			     int argc, const char *const *argv);
int close_parse(struct parse_mod *);

/* mount module */

#define AUTOFS_MOUNT_VERSION 4

#ifdef MODULE_MOUNT
int mount_init(void **context);
int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *options, void *context);
int mount_done(void *context);
#endif
typedef int (*mount_init_t) (void **);
typedef int (*mount_mount_t) (const char *, const char *, int, const char *, const char *,
			      const char *, void *);
typedef int (*mount_done_t) (void *);

struct mount_mod {
	mount_init_t mount_init;
	mount_mount_t mount_mount;
	mount_done_t mount_done;
	void *dlhandle;
	void *context;
};

struct mount_mod *open_mount(const char *name, const char *err_prefix);
int close_mount(struct mount_mod *);

/* mapent cache definition */

#define CHE_FAIL	0x0000
#define CHE_OK		0x0001
#define CHE_UPDATED	0x0002
#define CHE_RMPATH	0x0004
#define CHE_MISSING	0x0008

struct mapent_cache {
	struct mapent_cache *next;
	char *key;
	char *mapent;
	time_t age;
};

void cache_init(void);
struct mapent_cache *cache_lookup(const char *key);
struct mapent_cache *cache_lookup_next(struct mapent_cache *me);
struct mapent_cache *cache_lookup_first(void);
struct mapent_cache *cache_partial_match(const char *prefix);
int cache_add(const char *root, const char *key, const char *mapent, time_t age);
int cache_update(const char *root, const char *key, const char *mapent, time_t age);
int cache_delete(const char *root, const char *key, int rmpath);
void cache_clean(const char *root, time_t age);
void cache_release(void);
int cache_ghost(const char *root, int is_ghosted,
		const char *map, const char *type, struct parse_mod *parse);

/* buffer management */

int _strlen(const char *str, size_t max);
int cat_path(char *buf, size_t len, const char *dir, const char *base);
int ncat_path(char *buf, size_t len,
              const char *dir, const char *base, size_t blen);

/* rpc helper subs */
#define RPC_PING_FAIL           0x0000
#define RPC_PING_V2             NFS2_VERSION
#define RPC_PING_V3             NFS3_VERSION
#define RPC_PING_UDP            0x0100
#define RPC_PING_TCP            0x0200

unsigned int rpc_ping(const char *host, long seconds, long micros);
int rpc_time(const char *host, 
	     unsigned int ping_vers, unsigned int ping_proto,
	     long seconds, long micros, double *result);

/* mount table utilities */
struct mnt_list {
	char *path;
	char *fs_name;
	char *fs_type;
	pid_t pid;
	time_t last_access;
	struct mnt_list *next;
};

struct mnt_list *get_mnt_list(const char *table, const char *path, int include);
struct mnt_list *reverse_mnt_list(struct mnt_list *list);
struct mnt_list *get_base_mnt_list(struct mnt_list *list);
void free_mnt_list(struct mnt_list *list);
int contained_in_local_fs(const char *path);
int is_mounted(const char *table, const char *path);
int has_fstab_option(const char *path, const char *opt);
int allow_owner_mount(const char *);

/* nsswitch parsing */
#define MAPTYPE_FILE 1
#define MAPTYPE_PROGRAM 2

char *get_nsswitch_map(const char *);
int isfilemap(const char *);
int isypmap(const char *);

/* log notification */
extern int do_verbose;
extern int do_debug;

/* command line option to print out included map contents */
extern int dumpmap;

#define info(msg, args...) 		\
if (do_verbose || do_debug) 		\
	syslog(LOG_INFO, msg, ##args);

#define warn(msg, args...) 			\
if (do_verbose || do_debug) 		\
	syslog(LOG_WARNING, msg, ##args);

#define error(msg, args...)	syslog(LOG_ERR, msg, ##args);

#define crit(msg, args...)	syslog(LOG_CRIT, msg, ##args);

#define debug(msg, args...) 		\
if (do_debug) 				\
	syslog(LOG_DEBUG, msg, ##args);

/* Define reentrant logging macros for signal handlers */

#define debug_r(context, msg, args...)				\
do {								\
	if (do_debug)						\
		syslog_r(LOG_DEBUG, context, msg, ##args);	\
} while (0)

#define warn_r(context, msg, args...)				\
do {								\
	if (do_verbose || do_debug)				\
		syslog_r(LOG_WARNING, context, msg, ##args);	\
} while (0)

#define error_r(context, msg, args...)			\
do {							\
	syslog_r(LOG_ERR, context, msg, ##args);	\
} while (0)

#define crit_r(context, msg, args...)			\
do {							\
	syslog_r(LOG_CRIT, context, msg, ##args);	\
} while (0)

#endif

