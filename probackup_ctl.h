#ifndef __PROBACKUP_CTL_H__
#define __PROBACKUP_CTL_H__

#include "postgres.h"

#include "funcapi.h"
#include "nodes/pg_list.h"
#include "utils/jsonb.h"

#define BUFSZ 8192

#define DEFAULT_LOG_PROBACKUP_COMMANDS false
#define DEFAULT_PROBACKUP_PATH "pg_probackup"
#define DEFAULT_STORAGE_KIND "FS"

extern char *global_probackup_path;
extern int   probackup_flavour;

/* Single backup info */
typedef struct tag_ProbackupBackup
{
	char       *id;
	char       *wal;
	char       *status;
	int64       min_xid;
	TimestampTz end_time;
	char       *hostname;
	XLogRecPtr  stop_lsn;
	XLogRecPtr  start_lsn;
	int64       wal_bytes;
	int64       block_size;
	int64       data_bytes;
	TimestampTz start_time;
	char       *backup_mode;
	int64       current_tli;
	char       *compress_alg;
	char       *backup_source;
	int64       min_multixact;
	int64       compress_level;
	char       *server_version;
	char       *program_version;
	int64       xlog_block_size;
	int64       wal_segment_size;
	int64       uncompressed_bytes;
	int64       wal_bytes_compress;
} ProbackupBackup;

/* Instance */
typedef struct tag_ProbackupInstance
{
	char *version;
	char *instance;
	List *backups;
} ProbackupInstance;

/* Catalog is a list of instances */
typedef struct tag_ProbackupCatalog
{
	char *path;
	List *instances;
} ProbackupCatalog;

/* Path + storage */
typedef struct
{
	int   id;
	char *backup_path;
	char *storage;
	char *storage_name;
	char *probackup_bin;
} BackupPath;

/* Duplicate NZE string */
char *mydup(char *p, int len);

/* Get key from JsonbContainer as a JsonbValue*/
JsonbValue *get_json_key(const char *key, JsonbContainer *container);
/* Get key from JsonbContainer as a string */
char *get_string_value(const char *key, JsonbContainer *container);
/* Get key from JsonbContainer as an int */
int64 get_int_value(const char *key, JsonbContainer *container);

TimestampTz get_datetime_value(const char *key, JsonbContainer *container);
XLogRecPtr  get_lsn_value(const char *key, JsonbContainer *container);

/* Format storage params for probackup */
char *storage_params(const BackupPath *bp);
List *storage_params_list(const BackupPath *bp);

/* List known catalogs */
List      *select_catalogs(int64 catalog_id);
BackupPath select_catalog(int catalog_id);

/* Build ProbackupBackup from json dictionary */
ProbackupBackup *get_backup_value3(JsonbContainer *container);
ProbackupBackup *get_backup_value2(JsonbContainer *container);
ProbackupBackup *(*get_backup_value)(JsonbContainer *container);
List *get_backup_list_value(const char *key, JsonbContainer *outer);

ProbackupInstance *parse_show_instance(JsonbValue val);
ProbackupCatalog  *parse_show_output(Jsonb *jb, const char *backup_path);
ProbackupCatalog  *probackup_exec_show(const BackupPath *bp, char *pg_instance);

char  *exec_probackup(const BackupPath *bp, const char *command, List *params);
Jsonb *exec_probackup_json(const BackupPath *bp, const char *command,
                           List *params);

/* Run prog with arguments and capture stdout into out and stderr into err */
int run(List *args_list, StringInfoData *out, StringInfoData *err);

/* GUC */
/* Enables logging of executed probackup commands */
extern bool probackup_log_commands;

void  flatten_backups(FuncCallContext *funcctx, int64 catalog_id,
                      char *pg_instance);
char *probackup_exec_backup(const BackupPath *bp, char *pg_instance,
                            char *backup_mode, char *wal_mode, char *backup_id,
                            char *parent_backup_id);

char *jbvTypeName(enum jbvType type);
void  init_guc_variables(void);
char *run_probackup(const char *probackup_path, const char *command,
                    List *all_params);

#endif // __PROBACKUP_CTL_H__
