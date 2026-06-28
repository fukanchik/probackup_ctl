#include "probackup_ctl.h"
#include "postgres.h"
#include "c.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/jsonb.h"
#include "utils/palloc.h"
#include "utils/timestamp.h"
#include "utils/pg_lsn.h"

static Datum
timestamptz_diff_interval(TimestampTz start_time, TimestampTz stop_time)
{
    int64_t diff_usecs;
    Interval *interval;

    if (TIMESTAMP_NOT_FINITE(start_time) || TIMESTAMP_NOT_FINITE(stop_time))
        ereport(ERROR, (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
                        errmsg("cannot compute difference between non-finite timestamps")));

    diff_usecs = stop_time - start_time;

    interval = (Interval *) palloc(sizeof(Interval));

    interval->time = diff_usecs;
    interval->day  = 0;
    interval->month = 0;

	return IntervalPGetDatum(interval);
}
static HeapTuple
build_backup_tuple(int ncat, ProbackupInstance *instance,
                   ProbackupBackup *backup, FuncCallContext *funcctx)
{
	Datum     values[15];
	bool      nulls[15];
	HeapTuple tuple;

	nulls[0]  = false;
	nulls[1]  = false;
	nulls[2]  = false;
	nulls[3]  = false;
	nulls[4]  = false;
	nulls[5]  = false;
	nulls[6]  = false;
	nulls[7]  = false;
	nulls[8]  = false;
	nulls[9]  = false;
	nulls[10] = false;
	nulls[11] = false;
	nulls[12] = false;
	nulls[13] = false;
	nulls[14] = false;

	values[0] = Int64GetDatum(ncat);
	values[1] = CStringGetTextDatum(instance->instance);
	values[2] = CStringGetTextDatum(backup->id);
	values[3] = TimestampTzGetDatum(backup->end_time);
	values[4] = CStringGetTextDatum(backup->backup_mode);
	values[5] = CStringGetTextDatum(backup->wal);
	values[6] = Int64GetDatum(backup->current_tli);

	values[7]  = timestamptz_diff_interval(backup->start_time, (backup->end_time==0)?GetCurrentTimestamp():backup->end_time);
	values[8]  = Int64GetDatum(backup->data_bytes);
	values[9]  = Int64GetDatum(backup->wal_bytes);
	values[10] = CStringGetTextDatum(backup->compress_alg);
	values[11] = Int64GetDatum(backup->compress_level);
	values[12] = LSNGetDatum(backup->start_lsn);
	values[13] = LSNGetDatum(backup->stop_lsn);
	values[14] = CStringGetTextDatum(backup->status);

	tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);

	return tuple;
}

void
flatten_backups(FuncCallContext *funcctx, int64 catalog_id, char *pg_instance)
{
	List           *output = NIL;
	const ListCell *cat_cell;
	List           *cats = select_catalogs(catalog_id);

	foreach (cat_cell, cats)
	{
		BackupPath       *bp        = lfirst(cat_cell);
		ProbackupCatalog *cat       = probackup_exec_show(bp, pg_instance);
		List             *instances = cat->instances;
		const ListCell   *inst_cell;

		foreach (inst_cell, instances)
		{
			ProbackupInstance *instance = lfirst(inst_cell);
			const ListCell    *backup_cell;
			foreach (backup_cell, instance->backups)
			{
				ProbackupBackup *backup = lfirst(backup_cell);
				HeapTuple        tuple =
				        build_backup_tuple(bp->id, instance, backup, funcctx);

				output = lappend(output, tuple);
			}
		}
	}

	if (output)
	{
		funcctx->user_fctx = output;
		funcctx->call_cntr = 0;
		funcctx->max_calls = output->length;
	} else
	{
		funcctx->user_fctx = NULL;
		funcctx->call_cntr = 0;
		funcctx->max_calls = 0;
	}
}

char *
probackup_exec_backup(const BackupPath *bp, char *pg_instance,
                      char *backup_mode, char *wal_mode, char *backup_id,
                      char *parent_backup_id)
{
	char *out;
	List *params = NIL;

	params = lappend(params, "--instance");
	params = lappend(params, pg_instance);

	if (probackup_flavour == 2)
	{
		if (!backup_mode || strlen(backup_mode) == 0)
		{
			backup_mode = "FULL";
		}
		if (!wal_mode || strlen(wal_mode) == 0)
		{
			wal_mode = "STREAM";
		}
	}

	if (backup_mode && strlen(backup_mode) > 0)
	{
		params = lappend(params, "--backup-mode");
		params = lappend(params, backup_mode);
	}

	if (wal_mode && strlen(wal_mode) > 0)
	{
		if (!strcasecmp(wal_mode, "stream"))
			params = lappend(params, "--stream");
	}

	out = exec_probackup(bp, "backup", params);

	return out;
}
