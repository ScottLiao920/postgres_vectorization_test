# cstore_fdw/Makefile
#
# Copyright (c) 2014 Citus Data, Inc.
#

MODULE_big = cstore_fdw

SDK_INSTALL_PATH := /opt/intel/sgxsdk
SGX_INCLUDE_PATH := $(SDK_INSTALL_PATH)/include
UNTRUSTED_DIR=untrusted
INTERFACE_DIR=untrusted/interface
EXTENSION_DIR=untrusted/extensions
LZ4_DIR=untrusted/lz4

PG_CPPFLAGS := -o0 --std=c99 \
	$(addprefix -I$(CURDIR)/../../, include $(UNTRUSTED_DIR) $(CSTORE_DIR) $(LZ4_DIR)/lib) \
	-I$(SGX_INCLUDE_PATH)
SHLIB_LINK = -lprotobuf-c
OBJS = cstore.pb-c.o cstore_fdw.o cstore_writer.o cstore_reader.o \
       cstore_metadata_serialization.o vectorized_aggregates.o \
       vectorized_transition_functions.o


EXTENSION = cstore_fdw
DATA = cstore_fdw--1.1.sql cstore_fdw--1.0--1.1.sql

REGRESS = create load query analyze data_types functions block_filtering drop
EXTRA_CLEAN = cstore.pb-c.h cstore.pb-c.c data/*.cstore data/*.cstore.footer \
              sql/block_filtering.sql sql/create.sql sql/data_types.sql sql/load.sql \
              expected/block_filtering.out expected/create.out expected/data_types.out \
              expected/load.out

ifeq ($(enable_coverage),yes)
	PG_CPPFLAGS += --coverage
	SHLIB_LINK  += --coverage
	EXTRA_CLEAN += *.gcno
endif

#
# Users need to specify their Postgres installation path through pg_config. For
# example: /usr/local/pgsql/bin/pg_config or /usr/lib/postgresql/9.3/bin/pg_config
#

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

ifndef MAJORVERSION
    MAJORVERSION := $(basename $(VERSION))
endif

ifeq (,$(findstring $(MAJORVERSION), 9.3 9.4))
    $(error PostgreSQL 9.3 or 9.4 is required to compile this extension)
endif

cstore.pb-c.c: cstore.proto
	protoc-c --c_out=. cstore.proto

installcheck: remove_cstore_files

remove_cstore_files:
	rm -f data/*.cstore data/*.cstore.footer
