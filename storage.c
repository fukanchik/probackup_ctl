/* Support for probackup storages configuration. FS/SFTP/S3. */
#include "executor/spi.h"
#include "probackup_ctl.h"
#include "utils/builtins.h"

typedef struct
{
	char *sftp_host;
	char *sftp_port;
	char *sftp_user;
} SftpParameters;

typedef struct
{
	char *s3_hostname;
	char *s3_port;
	char *s3_access_key;
	char *s3_secret_key;
	char *s3_region;
	char *s3_bucket;
	bool  s3_https;
} S3Parameters;

static void
check_num_storages_valid(const char *kind, const char *storage_name, uint64 numvals)
{
	if (numvals == 0)
	{
		ereport(ERROR, errmsg("Couldn't find %s storage '%s'", kind, storage_name));
	}

	if (numvals != 1)
	{
		ereport(ERROR, errmsg("Multiple %s storages with the same name: %s",
							  kind,
							  storage_name));
	}
}

static SftpParameters
select_sftp(const char *storage_name)
{
	SftpParameters ret;
	Oid            argtypes[1] = {TEXTOID};
	Datum          Values[1]   = {CStringGetTextDatum(storage_name)};
	const char    *sql =
	        "SELECT sftp_hostname, sftp_port, sftp_user from "
	        "probackup.sftp_config where name=$1";
	MemoryContext old = CurrentMemoryContext;
	int           rc;

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, errmsg("Can't SPI_connect()"));
	}

	rc = SPI_execute_with_args(sql, 1, argtypes, Values, NULL, false, 0);
	if (rc < 0)
	{
		ereport(ERROR, errmsg("SPI error number: %d", rc));
	}

	if (rc == SPI_OK_SELECT && SPI_tuptable != NULL)
	{
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		uint64    numvals = SPI_processed;

		check_num_storages_valid("SFTP", storage_name, numvals);

		for (uint64 i = 0; i < numvals; i++)
		{
			HeapTuple tuple        = SPI_tuptable->vals[i];
			char     *hostname_str = SPI_getvalue(tuple, tupdesc, 1);
			char     *port_str     = SPI_getvalue(tuple, tupdesc, 2);
			char     *user_str     = SPI_getvalue(tuple, tupdesc, 3);

			MemoryContext spi_ctx = CurrentMemoryContext;
			MemoryContextSwitchTo(old);

			ret.sftp_host = pstrdup(hostname_str);
			ret.sftp_port = pstrdup(port_str);
			ret.sftp_user = pstrdup(user_str);

			MemoryContextSwitchTo(spi_ctx);
		}
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, errmsg("Can't SPI_finish()"));
	}

	return ret;
}

static S3Parameters
select_s3(const char *storage_name)
{
	S3Parameters ret;
	Oid          argtypes[1] = {TEXTOID};
	Datum        Values[1]   = {CStringGetTextDatum(storage_name)};
	const char  *sql =
	        "SELECT s3_hostname, s3_port, s3_access_key, s3_secret_key, "
	        "s3_region, s3_bucket, s3_https from "
	        "probackup.s3_config where name=$1";
	MemoryContext old = CurrentMemoryContext;
	int           rc;

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, errmsg("Can't SPI_connect()"));
	}

	rc = SPI_execute_with_args(sql, 1, argtypes, Values, NULL, false, 0);
	if (rc < 0)
	{
		ereport(ERROR, errmsg("SPI error number: %d", rc));
	}

	if (rc == SPI_OK_SELECT && SPI_tuptable != NULL)
	{
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		uint64    numvals = SPI_processed;

		check_num_storages_valid("S3", storage_name, numvals);

		for (uint64 i = 0; i < numvals; i++)
		{
			HeapTuple tuple          = SPI_tuptable->vals[i];
			char     *hostname_str   = SPI_getvalue(tuple, tupdesc, 1);
			char     *port_str       = SPI_getvalue(tuple, tupdesc, 2);
			char     *access_key_str = SPI_getvalue(tuple, tupdesc, 3);
			char     *secret_key_str = SPI_getvalue(tuple, tupdesc, 4);
			char     *region_str     = SPI_getvalue(tuple, tupdesc, 5);
			char     *bucket_str     = SPI_getvalue(tuple, tupdesc, 6);
			char     *https_str      = SPI_getvalue(tuple, tupdesc, 7);

			MemoryContext spi_ctx = CurrentMemoryContext;
			MemoryContextSwitchTo(old);

			ret.s3_hostname   = pstrdup(hostname_str);
			ret.s3_port       = pstrdup(port_str);
			ret.s3_access_key = pstrdup(access_key_str);
			ret.s3_secret_key = pstrdup(secret_key_str);
			ret.s3_region     = pstrdup(region_str);
			ret.s3_bucket     = pstrdup(bucket_str);
			ret.s3_https      = !strcasecmp(https_str, "ON");

			MemoryContextSwitchTo(spi_ctx);
		}
	}

	if (SPI_finish() != SPI_OK_FINISH)
	{
		ereport(ERROR, errmsg("Can't SPI_finish()"));
	}

	return ret;
}

List *
storage_params_list(const BackupPath *bp)
{
	List *ret = NIL;

	ret = lappend(ret, "--backup-path");
	ret = lappend(ret, bp->backup_path);
	if (!strcasecmp(bp->storage, "FS")) return ret;

	if (!strcasecmp(bp->storage, "SFTP"))
	{
		SftpParameters p = select_sftp(bp->storage_name);

		if (p.sftp_host && strlen(p.sftp_host) > 0)
		{
			ret = lappend(ret, "--remote-host");
			ret = lappend(ret, p.sftp_host);
		}

		if (p.sftp_port && strlen(p.sftp_port) > 0)
		{
			ret = lappend(ret, "--remote-port");
			ret = lappend(ret, p.sftp_port);
		}

		if (p.sftp_user && strlen(p.sftp_user) > 0)
		{
			ret = lappend(ret, "--remote-user");
			ret = lappend(ret, p.sftp_user);
		}

		return ret;
	}

	if (!strcasecmp(bp->storage, "S3"))
	{
		S3Parameters p = select_s3(bp->storage_name);

		ret = lappend(ret, "--s3");

		if (p.s3_hostname)
		{
			ret = lappend(ret, "--s3-host");
			ret = lappend(ret, p.s3_hostname);
		}

		if (p.s3_port)
		{
			ret = lappend(ret, "--s3-port");
			ret = lappend(ret, p.s3_port);
		}

		if (p.s3_access_key)
		{
			ret = lappend(ret, "--access-key");
			ret = lappend(ret, p.s3_access_key);
		}

		if (p.s3_secret_key)
		{
			ret = lappend(ret, "--secret-key");
			ret = lappend(ret, p.s3_secret_key);
		}

		if (p.s3_region)
		{
			ret = lappend(ret, "--s3-region");
			ret = lappend(ret, p.s3_region);
		}

		if (p.s3_bucket)
		{
			ret = lappend(ret, "--s3-bucket");
			ret = lappend(ret, p.s3_bucket);
		}

		if (p.s3_https)
		{
			ret = lappend(ret, "--s3-secure");
			ret = lappend(ret, "on");
		}

		return ret;
	}

	ereport(ERROR, errmsg("Unexpected storage type %s", bp->storage));

	return NULL;
}

char *
storage_params(const BackupPath *bp)
{
	StringInfoData resp;

	initStringInfo(&resp);
	appendStringInfo(&resp, " --backup-path %s", bp->backup_path);

	if (!strcasecmp(bp->storage, "FS")) return resp.data;

	if (!strcasecmp(bp->storage, "SFTP"))
	{
		SftpParameters p = select_sftp(bp->storage_name);

		if (p.sftp_host && strlen(p.sftp_host) > 0)
		{
			appendStringInfo(&resp, " --remote-host=%s", p.sftp_host);
		}

		if (p.sftp_port && strlen(p.sftp_port) > 0)
		{
			appendStringInfo(&resp, " --remote-port=%s", p.sftp_port);
		}

		if (p.sftp_user && strlen(p.sftp_user) > 0)
		{
			appendStringInfo(&resp, " --remote-user=%s", p.sftp_user);
		}

		return resp.data;
	}

	if (!strcasecmp(bp->storage, "S3"))
	{
		S3Parameters p = select_s3(bp->storage_name);

		appendStringInfo(&resp, " --s3");

		if (p.s3_hostname)
		{
			appendStringInfo(&resp, " --s3-host=%s", p.s3_hostname);
		}

		if (p.s3_port)
		{
			appendStringInfo(&resp, " --s3-port=%s", p.s3_port);
		}

		if (p.s3_access_key)
		{
			appendStringInfo(&resp, " --access-key=%s", p.s3_access_key);
		}

		if (p.s3_secret_key)
		{
			appendStringInfo(&resp, " --secret-key=%s", p.s3_secret_key);
		}

		if (p.s3_region)
		{
			appendStringInfo(&resp, " --s3-region=%s", p.s3_region);
		}

		if (p.s3_bucket)
		{
			appendStringInfo(&resp, " --s3-bucket=%s", p.s3_bucket);
		}

		if (p.s3_https)
		{
			appendStringInfo(&resp, " --s3-secure=on");
		}

		return resp.data;
	}

	ereport(ERROR, errmsg("Unexpected storage type %s", bp->storage));

	return NULL;
}
