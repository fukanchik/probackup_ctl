#include "executor/spi.h"
#include "probackup_ctl.h"

List *
select_catalogs(int64 catalog_id)
{
	const char   *sql;
	int           ret;
	List         *result      = NIL;
	MemoryContext old         = CurrentMemoryContext;
	Oid           argtypes[1] = {INT8OID};
	Datum         Values[1]   = {Int64GetDatum(catalog_id)};

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, errmsg("Can't SPI_connect()"));
	}

	if (catalog_id != 0)
	{
		sql = "SELECT id, backup_path, storage, storage_name, probackup_bin "
		      "from "
		      "probackup.catalogs where id=$1";
		ret = SPI_execute_with_args(sql, 1, argtypes, Values, NULL, true, 0);
	} else
	{
		sql = "SELECT id, backup_path, storage, storage_name, probackup_bin "
		      "from "
		      "probackup.catalogs";
		ret = SPI_execute(sql, true, 0);
	}
	if (ret < 0)
	{
		ereport(ERROR, errmsg("SPI error number: %d", ret));
	}

	if (ret == SPI_OK_SELECT && SPI_tuptable != NULL)
	{
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		uint64    numvals = SPI_processed;

		for (uint64 i = 0; i < numvals; i++)
		{
			HeapTuple   tuple            = SPI_tuptable->vals[i];
			char       *id_str           = SPI_getvalue(tuple, tupdesc, 1);
			char       *backup_path_str  = SPI_getvalue(tuple, tupdesc, 2);
			char       *storage_str      = SPI_getvalue(tuple, tupdesc, 3);
			char       *storage_name_str = SPI_getvalue(tuple, tupdesc, 4);
			char       *probackup_bin    = SPI_getvalue(tuple, tupdesc, 5);
			BackupPath *bp;

			MemoryContext spi_ctx = CurrentMemoryContext;
			MemoryContextSwitchTo(old);

			bp                = palloc0(sizeof(BackupPath));
			bp->id            = atoi(id_str);
			bp->backup_path   = pstrdup(backup_path_str);
			bp->storage       = pstrdup(storage_str);
			bp->storage_name  = pstrdup(storage_name_str);
			bp->probackup_bin = probackup_bin?pstrdup(probackup_bin):NULL;

			result = lappend(result, bp);
			MemoryContextSwitchTo(spi_ctx);
		}
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, errmsg("Can't SPI_finish()"));
	}

	return result;
}

BackupPath
select_catalog(int catalog_id)
{
	const char   *sql;
	MemoryContext old         = CurrentMemoryContext;
	Oid           argtypes[1] = {INT8OID};
	Datum         Values[1]   = {Int64GetDatum(catalog_id)};
	BackupPath    ret;
	int           rc;

	memset(&ret, 0, sizeof(BackupPath));

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, errmsg("Can't SPI_connect()"));
	}

	sql = "SELECT backup_path, storage, storage_name from probackup.catalogs "
	      "where id=$1";
	rc  = SPI_execute_with_args(sql, 1, argtypes, Values, NULL, true, 0);
	if (rc < 0)
	{
		ereport(ERROR, errmsg("SPI error number: %d", rc));
	}

	if (rc == SPI_OK_SELECT && SPI_tuptable != NULL)
	{
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		uint64    numvals = SPI_processed;

		if (numvals == 0)
		{
			ereport(ERROR, errmsg("No catalog with id %d", catalog_id));
		}

		if (numvals != 1)
		{
			ereport(ERROR, errmsg("Need one value: %ld", numvals));
		}

		for (uint64 i = 0; i < numvals; i++)
		{
			HeapTuple tuple             = SPI_tuptable->vals[i];
			char     *backup_path_str   = SPI_getvalue(tuple, tupdesc, 1);
			char     *storage_str       = SPI_getvalue(tuple, tupdesc, 2);
			char     *storage_name_str  = SPI_getvalue(tuple, tupdesc, 3);
			char     *probackup_bin_str = SPI_getvalue(tuple, tupdesc, 4);

			MemoryContext spi_ctx = CurrentMemoryContext;
			MemoryContextSwitchTo(old);

			ret.id           = catalog_id;
			ret.backup_path  = pstrdup(backup_path_str);
			ret.storage      = pstrdup(storage_str);
			ret.storage_name = pstrdup(storage_name_str);
			ret.probackup_bin =
			        probackup_bin_str ? pstrdup(probackup_bin_str) : NULL;

			MemoryContextSwitchTo(spi_ctx);
		}
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, errmsg("Can't SPI_finish()"));
	}

	return ret;
}
