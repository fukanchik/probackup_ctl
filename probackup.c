#include "probackup_ctl.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"

char *
run_probackup(const char *probackup_path, const char *command, List *params)
{
	List *pbk =
	        lappend(lappend(NIL, pstrdup(probackup_path)), pstrdup(command));
	List          *all_params = list_concat(pbk, params);
	StringInfoData out, err;
	int            rc;

	initStringInfo(&out);
	initStringInfo(&err);

	rc = run(all_params, &out, &err);

	if (strlen(err.data) > 0)
	{
		ereport(INFO,
		        errmsg("Probackup result code=%d, output:\n%s", rc, err.data));
	}
	return out.data;
}

char *
exec_probackup(const BackupPath *bp, const char *command, List *params)
{
	List *s_params   = storage_params_list(bp);
	List *all_params = list_concat(s_params, params);
	const char *probackup_bin = global_probackup_path;

	if (bp->probackup_bin)
	{
		probackup_bin = bp->probackup_bin;
	}
	return run_probackup(probackup_bin, command, all_params);
}

Jsonb *
exec_probackup_json(const BackupPath *bp, const char *command, List *params)
{
	char  *out;
	Jsonb *jb;
	Datum  jsonb_datum;

	params = lappend(params, "--format");
	params = lappend(params, "json");
	out    = exec_probackup(bp, command, params);

	jsonb_datum = DirectFunctionCall1(jsonb_in, CStringGetDatum(out));
	jb          = DatumGetJsonbP(jsonb_datum);

	return jb;
}

int
get_probackup_version(const char *probackup_path)
{
	char *version;
	const char *path_to_use = probackup_path;

	if (path_to_use == NULL)
	{
		path_to_use = global_probackup_path;
	}

	version= run_probackup(path_to_use, "version", NIL);

	if (strstr(version, "pg_probackup3") != NULL)
	{
		return 3;
	} else if (strstr(version, "pg_probackup") != NULL)
	{
		return 2;
	} else
	{
		return -1;
	}
}
