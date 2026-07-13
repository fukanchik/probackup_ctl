CREATE EXTENSION probackup_ctl version '1.0';


SELECT probackup.register_catalog(backup_path => NULL);

SELECT probackup.register_catalog(backup_path => '/tmp/b2');

SELECT * from probackup.catalogs;
