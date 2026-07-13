#include "probackup_ctl.h"
#include "utils/guc.h"

char *global_probackup_path = NULL;
int   probackup_flavour = 0;

typedef struct
{
	int version;
	char path[MAXPGPATH];
} guc_ver;

/* Check new pg_probackup command and guess version. */
static bool
probackup_check_command(char **newval, void **extra, GucSource source)
{
	bool ok = false;
	int  probackup_version;

	if (*newval == NULL || *newval[0] == '\0')
	{
		/* Empty values are not allowed */
		return false;
	}

	PG_TRY();
	{
		char *version = run_probackup(*newval, "version", NIL);

		if (strstr(version, "pg_probackup3") != NULL)
		{
			probackup_version = 3;
			ok                = true;
		} else if (strstr(version, "pg_probackup") != NULL)
		{
			probackup_version = 2;
			ok                = true;
		} else
		{
			ok = false;
		}
	}
	PG_CATCH();
	{
		ok = false;
	}
	PG_END_TRY();

	if (ok)
	{
		guc_ver *ver = guc_malloc(ERROR, sizeof(guc_ver));
		if (!ver) return false;

		strncpy(ver->path, *newval, MAXPGPATH);
		ver->version = probackup_version;

		*extra = ver;
	}

	return ok;
}

/* Assign new pg_probackup path and version. */
static void
probackup_command_assign(const char *newval, void *extra)
{
	guc_ver *ver = extra;

	global_probackup_path    = ver->path;
	probackup_flavour = ver->version;

	if (probackup_flavour == 2)
	{
		get_backup_value = get_backup_value2;
		ereport(INFO, errmsg("Setting probackup flavour version 2"));
	} else
	{
		get_backup_value = get_backup_value3;
		ereport(INFO, errmsg("Setting probackup flavour version 3"));
	}
}

/* Register supported GUC variables */
void
init_guc_variables(void)
{
	DefineCustomBoolVariable("probackup_ctl.log_commands",
	                         "Enables logging of executed probackup commands.",
	                         NULL, &probackup_log_commands,
	                         DEFAULT_LOG_PROBACKUP_COMMANDS, PGC_USERSET, 0,
	                         NULL, NULL, NULL);

	DefineCustomStringVariable("probackup_ctl.command", "Probackup path to use",
	                           NULL, &global_probackup_path, DEFAULT_PROBACKUP_PATH,
	                           PGC_USERSET, 0, probackup_check_command,
	                           probackup_command_assign, NULL);

	MarkGUCPrefixReserved("probackup_ctl");
}
