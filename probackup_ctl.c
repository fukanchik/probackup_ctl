#include "probackup_ctl.h"

#include "postgres.h"

#include "c.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"
#include "utils/jsonb.h"
#include "utils/palloc.h"

// WTF?!
#include "backup.c"
#include "catalog.c"
#include "exec.c"
#include "probackup.c"
#include "show.c"
#include "storage.c"
#include "utils.c"
#include "guc_vars.c"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(probackup_register_catalog);
PG_FUNCTION_INFO_V1(probackup_show);
PG_FUNCTION_INFO_V1(probackup_backup);
PG_FUNCTION_INFO_V1(probackup_delete);
PG_FUNCTION_INFO_V1(probackup_log);

void _PG_init(void);

void
_PG_init(void)
{
	init_guc_variables();
}

Datum
probackup_register_catalog(PG_FUNCTION_ARGS)
{
	char *backup_path  = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char *storage      = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *storage_name = text_to_cstring(PG_GETARG_TEXT_PP(2));
	text *ret;

	BackupPath bp = {.id           = 0,
	                 .backup_path  = backup_path,
	                 .storage      = storage,
	                 .storage_name = storage_name};

	ProbackupCatalog *cat = probackup_exec_show(&bp, NULL);

	if (cat == NULL)
	{
		ereport(ERROR, errmsg("No catalog at %s", backup_path));
		return 0;
	}

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, errmsg("Can't SPI_connect()"));
	}

	{
		Oid         argtypes[3] = {TEXTOID, TEXTOID, TEXTOID};
		Datum       Values[3]   = {CStringGetTextDatum(backup_path),
		                           CStringGetTextDatum(storage),
		                           CStringGetTextDatum(storage_name)};
		const char *sql =
		        "INSERT INTO probackup.catalogs (backup_path, storage, "
		        "storage_name) VALUES ($1, $2, $3)";
		int ret =
		        SPI_execute_with_args(sql, 3, argtypes, Values, NULL, false, 0);
		if (ret < 0)
		{
			ereport(ERROR, errmsg("SPI error number: %d", ret));
		}
		ereport(INFO, errmsg("SPI res=%d", ret));
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, errmsg("Can't SPI_finish()"));
	}

	ret = cstring_to_text(backup_path);
	pfree(backup_path);
	PG_RETURN_TEXT_P(ret);
}

Datum
probackup_show(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc     tupdesc;

		int64 catalog_id  = PG_GETARG_INT64(0);
		char *pg_instance = text_to_cstring(PG_GETARG_TEXT_PP(1));

		funcctx    = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		tupdesc    = CreateTemplateTupleDesc(15);
		TupleDescInitEntry(tupdesc, (AttrNumber)1, "catalog_id", INT8OID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)2, "pg_instance", TEXTOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)3, "backup_id", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)4, "end_time", TIMESTAMPTZOID,
		                   -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)5, "backup_mode", TEXTOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)6, "wal_mode", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)7, "current_tli", INT8OID, -1,
		                   0);

		TupleDescInitEntry(tupdesc, (AttrNumber)8, "duration", INTERVALOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)9, "data_size", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)10, "wal_size", INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)11, "compress_alg", TEXTOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)12, "compress_ratio", INT8OID,
		                   -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber)13, "start_lsn", PG_LSNOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)14, "stop_lsn", PG_LSNOID, -1,
		                   0);
		TupleDescInitEntry(tupdesc, (AttrNumber)15, "status", TEXTOID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		flatten_backups(funcctx, catalog_id, pg_instance);

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();

	if (funcctx->call_cntr < funcctx->max_calls)
	{
		List     *tuples = funcctx->user_fctx;
		HeapTuple tuple  = tuples->elements[funcctx->call_cntr].ptr_value;
		Datum     result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	} else
	{
		SRF_RETURN_DONE(funcctx);
	}
}

Datum
probackup_backup(PG_FUNCTION_ARGS)
{
	int64 catalog_id       = PG_GETARG_INT64(0);
	char *pg_instance      = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *backup_mode      = text_to_cstring(PG_GETARG_TEXT_PP(2));
	char *wal_mode         = text_to_cstring(PG_GETARG_TEXT_PP(3));
	char *backup_id        = text_to_cstring(PG_GETARG_TEXT_PP(4));
	char *parent_backup_id = text_to_cstring(PG_GETARG_TEXT_PP(5));

	BackupPath bp = select_catalog(catalog_id);

	char *ret = probackup_exec_backup(&bp, pg_instance, backup_mode, wal_mode,
	                                  backup_id, parent_backup_id);
	text *txt = cstring_to_text(ret);
	PG_RETURN_TEXT_P(txt);
}

Datum
probackup_delete(PG_FUNCTION_ARGS)
{
	int64 catalog_id  = PG_GETARG_INT64(0);
	char *pg_instance = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *backup_id   = text_to_cstring(PG_GETARG_TEXT_PP(2));

	BackupPath bp     = select_catalog(catalog_id);
	List      *params = NIL;

	if (pg_instance && strlen(pg_instance) > 0)
	{
		params = lappend(params, "--instance");
		params = lappend(params, pg_instance);
	} else
	{
		ereport(ERROR, errmsg("Backup instance is mandatory"));
	}

	if (backup_id && strlen(backup_id) > 0)
	{
		params = lappend(params, "--backup-id");
		params = lappend(params, backup_id);
	} else
	{
		ereport(ERROR, errmsg("Backup id is mandatory"));
	}

	{
		text *txt;
		char *ret = exec_probackup(&bp, "delete", params);
		txt       = cstring_to_text(ret);
		PG_RETURN_TEXT_P(txt);
	}
}

Datum
probackup_log(PG_FUNCTION_ARGS)
{
	int64 catalog_id  = PG_GETARG_INT64(0);
	char *pg_instance = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char *backup_id   = text_to_cstring(PG_GETARG_TEXT_PP(2));

	BackupPath bp     = select_catalog(catalog_id);
	char *log_path;
	FILE *fi;
	StringInfoData out;
	char buf[BUFSZ];
	text          *txt;

	if (strcasecmp(bp.storage, "fs"))
	{
		ereport(ERROR, errmsg("Log is only available for local backups"));
	}
	log_path = psprintf("%s/backups/%s/%s.log", bp.backup_path, pg_instance, backup_id);

	fi = fopen(log_path, "r");
	if (!fi)
	{
		ereport(ERROR, errmsg("Can't read %s", log_path));
	}

	initStringInfo(&out);

	while(!feof(fi))
	{
		fgets(buf, BUFSZ, fi);
		appendStringInfo(&out, "%s", buf);
	}

	fclose(fi);

	ereport(INFO, errmsg("Probackup log %s/%s:\n%s", pg_instance, backup_id, out.data));
	txt       = cstring_to_text("");
	PG_RETURN_TEXT_P(txt);
}
