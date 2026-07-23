Extension is for controlling probackup catalogs from within postgres database.

# Interface
Extension creates schema "probackup" which contains:
Table "catalogs" which describes catalog: id, backup path and storage detauls (is it either FS/SFTP/S3).
Table "s3_config" which contains connection parameters to S3.
Table "sftp_config" which contains connection parameters for SFTP.

Function "register_catalog" which takes backup_path and storage details, checks
connection parameters and creates catalog record.

Function "show" which it returns all the backups in all catalogs. The list can
be limited to specific catalog and instance.

Function "backup" which takes catalog and instance and runs a backup in that instance.

Function "delete" which takes catalog, instance and backup_id and deletes that backup.

Function "log" which takes a catalog, instance and backup_id and returns backup log. Only works for local FS probackup3.

GUC `probackup_ctl.log_commands (default false)
enables logging of executed probackup command. Use for debugging.

GUC 'probackup_ctl.command' (default pg_probackup)
Is a path to probackup command which should be used in your session.


# Setup
1. LOAD 'probackup_ctl';

Build the extension:
```
make clean && make install
```
Install it into server:
```
create extension probackup_ctl;
```
Setup storages:
SFTP storage
```
insert into probackup.sftp_config (name, sftp_hostname, sftp_port, sftp_user) values ('ssh0', 'localhost', '', '');
```
S3 storage, note: keys are openly accessible here!:
```
insert into probackup.s3_config (name, s3_hostname, s3_port, s3_access_key, s3_secret_key, s3_region, s3_bucket, s3_https) values ('locals3', 'localhost', '9000', 'minioadmin', 'minioadmin', 'us-west-2', 's3demo', 'OFF');
```
Now you can access and register catalogs:

Regular file system catalog (version 2):
```
select probackup.register_catalog('/tmp/b2/',storage => 'fs', probackup_bin=>'pg_probackup');
```

Regular file system catalog (version 3):
```
select probackup.register_catalog('/tmp/b3/',storage => 'fs', probackup_bin=>'pg_probackup3');
```

SFTP catalog (ver 3):
```
select probackup.register_catalog('/tmp/b3-ssh/',storage => 'sftp', storage_name => 'ssh0', probackup_bin=>'pg_probackup3');
```

S3 catalog (ver 2, postgres 18):
```
select probackup.register_catalog('/tmp/b3-s3/',storage => 's3', storage_name => 'locals3', probackup_bin=>'pg_probackup');
```

Now you can list all backups in all catalogs:
```
select * from probackup.show();
```
list backups in a particular catalog:
```
select * from probackup.show() where catalog_id=1;
select * from probackup.show(catalog_id=>1);
```

...or make a backup:
```
select * from probackup.backup(catalog_id=>2, pg_instance=>'dba1', wal_mode=>'stream');
```
or enable command logging for debugging problems:
```
set probackup_ctl.log_commands=true;
```

Let's delete a backup:
```
select * from probackup.delete(catalog_id=>3, pg_instance=>'dba1', backup_id=>'2026-06-20-21-31-20-518');
```

For probackup version 3 we can check logfile of an old backup:
```
 select * from probackup.log(catalog_id=>1,pg_instance=>'dba1',backup_id=>'2026-07-23-12-40-19-663');
```
