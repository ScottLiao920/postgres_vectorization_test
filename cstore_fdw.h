/*-------------------------------------------------------------------------
 *
 * cstore_fdw.h
 *
 * Type and function declarations for CStore foreign data wrapper.
 *
 * Copyright (c) 2014, Citus Data, Inc.
 *
 * $Id$
 *
 *-------------------------------------------------------------------------
 */

#ifndef CSTORE_FDW_H
#define CSTORE_FDW_H

#include "access/tupdesc.h"
#include "fmgr.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "lib/stringinfo.h"
#include "lz4.h"
#include "untrusted/extensions/stdafx.h"
#include "untrusted/interface/interface.h"
#include <tools/base64.hpp>


/* Defines for valid option names */
#define OPTION_NAME_FILENAME "filename"
#define OPTION_NAME_COMPRESSION_TYPE "compression"
#define OPTION_NAME_STRIPE_ROW_COUNT "stripe_row_count"
#define OPTION_NAME_BLOCK_ROW_COUNT "block_row_count"

/* Default values for option parameters */
#define DEFAULT_COMPRESSION_TYPE COMPRESSION_NONE
#define DEFAULT_STRIPE_ROW_COUNT 150000
#define DEFAULT_BLOCK_ROW_COUNT 10000

/* Limits for option parameters */
#define STRIPE_ROW_COUNT_MINIMUM 1000
#define STRIPE_ROW_COUNT_MAXIMUM 10000000
#define BLOCK_ROW_COUNT_MINIMUM 1000
#define BLOCK_ROW_COUNT_MAXIMUM 100000

/* String representations of compression types */
#define COMPRESSION_STRING_NONE "none"
#define COMPRESSION_STRING_PG_LZ "pglz"
#define COMPRESSION_STRING_LZ4 "lz4"
#define COMPRESSION_STRING_ENC_LZ4 "enc_lz4"
#define COMPRESSION_STRING_DELIMITED_LIST "none, pglz, lz4, enc_lz4"

/* CStore file signature */
#define CSTORE_MAGIC_NUMBER "citus_cstore"
#define CSTORE_VERSION_MAJOR 1
#define CSTORE_VERSION_MINOR 1

/* miscellaneous defines */
#define CSTORE_FDW_NAME "cstore_fdw"
#define CSTORE_FOOTER_FILE_SUFFIX ".footer"
#define CSTORE_TEMP_FILE_SUFFIX ".tmp"
#define CSTORE_TUPLE_COST_MULTIPLIER 10
#define CSTORE_POSTSCRIPT_SIZE_LENGTH 1
#define CSTORE_POSTSCRIPT_SIZE_MAX 256


/*
 * CStoreValidOption keeps an option name and a context. When an option is passed
 * into cstore_fdw objects (server and foreign table), we compare this option's
 * name and context against those of valid options.
 */
typedef struct CStoreValidOption {
    const char *optionName;
    Oid optionContextId;

} CStoreValidOption;


/* Array of options that are valid for cstore_fdw */
static const uint32 ValidOptionCount = 4;
static const CStoreValidOption ValidOptionArray[] =
        {
                /* foreign table options */
                {OPTION_NAME_FILENAME,         ForeignTableRelationId},
                {OPTION_NAME_COMPRESSION_TYPE, ForeignTableRelationId},
                {OPTION_NAME_STRIPE_ROW_COUNT, ForeignTableRelationId},
                {OPTION_NAME_BLOCK_ROW_COUNT,  ForeignTableRelationId}
        };


/* Enumaration for cstore file's compression method */
typedef enum {
    COMPRESSION_TYPE_INVALID = -1,
    COMPRESSION_NONE = 0,
    COMPRESSION_PG_LZ = 1,
    COMPRESSION_LZ4 = 2,
    COMPRESSION_ENC_LZ4 = 3,
    COMPRESSION_ENC_NONE = 4,

    COMPRESSION_COUNT

} CompressionType;

typedef struct LZ4CompressHeader {
    size_t src_len; // original string length
    size_t comp_len; // length of compressed string
} LZ4CompressHeader;
#define CSTORE_COMPRESS_HDRSZ_LZ4       (sizeof(LZ4CompressHeader))
#define CSTORE_COMPRESS_RAWSIZE_LZ4(ptr) (((LZ4CompressHeader *) (ptr))->src_len)
#define CSTORE_COMPRESS_RAWDATA_LZ4(ptr) (((char *) (ptr)) + CSTORE_COMPRESS_HDRSZ_LZ4)
#define CSTORE_COMPRESS_SET_RAWSIZE_LZ4(ptr, len) (((LZ4CompressHeader *) (ptr))->src_len = (len))


/*
 * CStoreFdwOptions holds the option values to be used when reading or writing
 * a cstore file. To resolve these values, we first check foreign table's options,
 * and if not present, we then fall back to the default values specified above.
 */
typedef struct CStoreFdwOptions {
    char *filename;
    CompressionType compressionType;
    uint64 stripeRowCount;
    uint32 blockRowCount;

} CStoreFdwOptions;


/*
 * StripeMetadata represents information about a stripe. This information is
 * stored in the cstore file's footer.
 */
typedef struct StripeMetadata {
    uint64 fileOffset;
    uint64 skipListLength;
    uint64 dataLength;
    uint64 footerLength;

} StripeMetadata;


/* TableFooter represents the footer of a cstore file. */
typedef struct TableFooter {
    List *stripeMetadataList;
    uint64 blockRowCount;

} TableFooter;


/* ColumnBlockSkipNode contains statistics for a ColumnBlockData. */
typedef struct ColumnBlockSkipNode {
    /* statistics about values of a column block */
    bool hasMinMax;
    Datum minimumValue;
    Datum maximumValue;
    uint64 rowCount;

    /*
     * Offsets and sizes of value and exists streams in the column data.
     * These enable us to skip reading suppressed row blocks, and start reading
     * a block without reading previous blocks.
     */
    uint64 valueBlockOffset;
    uint64 valueLength;
    uint64 existsBlockOffset;
    uint64 existsLength;

    CompressionType valueCompressionType;

} ColumnBlockSkipNode;


/*
 * StripeSkipList can be used for skipping row blocks. It contains a column block
 * skip node for each block of each column. blockSkipNodeArray[column][block]
 * is the entry for the specified column block.
 */
typedef struct StripeSkipList {
    ColumnBlockSkipNode **blockSkipNodeArray;
    uint32 columnCount;
    uint32 blockCount;

} StripeSkipList;


/*
 * ColumnBlockData represents a block of data in a column. valueArray stores
 * the values of data, and existsArray stores whether a value is present.
 * There is a one-to-one correspondence between valueArray and existsArray.
 */
typedef struct ColumnBlockData {
    bool *existsArray;
    Datum *valueArray;

} ColumnBlockData;


/*
 * ColumnData represents data for a column in a row stripe. Each column is made
 * of multiple column blocks.
 */
typedef struct ColumnData {
    ColumnBlockData **blockDataArray;

} ColumnData;


/* StripeData represents data for a row stripe in a cstore file. */
typedef struct StripeData {
    uint32 columnCount;
    uint32 rowCount;
    ColumnData **columnDataArray;

} StripeData;


/*
 * StripeFooter represents a stripe's footer. In this footer, we keep three
 * arrays of sizes. The number of elements in each of the arrays is equal
 * to the number of columns.
 */
typedef struct StripeFooter {
    uint32 columnCount;
    uint64 *skipListSizeArray;
    uint64 *existsSizeArray;
    uint64 *valueSizeArray;

} StripeFooter;


/* TableReadState represents state of a cstore file read operation. */
typedef struct TableReadState {
    FILE *tableFile;
    TableFooter *tableFooter;
    TupleDesc tupleDescriptor;

    /*
     * List of Var pointers for columns in the query. We use this both for
     * getting vector of projected columns, and also when we want to build
     * base constraint to find selected row blocks.
     */
    List *projectedColumnList;

    List *whereClauseList;
    MemoryContext stripeReadContext;
    StripeData *stripeData;
    uint32 readStripeCount;
    uint64 stripeReadRowCount;

} TableReadState;


/* TableWriteState represents state of a cstore file write operation. */
typedef struct TableWriteState {
    FILE *tableFile;
    TableFooter *tableFooter;
    StringInfo tableFooterFilename;
    CompressionType compressionType;
    TupleDesc tupleDescriptor;
    FmgrInfo **comparisonFunctionArray;
    uint64 currentFileOffset;


    MemoryContext stripeWriteContext;
    StripeData *stripeData;
    StripeSkipList *stripeSkipList;
    uint32 stripeMaxRowCount;

} TableWriteState;


/* Function declarations for extension loading and unloading */
extern void _PG_init(void);

extern void _PG_fini(void);

/* event trigger function declarations */
extern Datum cstore_ddl_event_end_trigger(PG_FUNCTION_ARGS);

/* Function declarations for utility UDFs */
extern Datum cstore_table_size(PG_FUNCTION_ARGS);

/* Function declarations for foreign data wrapper */
extern Datum cstore_fdw_handler(PG_FUNCTION_ARGS);

extern Datum cstore_fdw_validator(PG_FUNCTION_ARGS);

/* Function declarations for writing to a cstore file */
extern TableWriteState *CStoreBeginWrite(const char *filename,
                                         CompressionType compressionType,
                                         uint64 stripeMaxRowCount,
                                         uint32 blockRowCount,
                                         TupleDesc tupleDescriptor);

extern void CStoreWriteRow(TableWriteState *state, Datum *columnValues,
                           bool *columnNulls);

extern void CStoreEndWrite(TableWriteState *state);

/* Function declarations for reading from a cstore file */
extern TableReadState *CStoreBeginRead(const char *filename, TupleDesc tupleDescriptor,
                                       List *projectedColumnList, List *qualConditions);

extern TableFooter *CStoreReadFooter(StringInfo tableFooterFilename);

extern bool CStoreReadFinished(TableReadState *state);

extern bool CStoreReadNextRow(TableReadState *state, Datum *columnValues,
                              bool *columnNulls);

extern void CStoreEndRead(TableReadState *state);

/* Function declarations for common functions */
extern FmgrInfo *GetFunctionInfoOrNull(Oid typeId, Oid accessMethodId,
                                       int16 procedureId);


#endif   /* CSTORE_FDW_H */ 
