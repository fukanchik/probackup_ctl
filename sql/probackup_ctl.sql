CREATE EXTENSION probackup_ctl version '1.0';

SELECT probackup.register_catalog('/tmp/b2');

SELECT * from probackup.catalogs;
