PGFILEDESC = "Extension to talk to probackup3"

EXTENSION=probackup_ctl
DATA=probackup_ctl--1.0.sql

OBJS=exec.o catalog.o probackup.o storage.o utils.o probackup_ctl.o guc_vars.o show.o backup.o

MODULE_big=probackup_ctl

REGRESS=probackup_ctl

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
