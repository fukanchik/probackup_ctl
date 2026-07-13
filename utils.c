#include "c.h"
#include "nodes/miscnodes.h"
#include "probackup_ctl.h"
#include "utils/builtins.h"
#include "utils/datetime.h"
#include "utils/jsonb.h"
#include "utils/pg_lsn.h"
#include "utils/timestamp.h"

char *
jbvTypeName(enum jbvType type)
{
	char  buf[BUFSZ];
	char *ret;

	switch (type)
	{
		case jbvNull:
			ret = "null";
			break;
		case jbvString:
			ret = "string";
			break;
		case jbvNumeric:
			ret = "numeric";
			break;
		case jbvBool:
			ret = "bool";
			break;
		case jbvArray:
			ret = "array";
			break;
		case jbvObject:
			ret = "object";
			break;
		case jbvBinary:
			ret = "binary";
			break;
		case jbvDatetime:
			ret = "datetime";
			break;
		default:
			snprintf(buf, BUFSZ, "<unknown: %X>", type);
			break;
	}
	return pstrdup(ret);
}

char *
mydup(char *p, int len)
{
	char *ret = palloc(len + 1);
	if (!ret) ereport(ERROR, errmsg("Can't alloc"));

	memcpy(ret, p, len);
	ret[len] = 0;
	return ret;
}

JsonbValue *
get_json_key(const char *key, JsonbContainer *container)
{
	JsonbValue  kval;
	JsonbValue *v;

	kval.type           = jbvString;
	kval.val.string.val = pstrdup(key);
	kval.val.string.len = strlen(key);

	v = findJsonbValueFromContainer(container, JB_FOBJECT, &kval);

	return v;
}

XLogRecPtr
get_lsn_value(const char *key, JsonbContainer *container)
{
	char      *str = get_string_value(key, container);
	XLogRecPtr ret;

#if PG_VERSION_NUM < 170000
	bool err = false;

	ret = pg_lsn_in_internal(str, &err);
	if (err)
#else
	ErrorSaveContext err = {T_ErrorSaveContext};
	ret                  = pg_lsn_in_safe(str, (Node *)&err);
	if (err.error_occurred)
#endif
	{
		ereport(ERROR, errmsg("Wrong LSN: %s", str));
	}
	return ret;
}

TimestampTz
get_datetime_value(const char *key, JsonbContainer *container)
{
	JsonbValue        *v;
	int                dterr;
	char              *str;
	char               workbuf[MAXDATELEN + MAXDATEFIELDS];
	char              *field[MAXDATEFIELDS];
	int                ftype[MAXDATEFIELDS];
	int                nf;
	int                dtype;
	struct pg_tm       tm;
	fsec_t             fsec;
	int                tz = 0;
	DateTimeErrorExtra extra;
	TimestampTz        result;

	v = get_json_key(key, container);
	if (!v)
	{
		return 0;
	}
	if (v->type != jbvString)
	{
		ereport(ERROR,
		        errmsg("Expected string value at key %s, but got type %x", key,
		               v->type));
	}
	str   = mydup(v->val.string.val, v->val.string.len);
	dterr = ParseDateTime(str, workbuf, sizeof(workbuf), field, ftype,
	                      MAXDATEFIELDS, &nf);
	if (dterr == 0)
	{
		dterr = DecodeDateTime(field, ftype, nf, &dtype, &tm, &fsec, &tz,
		                       &extra);
	}
	if (dterr != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_DATETIME_FORMAT),
		                errmsg("invalid input syntax for type timestamp with "
		                       "time zone: \"%s\"",
		                       str)));
	}
	if (tm2timestamp(&tm, fsec, &tz, &result) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
		                errmsg("timestamp out of range: \"%s\"", str)));
	}
	pfree(str);

	return result;
}

char *
get_string_value(const char *key, JsonbContainer *container)
{
	JsonbValue *v;

	v = get_json_key(key, container);

	if (!v)
	{
		return NULL;
	}

	if (v->type != jbvString)
	{
		ereport(ERROR,
		        errmsg("Expected string value at key %s, but got type %s", key,
		               jbvTypeName(v->type)));
	}

	return mydup(v->val.string.val, v->val.string.len);
}

int64
get_int_value(const char *key, JsonbContainer *container)
{
	JsonbValue *v;

	v = get_json_key(key, container);

	// Can't error here as probackup in some cases reports and in some cases
	//  omits some keys
	if (!v) return 0;

	if (v->type != jbvNumeric)
	{
		ereport(ERROR,
		        errmsg("Expected numeric value at key %s, but got type %s", key,
		               jbvTypeName(v->type)));
	}

	{
		char *str = DatumGetCString(DirectFunctionCall1(
		        numeric_out, PointerGetDatum(v->val.numeric)));
		return strtoll(str, NULL, 10);
	}
}
