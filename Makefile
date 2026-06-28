PGFILEDESC = "Extension to talk to probackup3"

EXTENSION=probackup_ctl
DATA=probackup_ctl--1.0.sql

OBJS=exec.o catalog.o probackup.o show.o storage.o utils.o probackup_ctl.o

MODULES=probackup_ctl

REGRESS=probackup_ctl

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
