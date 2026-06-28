#include "probackup_ctl.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"

ProbackupBackup *
get_backup_value3(JsonbContainer *container)
{
	ProbackupBackup *ret = palloc0(sizeof(ProbackupBackup));

	ret->id                 = get_string_value("id", container);
	ret->wal                = get_string_value("wal", container);
	ret->status             = get_string_value("status", container);
	ret->min_xid            = get_int_value("min-xid", container);
	ret->end_time           = get_datetime_value("end-time", container);
	ret->hostname           = get_string_value("hostname", container);
	ret->stop_lsn           = get_lsn_value("stop-lsn", container);
	ret->start_lsn          = get_lsn_value("start-lsn", container);
	ret->wal_bytes          = get_int_value("wal-bytes", container);
	ret->block_size         = get_int_value("block-size", container);
	ret->data_bytes         = get_int_value("data-bytes", container);
	ret->start_time         = get_datetime_value("start-time", container);
	ret->backup_mode        = get_string_value("backup-mode", container);
	ret->current_tli        = get_int_value("current-tli", container);
	ret->compress_alg       = get_string_value("compress-alg", container);
	ret->backup_source      = get_string_value("backup-source", container);
	ret->min_multixact      = get_int_value("min-multixact", container);
	ret->compress_level     = get_int_value("compress-level", container);
	// Server version is int in V3 and is string in V2
	ret->server_version     = psprintf("%ld", get_int_value("server-version", container));
	ret->program_version    = get_string_value("program-version", container);
	ret->xlog_block_size    = get_int_value("xlog-block-size", container);
	ret->wal_segment_size   = get_int_value("wal-segment-size", container);
	ret->uncompressed_bytes = get_int_value("uncompressed-bytes", container);
	ret->wal_bytes_compress = get_int_value("wal-bytes-compress", container);

	return ret;
}

ProbackupBackup *
get_backup_value2(JsonbContainer *container)
{
	ProbackupBackup *ret = palloc0(sizeof(ProbackupBackup));

	ret->id                 = get_string_value("id", container);
	ret->wal                = get_string_value("wal", container);
	ret->status             = get_string_value("status", container);
	ret->min_xid            = get_int_value("min-xid", container);
	ret->end_time           = get_datetime_value("end-time", container);
	ret->hostname           = get_string_value("hostname", container);
	ret->stop_lsn           = get_lsn_value("stop-lsn", container);
	ret->start_lsn          = get_lsn_value("start-lsn", container);
	ret->wal_bytes          = get_int_value("wal-bytes", container);
	ret->block_size         = get_int_value("block-size", container);
	ret->data_bytes         = get_int_value("data-bytes", container);
	ret->start_time         = get_datetime_value("start-time", container);
	ret->backup_mode        = get_string_value("backup-mode", container);
	ret->current_tli        = get_int_value("current-tli", container);
	ret->compress_alg       = get_string_value("compress-alg", container);
	ret->backup_source      = get_string_value("backup-source", container);
	ret->min_multixact      = get_int_value("min-multixact", container);
	ret->compress_level     = get_int_value("compress-level", container);
	ret->server_version     = get_string_value("server-version", container);
	ret->program_version    = get_string_value("program-version", container);
	ret->xlog_block_size    = get_int_value("xlog-block-size", container);
	ret->wal_segment_size   = get_int_value("wal-segment-size", container);
	ret->uncompressed_bytes = get_int_value("uncompressed-bytes", container);
	ret->wal_bytes_compress = get_int_value("wal-bytes-compress", container);

	return ret;
}

ProbackupBackup *(*get_backup_value)(JsonbContainer *container) = get_backup_value2;

List *
get_backup_list_value(const char *key, JsonbContainer *outer)
{
	JsonbContainer *inner;
	JsonbValue     *v;
	List           *ret = NIL;

	v = get_json_key(key, outer);

	if (v->type != jbvBinary)
	{
		ereport(ERROR, errmsg("Wrong type: %x", v->type));
	}

	inner = v->val.binary.data;

	if ((inner->header & JB_FARRAY) == 0)
	{
		ereport(ERROR, errmsg("Backup list is not an array: %x", inner->header));
	}

	if(0){	StringInfoData jtext;
	initStringInfo(&jtext);
	(void)JsonbToCString(&jtext, inner, 0);
	ereport(INFO, errmsg("%s", jtext.data));
	}


	{
		JsonbIterator     *it;
		JsonbIteratorToken r;
		JsonbValue         v;

		it = JsonbIteratorInit(inner);
		r  = JsonbIteratorNext(&it, &v, true);
		if (r != WJB_BEGIN_ARRAY)
		{
			ereport(ERROR, errmsg("JSON array expected but got %x", r));
		}

		while ((r = JsonbIteratorNext(&it, &v, true)) != WJB_END_ARRAY)
		{
			if (v.type == jbvBinary)
			{
				ProbackupBackup *backup = get_backup_value(v.val.binary.data);
				ret                     = lappend(ret, backup);
			} else
			{
				ereport(INFO, errmsg("WRONG TYPE: %x", v.type));
			}
		}
	}

	return ret;
}

ProbackupInstance *
parse_show_instance(JsonbValue val)
{
	JsonbContainer    *container;
	ProbackupInstance *ret;

	if (val.type != jbvBinary) return NULL;

	container = val.val.binary.data;

	if ((container->header & JB_FOBJECT) == 0) return NULL;

	{
		StringInfoData jtext;
		initStringInfo(&jtext);
		(void)JsonbToCString(&jtext, container, val.val.binary.len);
	}

	ret = palloc0(sizeof(ProbackupInstance));

	ret->version  = get_string_value("version", container);
	ret->instance = get_string_value("instance", container);
	ret->backups  = get_backup_list_value("backups", container);

	return ret;
}

ProbackupCatalog *
parse_show_output(Jsonb *jb, const char *backup_path)
{
	ProbackupCatalog  *ret;
	JsonbIterator     *it;
	JsonbIteratorToken token;
	JsonbValue         val;

	ret       = palloc0(sizeof(ProbackupCatalog));
	ret->path = pstrdup(backup_path);
	it        = JsonbIteratorInit(&jb->root);

	while ((token = JsonbIteratorNext(&it, &val, true)) != WJB_END_ARRAY)
	{
		if (token == WJB_BEGIN_ARRAY)
		{
		}
		else if (token == WJB_ELEM)
		{
			ret->instances = lappend(ret->instances, parse_show_instance(val));
		} else
		{
			ereport(INFO, errmsg("Wrong obj token %x", token));
		}
	}

	return ret;
}

ProbackupCatalog *
probackup_exec_show(const BackupPath *bp, char *pg_instance)
{
	List *params = NIL;
	Jsonb *jb;

	if (pg_instance && strlen(pg_instance) > 0)
	{
		params = lappend(params, "--instance");
		params = lappend(params, pg_instance);
	}

	jb = exec_probackup_json(bp, "show", params);

 	return parse_show_output(jb, bp->backup_path);
}
