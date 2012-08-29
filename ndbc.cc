/*
Copyright (c) 2012 ICRL

See the file license.txt for copying permission.

Resource: ndbc.cc

Maps a subset of the ODBC API into a node.js plugin.
Includes additional features designed to streamline some common ODBC tasks.
Detailed usage of each function is documented prior to each function definition.

Features implemented:

ODBC API (1:1 with ODBC functions)
SQLAllocHandle
SQLFreeHandle
SQLSetEnvAttr
SQLGetEnvAttr
SQLConnect
SQLDisconnect
SQLSetConnectAttr
SQLGetConnectAttr
SQLGetInfo
SQLSetStmtAttr
SQLGetStmtAttr
SQLExecDirect
SQLRowCount

ndbc Extensions:
JsonDescribe - Inspects a completed statement's result set and returns a formatting string that
               can be used to export data with JsonData.
JsonHeader - Returns a string containing the header of a JSON-formatted result set.
             The result set takes the form of a 2-dimensional array, with the first row containing the column headers.
JsonData - Returns one or more rows from a completed result set.
           Requires a row formatting string like that provided by JsonDescribe.
JsonTrailer - Returns the character string required to close out the result set (thus far, "]").

Change History
Date        Author                Description
----------------------------------------------------------------------------------------------------------------------------
2012-07-28  Gregory Dow           Initial release.  Includes enough features to connect to a named data source, and
                                  manipulate and retrieve data by submitting SQL statements.
*/

/*
TODO:

Found out on 2012-08-09 that external handles, when passed to and from a child process, are not always still usable when
returned to the main process.  Need to create a test version where the connection can be checked using C code only to
determine whether a child process can connect to a database at all.
*/

/* Include node.js API.
 */
#include <node.h>
#include <v8.h>

/* Windows include for windows environments.
 */
#if defined (__WIN32__)
#include <windows.h>
#endif

/* Include ODBC API.
 */
#include <sql.h>
#include <sqlext.h>

using namespace v8;

/* String representations of ODBC constants.
 * Values are sorted alphabetically to ease deduplication.
 */
#define ndbcSQL_ACCESSIBLE_PROCEDURES String::NewSymbol("SQL_ACCESSIBLE_PROCEDURES")
#define ndbcSQL_ACCESSIBLE_TABLES String::NewSymbol("SQL_ACCESSIBLE_TABLES")
#define ndbcSQL_ACTIVE_ENVIRONMENTS String::NewSymbol("SQL_ACTIVE_ENVIRONMENTS")
#define ndbcSQL_AD_ADD_CONSTRAINT_DEFERRABLE String::NewSymbol("SQL_AD_ADD_CONSTRAINT_DEFERRABLE")
#define ndbcSQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED String::NewSymbol("SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED")
#define ndbcSQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE String::NewSymbol("SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE")
#define ndbcSQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE String::NewSymbol("SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE")
#define ndbcSQL_AD_ADD_DOMAIN_CONSTRAINT String::NewSymbol("SQL_AD_ADD_DOMAIN_CONSTRAINT")
#define ndbcSQL_AD_ADD_DOMAIN_DEFAULT String::NewSymbol("SQL_AD_ADD_DOMAIN_DEFAULT")
#define ndbcSQL_AD_CONSTRAINT_NAME_DEFINITION String::NewSymbol("SQL_AD_CONSTRAINT_NAME_DEFINITION")
#define ndbcSQL_AD_DROP_DOMAIN_CONSTRAINT String::NewSymbol("SQL_AD_DROP_DOMAIN_CONSTRAINT")
#define ndbcSQL_AD_DROP_DOMAIN_DEFAULT String::NewSymbol("SQL_AD_DROP_DOMAIN_DEFAULT")
#define ndbcSQL_AF_ALL String::NewSymbol("SQL_AF_ALL")
#define ndbcSQL_AF_AVG String::NewSymbol("SQL_AF_AVG")
#define ndbcSQL_AF_COUNT String::NewSymbol("SQL_AF_COUNT")
#define ndbcSQL_AF_DISTINCT String::NewSymbol("SQL_AF_DISTINCT")
#define ndbcSQL_AF_MAX String::NewSymbol("SQL_AF_MAX")
#define ndbcSQL_AF_MIN String::NewSymbol("SQL_AF_MIN")
#define ndbcSQL_AF_SUM String::NewSymbol("SQL_AF_SUM")
#define ndbcSQL_AGGREGATE_FUNCTIONS String::NewSymbol("SQL_AGGREGATE_FUNCTIONS")
#define ndbcSQL_ALTER_DOMAIN String::NewSymbol("SQL_ALTER_DOMAIN")
#define ndbcSQL_ALTER_TABLE String::NewSymbol("SQL_ALTER_TABLE")
#define ndbcSQL_AM_CONNECTION String::NewSymbol("SQL_AM_CONNECTION")
#define ndbcSQL_AM_NONE String::NewSymbol("SQL_AM_NONE")
#define ndbcSQL_AM_STATEMENT String::NewSymbol("SQL_AM_STATEMENT")
#define ndbcSQL_ASYNC_DBC_CAPABLE String::NewSymbol("SQL_ASYNC_DBC_CAPABLE")
#define ndbcSQL_ASYNC_DBC_FUNCTIONS String::NewSymbol("SQL_ASYNC_DBC_FUNCTIONS")
#define ndbcSQL_ASYNC_DBC_NOT_CAPABLE String::NewSymbol("SQL_ASYNC_DBC_NOT_CAPABLE")
#define ndbcSQL_ASYNC_ENABLE_OFF String::NewSymbol("SQL_ASYNC_ENABLE_OFF")
#define ndbcSQL_ASYNC_ENABLE_ON String::NewSymbol("SQL_ASYNC_ENABLE_ON")
#define ndbcSQL_ASYNC_MODE String::NewSymbol("SQL_ASYNC_MODE")
#define ndbcSQL_AT_ADD_COLUMN_COLLATION String::NewSymbol("SQL_AT_ADD_COLUMN_COLLATION")
#define ndbcSQL_AT_ADD_COLUMN_DEFAULT String::NewSymbol("SQL_AT_ADD_COLUMN_DEFAULT")
#define ndbcSQL_AT_ADD_COLUMN_SINGLE String::NewSymbol("SQL_AT_ADD_COLUMN_SINGLE")
#define ndbcSQL_AT_ADD_CONSTRAINT String::NewSymbol("SQL_AT_ADD_CONSTRAINT")
#define ndbcSQL_AT_ADD_TABLE_CONSTRAINT String::NewSymbol("SQL_AT_ADD_TABLE_CONSTRAINT")
#define ndbcSQL_AT_CONSTRAINT_DEFERRABLE String::NewSymbol("SQL_AT_CONSTRAINT_DEFERRABLE")
#define ndbcSQL_AT_CONSTRAINT_INITIALLY_DEFERRED String::NewSymbol("SQL_AT_CONSTRAINT_INITIALLY_DEFERRED")
#define ndbcSQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE String::NewSymbol("SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE")
#define ndbcSQL_AT_CONSTRAINT_NAME_DEFINITION String::NewSymbol("SQL_AT_CONSTRAINT_NAME_DEFINITION")
#define ndbcSQL_AT_CONSTRAINT_NON_DEFERRABLE String::NewSymbol("SQL_AT_CONSTRAINT_NON_DEFERRABLE")
#define ndbcSQL_AT_DROP_COLUMN_CASCADE String::NewSymbol("SQL_AT_DROP_COLUMN_CASCADE")
#define ndbcSQL_AT_DROP_COLUMN_DEFAULT String::NewSymbol("SQL_AT_DROP_COLUMN_DEFAULT")
#define ndbcSQL_AT_DROP_COLUMN_RESTRICT String::NewSymbol("SQL_AT_DROP_COLUMN_RESTRICT")
#define ndbcSQL_AT_DROP_TABLE_CONSTRAINT_CASCADE String::NewSymbol("SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE")
#define ndbcSQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT String::NewSymbol("SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT")
#define ndbcSQL_AT_SET_COLUMN_DEFAULT String::NewSymbol("SQL_AT_SET_COLUMN_DEFAULT")
#define ndbcSQL_ATTR_ACCESS_MODE String::NewSymbol("SQL_ATTR_ACCESS_MODE")
#define ndbcSQL_ATTR_APP_PARAM_DESC String::NewSymbol("SQL_ATTR_APP_PARAM_DESC")
#define ndbcSQL_ATTR_APP_ROW_DESC String::NewSymbol("SQL_ATTR_APP_ROW_DESC")
#define ndbcSQL_ATTR_ASYNC_ENABLE String::NewSymbol("SQL_ATTR_ASYNC_ENABLE")
#define ndbcSQL_ATTR_AUTO_IPD String::NewSymbol("SQL_ATTR_AUTO_IPD")
#define ndbcSQL_ATTR_AUTOCOMMIT String::NewSymbol("SQL_ATTR_AUTOCOMMIT")
#define ndbcSQL_ATTR_CONCURRENCY String::NewSymbol("SQL_ATTR_CONCURRENCY")
#define ndbcSQL_ATTR_CONNECTION_DEAD String::NewSymbol("SQL_ATTR_CONNECTION_DEAD")
#define ndbcSQL_ATTR_CONNECTION_POOLING String::NewSymbol("SQL_ATTR_CONNECTION_POOLING")
#define ndbcSQL_ATTR_CONNECTION_TIMEOUT String::NewSymbol("SQL_ATTR_CONNECTION_TIMEOUT")
#define ndbcSQL_ATTR_CP_MATCH String::NewSymbol("SQL_ATTR_CP_MATCH")
#define ndbcSQL_ATTR_CURRENT_CATALOG String::NewSymbol("SQL_ATTR_CURRENT_CATALOG")
#define ndbcSQL_ATTR_CURSOR_SCROLLABLE String::NewSymbol("SQL_ATTR_CURSOR_SCROLLABLE")
#define ndbcSQL_ATTR_CURSOR_SENSITIVITY String::NewSymbol("SQL_ATTR_CURSOR_SENSITIVITY")
#define ndbcSQL_ATTR_CURSOR_TYPE String::NewSymbol("SQL_ATTR_CURSOR_TYPE")
#define ndbcSQL_ATTR_ENABLE_AUTO_IPD String::NewSymbol("SQL_ATTR_ENABLE_AUTO_IPD")
#define ndbcSQL_ATTR_ENLIST_IN_DTC String::NewSymbol("SQL_ATTR_ENLIST_IN_DTC")
#define ndbcSQL_ATTR_FETCH_BOOKMARK_PTR String::NewSymbol("SQL_ATTR_FETCH_BOOKMARK_PTR")
#define ndbcSQL_ATTR_IMP_PARAM_DESC String::NewSymbol("SQL_ATTR_IMP_PARAM_DESC")
#define ndbcSQL_ATTR_IMP_ROW_DESC String::NewSymbol("SQL_ATTR_IMP_ROW_DESC")
#define ndbcSQL_ATTR_KEYSET_SIZE String::NewSymbol("SQL_ATTR_KEYSET_SIZE")
#define ndbcSQL_ATTR_LOGIN_TIMEOUT String::NewSymbol("SQL_ATTR_LOGIN_TIMEOUT")
#define ndbcSQL_ATTR_MAX_LENGTH String::NewSymbol("SQL_ATTR_MAX_LENGTH")
#define ndbcSQL_ATTR_MAX_ROWS String::NewSymbol("SQL_ATTR_MAX_ROWS")
#define ndbcSQL_ATTR_METADATA_ID String::NewSymbol("SQL_ATTR_METADATA_ID")
#define ndbcSQL_ATTR_NOSCAN String::NewSymbol("SQL_ATTR_NOSCAN")
#define ndbcSQL_ATTR_ODBC_CURSORS String::NewSymbol("SQL_ATTR_ODBC_CURSORS")
#define ndbcSQL_ATTR_ODBC_VERSION String::NewSymbol("SQL_ATTR_ODBC_VERSION")
#define ndbcSQL_ATTR_OUTPUT_NTS String::NewSymbol("SQL_OUTPUT_NTS")
#define ndbcSQL_ATTR_PACKET_SIZE String::NewSymbol("SQL_ATTR_PACKET_SIZE")
#define ndbcSQL_ATTR_PARAM_BIND_OFFSET_PTR String::NewSymbol("SQL_ATTR_PARAM_BIND_OFFSET_PTR")
#define ndbcSQL_ATTR_PARAM_BIND_TYPE String::NewSymbol("SQL_ATTR_PARAM_BIND_TYPE")
#define ndbcSQL_ATTR_PARAM_OPERATION_PTR String::NewSymbol("SQL_ATTR_PARAM_OPERATION_PTR")
#define ndbcSQL_ATTR_PARAM_STATUS_PTR String::NewSymbol("SQL_ATTR_PARAM_STATUS_PTR")
#define ndbcSQL_ATTR_PARAMS_PROCESSED_PTR String::NewSymbol("SQL_ATTR_PARAMS_PROCESSED_PTR")
#define ndbcSQL_ATTR_PARAMSET_SIZE String::NewSymbol("SQL_ATTR_PARAMSET_SIZE")
#define ndbcSQL_ATTR_QUERY_TIMEOUT String::NewSymbol("SQL_ATTR_QUERY_TIMEOUT")
#define ndbcSQL_ATTR_QUIET_MODE String::NewSymbol("SQL_ATTR_QUIET_MODE")
#define ndbcSQL_ATTR_RETRIEVE_DATA String::NewSymbol("SQL_ATTR_RETRIEVE_DATA")
#define ndbcSQL_ATTR_ROW_ARRAY_SIZE String::NewSymbol("SQL_ATTR_ROW_ARRAY_SIZE")
#define ndbcSQL_ATTR_ROW_BIND_OFFSET_PTR String::NewSymbol("SQL_ATTR_ROW_BIND_OFFSET_PTR")
#define ndbcSQL_ATTR_ROW_BIND_TYPE String::NewSymbol("SQL_ATTR_ROW_BIND_TYPE")
#define ndbcSQL_ATTR_ROW_NUMBER String::NewSymbol("SQL_ATTR_ROW_NUMBER")
#define ndbcSQL_ATTR_ROW_OPERATION_PTR String::NewSymbol("SQL_ATTR_ROW_OPERATION_PTR")
#define ndbcSQL_ATTR_ROW_STATUS_PTR String::NewSymbol("SQL_ATTR_ROW_STATUS_PTR")
#define ndbcSQL_ATTR_ROWS_FETCHED_PTR String::NewSymbol("SQL_ATTR_ROWS_FETCHED_PTR")
#define ndbcSQL_ATTR_SIMULATE_CURSOR String::NewSymbol("SQL_ATTR_SIMULATE_CURSOR")
#define ndbcSQL_ATTR_TRACE String::NewSymbol("SQL_ATTR_TRACE")
#define ndbcSQL_ATTR_TRACEFILE String::NewSymbol("SQL_ATTR_TRACEFILE")
#define ndbcSQL_ATTR_TRANSLATE_LIB String::NewSymbol("SQL_ATTR_TRANSLATE_LIB")
#define ndbcSQL_ATTR_TRANSLATE_OPTION String::NewSymbol("SQL_ATTR_TRANSLATE_OPTION")
#define ndbcSQL_ATTR_TXN_ISOLATION String::NewSymbol("SQL_ATTR_TXN_ISOLATION")
#define ndbcSQL_ATTR_USE_BOOKMARKS String::NewSymbol("SQL_ATTR_USE_BOOKMARKS")
#define ndbcSQL_AUTOCOMMIT_OFF String::NewSymbol("SQL_AUTOCOMMIT_OFF")
#define ndbcSQL_AUTOCOMMIT_ON String::NewSymbol("SQL_AUTOCOMMIT_ON")
#define ndbcSQL_BATCH_ROW_COUNT String::NewSymbol("SQL_BATCH_ROW_COUNT")
#define ndbcSQL_BATCH_SUPPORT String::NewSymbol("SQL_BATCH_SUPPORT")
#define ndbcSQL_BIND_BY_COLUMN String::NewSymbol("SQL_BIND_BY_COLUMN")
#define ndbcSQL_BOOKMARK_PERSISTENCE String::NewSymbol("SQL_BOOKMARK_PERSISTENCE")
#define ndbcSQL_BP_CLOSE String::NewSymbol("SQL_BP_CLOSE")
#define ndbcSQL_BP_DELETE String::NewSymbol("SQL_BP_DELETE")
#define ndbcSQL_BP_DROP String::NewSymbol("SQL_BP_DROP")
#define ndbcSQL_BP_OTHER_HSTMT String::NewSymbol("SQL_BP_OTHER_HSTMT")
#define ndbcSQL_BP_TRANSACTION String::NewSymbol("SQL_BP_TRANSACTION")
#define ndbcSQL_BP_UPDATE String::NewSymbol("SQL_BP_UPDATE")
#define ndbcSQL_BRC_EXPLICIT String::NewSymbol("SQL_BRC_EXPLICIT")
#define ndbcSQL_BRC_PROCEDURES String::NewSymbol("SQL_BRC_PROCEDURES")
#define ndbcSQL_BRC_ROLLED_UP String::NewSymbol("SQL_BRC_ROLLED_UP")
#define ndbcSQL_BS_ROW_COUNT_EXPLICIT String::NewSymbol("SQL_BS_ROW_COUNT_EXPLICIT")
#define ndbcSQL_BS_ROW_COUNT_PROC String::NewSymbol("SQL_BS_ROW_COUNT_PROC")
#define ndbcSQL_BS_SELECT_EXPLICIT String::NewSymbol("SQL_BS_SELECT_EXPLICIT")
#define ndbcSQL_BS_SELECT_PROC String::NewSymbol("SQL_BS_SELECT_PROC")
#define ndbcSQL_CA_CONSTRAINT_DEFERRABLE String::NewSymbol("SQL_CA_CONSTRAINT_DEFERRABLE")
#define ndbcSQL_CA_CONSTRAINT_INITIALLY_DEFERRED String::NewSymbol("SQL_CA_CONSTRAINT_INITIALLY_DEFERRED")
#define ndbcSQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE String::NewSymbol("SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE")
#define ndbcSQL_CA_CONSTRAINT_NON_DEFERRABLE String::NewSymbol("SQL_CA_CONSTRAINT_NON_DEFERRABLE")
#define ndbcSQL_CA_CREATE_ASSERTION String::NewSymbol("SQL_CA_CREATE_ASSERTION")
#define ndbcSQL_CA1_ABSOLUTE String::NewSymbol("SQL_CA1_ABSOLUTE")
#define ndbcSQL_CA1_BOOKMARK String::NewSymbol("SQL_CA1_BOOKMARK")
#define ndbcSQL_CA1_BULK_ADD String::NewSymbol("SQL_CA1_BULK_ADD")
#define ndbcSQL_CA1_BULK_DELETE_BY_BOOKMARK String::NewSymbol("SQL_CA1_BULK_DELETE_BY_BOOKMARK")
#define ndbcSQL_CA1_BULK_FETCH_BY_BOOKMARK String::NewSymbol("SQL_CA1_BULK_FETCH_BY_BOOKMARK")
#define ndbcSQL_CA1_BULK_UPDATE_BY_BOOKMARK String::NewSymbol("SQL_CA1_BULK_UPDATE_BY_BOOKMARK")
#define ndbcSQL_CA1_LOCK_EXCLUSIVE String::NewSymbol("SQL_CA1_LOCK_EXCLUSIVE")
#define ndbcSQL_CA1_LOCK_NO_CHANGE String::NewSymbol("SQL_CA1_LOCK_NO_CHANGE")
#define ndbcSQL_CA1_LOCK_UNLOCK String::NewSymbol("SQL_CA1_LOCK_UNLOCK")
#define ndbcSQL_CA1_NEXT String::NewSymbol("SQL_CA1_NEXT")
#define ndbcSQL_CA1_POS_DELETE String::NewSymbol("SQL_CA1_POS_DELETE")
#define ndbcSQL_CA1_POS_POSITION String::NewSymbol("SQL_CA1_POS_POSITION")
#define ndbcSQL_CA1_POS_REFRESH String::NewSymbol("SQL_CA1_POS_REFRESH")
#define ndbcSQL_CA1_POS_UPDATE String::NewSymbol("SQL_CA1_POS_UPDATE")
#define ndbcSQL_CA1_POSITIONED_DELETE String::NewSymbol("SQL_CA1_POSITIONED_DELETE")
#define ndbcSQL_CA1_POSITIONED_UPDATE String::NewSymbol("SQL_CA1_POSITIONED_UPDATE")
#define ndbcSQL_CA1_RELATIVE String::NewSymbol("SQL_CA1_RELATIVE")
#define ndbcSQL_CA1_SELECT_FOR_UPDATE String::NewSymbol("SQL_CA1_SELECT_FOR_UPDATE")
#define ndbcSQL_CA2_CRC_APPROXIMATE String::NewSymbol("SQL_CA2_CRC_APPROXIMATE")
#define ndbcSQL_CA2_CRC_EXACT String::NewSymbol("SQL_CA2_CRC_EXACT")
#define ndbcSQL_CA2_LOCK_CONCURRENCY String::NewSymbol("SQL_CA2_LOCK_CONCURRENCY")
#define ndbcSQL_CA2_MAX_ROWS_AFFECTS_ALL String::NewSymbol("SQL_CA2_MAX_ROWS_AFFECTS_ALL")
#define ndbcSQL_CA2_MAX_ROWS_CATALOG String::NewSymbol("SQL_CA2_MAX_ROWS_CATALOG")
#define ndbcSQL_CA2_MAX_ROWS_DELETE String::NewSymbol("SQL_CA2_MAX_ROWS_DELETE")
#define ndbcSQL_CA2_MAX_ROWS_INSERT String::NewSymbol("SQL_CA2_MAX_ROWS_INSERT")
#define ndbcSQL_CA2_MAX_ROWS_SELECT String::NewSymbol("SQL_CA2_MAX_ROWS_SELECT")
#define ndbcSQL_CA2_MAX_ROWS_UPDATE String::NewSymbol("SQL_CA2_MAX_ROWS_UPDATE")
#define ndbcSQL_CA2_OPT_ROWVER_CONCURRENCY String::NewSymbol("SQL_CA2_OPT_ROWVER_CONCURRENCY")
#define ndbcSQL_CA2_OPT_VALUES_CONCURRENCY String::NewSymbol("SQL_CA2_OPT_VALUES_CONCURRENCY")
#define ndbcSQL_CA2_READ_ONLY_CONCURRENCY String::NewSymbol("SQL_CA2_READ_ONLY_CONCURRENCY")
#define ndbcSQL_CA2_SENSITIVITY_ADDITIONS String::NewSymbol("SQL_CA2_SENSITIVITY_ADDITIONS")
#define ndbcSQL_CA2_SENSITIVITY_DELETIONS String::NewSymbol("SQL_CA2_SENSITIVITY_DELETIONS")
#define ndbcSQL_CA2_SENSITIVITY_UPDATES String::NewSymbol("SQL_CA2_SENSITIVITY_UPDATES")
#define ndbcSQL_CA2_SIMULATE_NON_UNIQUE String::NewSymbol("SQL_CA2_SIMULATE_NON_UNIQUE")
#define ndbcSQL_CA2_SIMULATE_TRY_UNIQUE String::NewSymbol("SQL_CA2_SIMULATE_TRY_UNIQUE")
#define ndbcSQL_CA2_SIMULATE_UNIQUE String::NewSymbol("SQL_CA2_SIMULATE_UNIQUE")
#define ndbcSQL_CATALOG_LOCATION String::NewSymbol("SQL_CATALOG_LOCATION")
#define ndbcSQL_CATALOG_NAME String::NewSymbol("SQL_CATALOG_NAME")
#define ndbcSQL_CATALOG_NAME_SEPARATOR String::NewSymbol("SQL_CATALOG_NAME_SEPARATOR")
#define ndbcSQL_CATALOG_TERM String::NewSymbol("SQL_CATALOG_TERM")
#define ndbcSQL_CATALOG_USAGE String::NewSymbol("SQL_CATALOG_USAGE")
#define ndbcSQL_CB_CLOSE String::NewSymbol("SQL_CB_CLOSE")
#define ndbcSQL_CB_DELETE String::NewSymbol("SQL_CB_DELETE")
#define ndbcSQL_CB_NON_NULL String::NewSymbol("SQL_CB_NON_NULL")
#define ndbcSQL_CB_NULL String::NewSymbol("SQL_CB_NULL")
#define ndbcSQL_CB_PRESERVE String::NewSymbol("SQL_CB_PRESERVE")
#define ndbcSQL_CCOL_CREATE_COLLATION String::NewSymbol("SQL_CCOL_CREATE_COLLATION")
#define ndbcSQL_CCS_COLLATE_CLAUSE String::NewSymbol("SQL_CCS_COLLATE_CLAUSE")
#define ndbcSQL_CCS_CREATE_CHARACTER_SET String::NewSymbol("SQL_CCS_CREATE_CHARACTER_SET")
#define ndbcSQL_CCS_LIMITED_COLLATION String::NewSymbol("SQL_CCS_LIMITED_COLLATION")
#define ndbcSQL_CD_FALSE String::NewSymbol("SQL_CD_FALSE")
#define ndbcSQL_CD_TRUE String::NewSymbol("SQL_CD_TRUE")
#define ndbcSQL_CDO_COLLATION String::NewSymbol("SQL_CDO_COLLATION")
#define ndbcSQL_CDO_CONSTRAINT String::NewSymbol("SQL_CDO_CONSTRAINT")
#define ndbcSQL_CDO_CONSTRAINT_DEFERRABLE String::NewSymbol("SQL_CDO_CONSTRAINT_DEFERRABLE")
#define ndbcSQL_CDO_CONSTRAINT_INITIALLY_DEFERRED String::NewSymbol("SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED")
#define ndbcSQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE String::NewSymbol("SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE")
#define ndbcSQL_CDO_CONSTRAINT_NAME_DEFINITION String::NewSymbol("SQL_CDO_CONSTRAINT_NAME_DEFINITION")
#define ndbcSQL_CDO_CONSTRAINT_NON_DEFERRABLE String::NewSymbol("SQL_CDO_CONSTRAINT_NON_DEFERRABLE")
#define ndbcSQL_CDO_CREATE_DOMAIN String::NewSymbol("SQL_CDO_CREATE_DOMAIN")
#define ndbcSQL_CDO_DEFAULT String::NewSymbol("SQL_CDO_DEFAULT")
#define ndbcSQL_CL_END String::NewSymbol("SQL_CL_END")
#define ndbcSQL_CL_NOT_SUPPORTED String::NewSymbol("SQL_CL_NOT_SUPPORTED")
#define ndbcSQL_CL_START String::NewSymbol("SQL_CL_START")
#define ndbcSQL_CN_ANY String::NewSymbol("SQL_CN_ANY")
#define ndbcSQL_CN_DIFFERENT String::NewSymbol("SQL_CN_DIFFERENT")
#define ndbcSQL_CN_NONE String::NewSymbol("SQL_CN_NONE")
#define ndbcSQL_COLLATION_SEQ String::NewSymbol("SQL_COLLATION_SEQ")
#define ndbcSQL_COLUMN_ALIAS String::NewSymbol("SQL_COLUMN_ALIAS")
#define ndbcSQL_CONCAT_NULL_BEHAVIOR String::NewSymbol("SQL_CONCAT_NULL_BEHAVIOR")
#define ndbcSQL_CONCUR_LOCK String::NewSymbol("SQL_CONCUR_LOCK")
#define ndbcSQL_CONCUR_READ_ONLY String::NewSymbol("SQL_CONCUR_READ_ONLY")
#define ndbcSQL_CONCUR_ROWVER String::NewSymbol("SQL_CONCUR_ROWVER")
#define ndbcSQL_CONCUR_VALUES String::NewSymbol("SQL_CONCUR_VALUES")
#define ndbcSQL_CONVERT_BIGINT String::NewSymbol("SQL_CONVERT_BIGINT")
#define ndbcSQL_CONVERT_BINARY String::NewSymbol("SQL_CONVERT_BINARY")
#define ndbcSQL_CONVERT_BIT String::NewSymbol("SQL_CONVERT_BIT")
#define ndbcSQL_CONVERT_CHAR String::NewSymbol("SQL_CONVERT_CHAR")
#define ndbcSQL_CONVERT_DATE String::NewSymbol("SQL_CONVERT_DATE")
#define ndbcSQL_CONVERT_DECIMAL String::NewSymbol("SQL_CONVERT_DECIMAL")
#define ndbcSQL_CONVERT_DOUBLE String::NewSymbol("SQL_CONVERT_DOUBLE")
#define ndbcSQL_CONVERT_FLOAT String::NewSymbol("SQL_CONVERT_FLOAT")
#define ndbcSQL_CONVERT_FUNCTIONS String::NewSymbol("SQL_CONVERT_FUNCTIONS")
#define ndbcSQL_CONVERT_GUID String::NewSymbol("SQL_CONVERT_GUID")
#define ndbcSQL_CONVERT_INTEGER String::NewSymbol("SQL_CONVERT_INTEGER")
#define ndbcSQL_CONVERT_INTERVAL_DAY_TIME String::NewSymbol("SQL_CONVERT_INTERVAL_DAY_TIME")
#define ndbcSQL_CONVERT_INTERVAL_YEAR_MONTH String::NewSymbol("SQL_CONVERT_INTERVAL_YEAR_MONTH")
#define ndbcSQL_CONVERT_LONGVARBINARY String::NewSymbol("SQL_CONVERT_LONGVARBINARY")
#define ndbcSQL_CONVERT_LONGVARCHAR String::NewSymbol("SQL_CONVERT_LONGVARCHAR")
#define ndbcSQL_CONVERT_NUMERIC String::NewSymbol("SQL_CONVERT_NUMERIC")
#define ndbcSQL_CONVERT_REAL String::NewSymbol("SQL_CONVERT_REAL")
#define ndbcSQL_CONVERT_SMALLINT String::NewSymbol("SQL_CONVERT_SMALLINT")
#define ndbcSQL_CONVERT_TIME String::NewSymbol("SQL_CONVERT_TIME")
#define ndbcSQL_CONVERT_TIMESTAMP String::NewSymbol("SQL_CONVERT_TIMESTAMP")
#define ndbcSQL_CONVERT_TINYINT String::NewSymbol("SQL_CONVERT_TINYINT")
#define ndbcSQL_CONVERT_VARBINARY String::NewSymbol("SQL_CONVERT_VARBINARY")
#define ndbcSQL_CONVERT_VARCHAR String::NewSymbol("SQL_CONVERT_VARCHAR")
#define ndbcSQL_CORRELATION_NAME String::NewSymbol("SQL_CORRELATION_NAME")
#define ndbcSQL_CP_OFF String::NewSymbol("SQL_CP_OFF")
#define ndbcSQL_CP_ONE_PER_DRIVER String::NewSymbol("SQL_CP_ONE_PER_DRIVER")
#define ndbcSQL_CP_ONE_PER_HENV String::NewSymbol("SQL_CP_ONE_PER_HENV")
#define ndbcSQL_CP_RELAXED_MATCH String::NewSymbol("SQL_CP_RELAXED_MATCH")
#define ndbcSQL_CP_STRICT_MATCH String::NewSymbol("SQL_CP_STRICT_MATCH")
#define ndbcSQL_CREATE_ASSERTION String::NewSymbol("SQL_CREATE_ASSERTION")
#define ndbcSQL_CREATE_CHARACTER_SET String::NewSymbol("SQL_CREATE_CHARACTER_SET")
#define ndbcSQL_CREATE_COLLATION String::NewSymbol("SQL_CREATE_COLLATION")
#define ndbcSQL_CREATE_DOMAIN String::NewSymbol("SQL_CREATE_DOMAIN")
#define ndbcSQL_CREATE_SCHEMA String::NewSymbol("SQL_CREATE_SCHEMA")
#define ndbcSQL_CREATE_TABLE String::NewSymbol("SQL_CREATE_TABLE")
#define ndbcSQL_CREATE_TRANSLATION String::NewSymbol("SQL_CREATE_TRANSLATION")
#define ndbcSQL_CREATE_VIEW String::NewSymbol("SQL_CREATE_VIEW")
#define ndbcSQL_CS_AUTHORIZATION String::NewSymbol("SQL_CS_AUTHORIZATION")
#define ndbcSQL_CS_CREATE_SCHEMA String::NewSymbol("SQL_CS_CREATE_SCHEMA")
#define ndbcSQL_CS_DEFAULT_CHARACTER_SET String::NewSymbol("SQL_CS_DEFAULT_CHARACTER_SET")
#define ndbcSQL_CT_COLUMN_COLLATION String::NewSymbol("SQL_CT_COLUMN_COLLATION")
#define ndbcSQL_CT_COLUMN_CONSTRAINT String::NewSymbol("SQL_CT_COLUMN_CONSTRAINT")
#define ndbcSQL_CT_COLUMN_DEFAULT String::NewSymbol("SQL_CT_COLUMN_DEFAULT")
#define ndbcSQL_CT_COMMIT_DELETE String::NewSymbol("SQL_CT_COMMIT_DELETE")
#define ndbcSQL_CT_COMMIT_PRESERVE String::NewSymbol("SQL_CT_COMMIT_PRESERVE")
#define ndbcSQL_CT_CONSTRAINT_DEFERRABLE String::NewSymbol("SQL_CT_CONSTRAINT_DEFERRABLE")
#define ndbcSQL_CT_CONSTRAINT_INITIALLY_DEFERRED String::NewSymbol("SQL_CT_CONSTRAINT_INITIALLY_DEFERRED")
#define ndbcSQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE String::NewSymbol("SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE")
#define ndbcSQL_CT_CONSTRAINT_NAME_DEFINITION String::NewSymbol("SQL_CT_CONSTRAINT_NAME_DEFINITION")
#define ndbcSQL_CT_CONSTRAINT_NON_DEFERRABLE String::NewSymbol("SQL_CT_CONSTRAINT_NON_DEFERRABLE")
#define ndbcSQL_CT_CREATE_TABLE String::NewSymbol("SQL_CT_CREATE_TABLE")
#define ndbcSQL_CT_GLOBAL_TEMPORARY String::NewSymbol("SQL_CT_GLOBAL_TEMPORARY")
#define ndbcSQL_CT_LOCAL_TEMPORARY String::NewSymbol("SQL_CT_LOCAL_TEMPORARY")
#define ndbcSQL_CT_TABLE_CONSTRAINT String::NewSymbol("SQL_CT_TABLE_CONSTRAINT")
#define ndbcSQL_CTR_CREATE_TRANSLATION String::NewSymbol("SQL_CTR_CREATE_TRANSLATION")
#define ndbcSQL_CU_CATALOGS_NOT_SUPPORTED String::NewSymbol("SQL_CU_CATALOGS_NOT_SUPPORTED")
#define ndbcSQL_CU_DML_STATEMENTS String::NewSymbol("SQL_CU_DML_STATEMENTS")
#define ndbcSQL_CU_INDEX_DEFINITION String::NewSymbol("SQL_CU_INDEX_DEFINITION")
#define ndbcSQL_CU_PRIVILEGE_DEFINITION String::NewSymbol("SQL_CU_PRIVILEGE_DEFINITION")
#define ndbcSQL_CU_PROCEDURE_INVOCATION String::NewSymbol("SQL_CU_PROCEDURE_INVOCATION")
#define ndbcSQL_CU_TABLE_DEFINITION String::NewSymbol("SQL_CU_TABLE_DEFINITION")
#define ndbcSQL_CUR_USE_DRIVER String::NewSymbol("SQL_CUR_USE_DRIVER")
#define ndbcSQL_CUR_USE_IF_NEEDED String::NewSymbol("SQL_CUR_USE_IF_NEEDED")
#define ndbcSQL_CUR_USE_ODBC String::NewSymbol("SQL_CUR_USE_ODBC")
#define ndbcSQL_CURSOR_COMMIT_BEHAVIOR String::NewSymbol("SQL_CURSOR_COMMIT_BEHAVIOR")
#define ndbcSQL_CURSOR_DYNAMIC String::NewSymbol("SQL_CURSOR_DYNAMIC")
#define ndbcSQL_CURSOR_FORWARD_ONLY String::NewSymbol("SQL_CURSOR_FORWARD_ONLY")
#define ndbcSQL_CURSOR_KEYSET_DRIVEN String::NewSymbol("SQL_CURSOR_KEYSET_DRIVEN")
#define ndbcSQL_CURSOR_ROLLBACK_BEHAVIOR String::NewSymbol("SQL_CURSOR_ROLLBACK_BEHAVIOR")
#define ndbcSQL_CURSOR_SENSITIVITY String::NewSymbol("SQL_CURSOR_SENSITIVITY")
#define ndbcSQL_CURSOR_STATIC String::NewSymbol("SQL_CURSOR_STATIC")
#define ndbcSQL_CV_CASCADED String::NewSymbol("SQL_CV_CASCADED")
#define ndbcSQL_CV_CHECK_OPTION String::NewSymbol("SQL_CV_CHECK_OPTION")
#define ndbcSQL_CV_CREATE_VIEW String::NewSymbol("SQL_CV_CREATE_VIEW")
#define ndbcSQL_CV_LOCAL String::NewSymbol("SQL_CV_LOCAL")
#define ndbcSQL_CVT_BIGINT String::NewSymbol("SQL_CVT_BIGINT")
#define ndbcSQL_CVT_BINARY String::NewSymbol("SQL_CVT_BINARY")
#define ndbcSQL_CVT_BIT String::NewSymbol("SQL_CVT_BIT")
#define ndbcSQL_CVT_CHAR String::NewSymbol("SQL_CVT_CHAR")
#define ndbcSQL_CVT_DATE String::NewSymbol("SQL_CVT_DATE")
#define ndbcSQL_CVT_DECIMAL String::NewSymbol("SQL_CVT_DECIMAL")
#define ndbcSQL_CVT_DOUBLE String::NewSymbol("SQL_CVT_DOUBLE")
#define ndbcSQL_CVT_FLOAT String::NewSymbol("SQL_CVT_FLOAT")
#define ndbcSQL_CVT_GUID String::NewSymbol("SQL_CVT_GUID")
#define ndbcSQL_CVT_INTEGER String::NewSymbol("SQL_CVT_INTEGER")
#define ndbcSQL_CVT_INTERVAL_DAY_TIME String::NewSymbol("SQL_CVT_INTERVAL_DAY_TIME")
#define ndbcSQL_CVT_INTERVAL_YEAR_MONTH String::NewSymbol("SQL_CVT_INTERVAL_YEAR_MONTH")
#define ndbcSQL_CVT_LONGVARBINARY String::NewSymbol("SQL_CVT_LONGVARBINARY")
#define ndbcSQL_CVT_LONGVARCHAR String::NewSymbol("SQL_CVT_LONGVARCHAR")
#define ndbcSQL_CVT_NUMERIC String::NewSymbol("SQL_CVT_NUMERIC")
#define ndbcSQL_CVT_REAL_ODBC String::NewSymbol("SQL_CVT_REAL_ODBC")
#define ndbcSQL_CVT_SMALLINT String::NewSymbol("SQL_CVT_SMALLINT")
#define ndbcSQL_CVT_TIME String::NewSymbol("SQL_CVT_TIME")
#define ndbcSQL_CVT_TIMESTAMP String::NewSymbol("SQL_CVT_TIMESTAMP")
#define ndbcSQL_CVT_TINYINT String::NewSymbol("SQL_CVT_TINYINT")
#define ndbcSQL_CVT_VARBINARY String::NewSymbol("SQL_CVT_VARBINARY")
#define ndbcSQL_CVT_VARCHAR String::NewSymbol("SQL_CVT_VARCHAR")
#define ndbcSQL_DA_DROP_ASSERTION String::NewSymbol("SQL_DA_DROP_ASSERTION")
#define ndbcSQL_DATA_SOURCE_NAME String::NewSymbol("SQL_DATA_SOURCE_NAME")
#define ndbcSQL_DATA_SOURCE_READ_ONLY String::NewSymbol("SQL_DATA_SOURCE_READ_ONLY")
#define ndbcSQL_DATABASE_NAME String::NewSymbol("SQL_DATABASE_NAME")
#define ndbcSQL_DATETIME_LITERALS String::NewSymbol("SQL_DATETIME_LITERALS")
#define ndbcSQL_DBMS_NAME String::NewSymbol("SQL_DBMS_NAME")
#define ndbcSQL_DBMS_VER String::NewSymbol("SQL_DBMS_VER")
#define ndbcSQL_DC_DROP_COLLATION String::NewSymbol("SQL_DC_DROP_COLLATION")
#define ndbcSQL_DCS_DROP_CHARACTER_SET String::NewSymbol("SQL_DCS_DROP_CHARACTER_SET")
#define ndbcSQL_DD_CASCADE String::NewSymbol("SQL_DD_CASCADE")
#define ndbcSQL_DD_DROP_DOMAIN String::NewSymbol("SQL_DD_DROP_DOMAIN")
#define ndbcSQL_DD_RESTRICT String::NewSymbol("SQL_DD_RESTRICT")
#define ndbcSQL_DDL_INDEX String::NewSymbol("SQL_DDL_INDEX")
#define ndbcSQL_DEFAULT_TXN_ISOLATION String::NewSymbol("SQL_DEFAULT_TXN_ISOLATION")
#define ndbcSQL_DESCRIBE_PARAMETER String::NewSymbol("SQL_DESCRIBE_PARAMETER")
#define ndbcSQL_DI_CREATE_INDEX String::NewSymbol("SQL_DI_CREATE_INDEX")
#define ndbcSQL_DI_DROP_INDEX String::NewSymbol("SQL_DI_DROP_INDEX")
#define ndbcSQL_DL_SQL92_DATE String::NewSymbol("SQL_DL_SQL92_DATE")
#define ndbcSQL_DL_SQL92_INTERVAL_DAY String::NewSymbol("SQL_DL_SQL92_INTERVAL_DAY")
#define ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_HOUR String::NewSymbol("SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR")
#define ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE String::NewSymbol("SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE")
#define ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_SECOND String::NewSymbol("SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND")
#define ndbcSQL_DL_SQL92_INTERVAL_HOUR String::NewSymbol("SQL_DL_SQL92_INTERVAL_HOUR")
#define ndbcSQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE String::NewSymbol("SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE")
#define ndbcSQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND String::NewSymbol("SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND")
#define ndbcSQL_DL_SQL92_INTERVAL_MINUTE String::NewSymbol("SQL_DL_SQL92_INTERVAL_MINUTE")
#define ndbcSQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND String::NewSymbol("SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND")
#define ndbcSQL_DL_SQL92_INTERVAL_MONTH String::NewSymbol("SQL_DL_SQL92_INTERVAL_MONTH")
#define ndbcSQL_DL_SQL92_INTERVAL_SECOND String::NewSymbol("SQL_DL_SQL92_INTERVAL_SECOND")
#define ndbcSQL_DL_SQL92_INTERVAL_YEAR String::NewSymbol("SQL_DL_SQL92_INTERVAL_YEAR")
#define ndbcSQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH String::NewSymbol("SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH")
#define ndbcSQL_DL_SQL92_TIME String::NewSymbol("SQL_DL_SQL92_TIME")
#define ndbcSQL_DL_SQL92_TIMESTAMP String::NewSymbol("SQL_DL_SQL92_TIMESTAMP")
#define ndbcSQL_DM_VER String::NewSymbol("SQL_DM_VER")
#define ndbcSQL_DRIVER_HDBC String::NewSymbol("SQL_DRIVER_HDBC")
#define ndbcSQL_DRIVER_HDESC String::NewSymbol("SQL_DRIVER_HDESC")
#define ndbcSQL_DRIVER_HENV String::NewSymbol("SQL_DRIVER_HENV")
#define ndbcSQL_DRIVER_HLIB String::NewSymbol("SQL_DRIVER_HLIB")
#define ndbcSQL_DRIVER_HSTMT String::NewSymbol("SQL_DRIVER_HSTMT")
#define ndbcSQL_DRIVER_NAME String::NewSymbol("SQL_DRIVER_NAME")
#define ndbcSQL_DRIVER_ODBC_VER String::NewSymbol("SQL_DRIVER_ODBC_VER")
#define ndbcSQL_DRIVER_VER String::NewSymbol("SQL_DRIVER_VER")
#define ndbcSQL_DROP_ASSERTION String::NewSymbol("SQL_DROP_ASSERTION")
#define ndbcSQL_DROP_CHARACTER_SET String::NewSymbol("SQL_DROP_CHARACTER_SET")
#define ndbcSQL_DROP_COLLATION String::NewSymbol("SQL_DROP_COLLATION")
#define ndbcSQL_DROP_DOMAIN String::NewSymbol("SQL_DROP_DOMAIN")
#define ndbcSQL_DROP_SCHEMA String::NewSymbol("SQL_DROP_SCHEMA")
#define ndbcSQL_DROP_TABLE String::NewSymbol("SQL_DROP_TABLE")
#define ndbcSQL_DROP_TRANSLATION String::NewSymbol("SQL_DROP_TRANSLATION")
#define ndbcSQL_DROP_VIEW String::NewSymbol("SQL_DROP_VIEW")
#define ndbcSQL_DS_CASCADE String::NewSymbol("SQL_DS_CASCADE")
#define ndbcSQL_DS_DROP_SCHEMA String::NewSymbol("SQL_DS_DROP_SCHEMA")
#define ndbcSQL_DS_RESTRICT String::NewSymbol("SQL_DS_RESTRICT")
#define ndbcSQL_DT_CASCADE String::NewSymbol("SQL_DT_CASCADE")
#define ndbcSQL_DT_DROP_TABLE String::NewSymbol("SQL_DT_DROP_TABLE")
#define ndbcSQL_DT_RESTRICT String::NewSymbol("SQL_DT_RESTRICT")
#define ndbcSQL_DTC_DONE String::NewSymbol("SQL_DTC_DONE")
#define ndbcSQL_DTR_DROP_TRANSLATION String::NewSymbol("SQL_DTR_DROP_TRANSLATION")
#define ndbcSQL_DV_CASCADE String::NewSymbol("SQL_DV_CASCADE")
#define ndbcSQL_DV_DROP_VIEW String::NewSymbol("SQL_DV_DROP_VIEW")
#define ndbcSQL_DV_RESTRICT String::NewSymbol("SQL_DV_RESTRICT")
#define ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES1 String::NewSymbol("SQL_DYNAMIC_CURSOR_ATTRIBUTES1")
#define ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES2 String::NewSymbol("SQL_DYNAMIC_CURSOR_ATTRIBUTES2")
#define ndbcSQL_ERROR String::NewSymbol("SQL_ERROR")
#define ndbcSQL_EXPRESSIONS_IN_ORDERBY String::NewSymbol("SQL_EXPRESSIONS_IN_ORDERBY")
#define ndbcSQL_FALSE String::NewSymbol("SQL_FALSE")
#define ndbcSQL_FILE_CATALOG String::NewSymbol("SQL_FILE_CATALOG")
#define ndbcSQL_FILE_NOT_SUPPORTED String::NewSymbol("SQL_FILE_NOT_SUPPORTED")
#define ndbcSQL_FILE_TABLE String::NewSymbol("SQL_FILE_TABLE")
#define ndbcSQL_FILE_USAGE String::NewSymbol("SQL_FILE_USAGE")
#define ndbcSQL_FN_CVT_CAST String::NewSymbol("SQL_FN_CVT_CAST")
#define ndbcSQL_FN_CVT_CONVERT String::NewSymbol("SQL_FN_CVT_CONVERT")
#define ndbcSQL_FN_NUM_ABS String::NewSymbol("SQL_FN_NUM_ABS")
#define ndbcSQL_FN_NUM_ACOS String::NewSymbol("SQL_FN_NUM_ACOS")
#define ndbcSQL_FN_NUM_ASIN String::NewSymbol("SQL_FN_NUM_ASIN")
#define ndbcSQL_FN_NUM_ATAN String::NewSymbol("SQL_FN_NUM_ATAN")
#define ndbcSQL_FN_NUM_ATAN2 String::NewSymbol("SQL_FN_NUM_ATAN2")
#define ndbcSQL_FN_NUM_CEILING String::NewSymbol("SQL_FN_NUM_CEILING")
#define ndbcSQL_FN_NUM_COS String::NewSymbol("SQL_FN_NUM_COS")
#define ndbcSQL_FN_NUM_COT String::NewSymbol("SQL_FN_NUM_COT")
#define ndbcSQL_FN_NUM_DEGREES String::NewSymbol("SQL_FN_NUM_DEGREES")
#define ndbcSQL_FN_NUM_EXP String::NewSymbol("SQL_FN_NUM_EXP")
#define ndbcSQL_FN_NUM_FLOOR String::NewSymbol("SQL_FN_NUM_FLOOR")
#define ndbcSQL_FN_NUM_LOG String::NewSymbol("SQL_FN_NUM_LOG")
#define ndbcSQL_FN_NUM_LOG10 String::NewSymbol("SQL_FN_NUM_LOG10")
#define ndbcSQL_FN_NUM_MOD String::NewSymbol("SQL_FN_NUM_MOD")
#define ndbcSQL_FN_NUM_PI String::NewSymbol("SQL_FN_NUM_PI")
#define ndbcSQL_FN_NUM_POWER String::NewSymbol("SQL_FN_NUM_POWER")
#define ndbcSQL_FN_NUM_RADIANS String::NewSymbol("SQL_FN_NUM_RADIANS")
#define ndbcSQL_FN_NUM_RAND String::NewSymbol("SQL_FN_NUM_RAND")
#define ndbcSQL_FN_NUM_ROUND String::NewSymbol("SQL_FN_NUM_ROUND")
#define ndbcSQL_FN_NUM_SIGN String::NewSymbol("SQL_FN_NUM_SIGN")
#define ndbcSQL_FN_NUM_SIN String::NewSymbol("SQL_FN_NUM_SIN")
#define ndbcSQL_FN_NUM_SQRT String::NewSymbol("SQL_FN_NUM_SQRT")
#define ndbcSQL_FN_NUM_TAN String::NewSymbol("SQL_FN_NUM_TAN")
#define ndbcSQL_FN_NUM_TRUNCATE String::NewSymbol("SQL_FN_NUM_TRUNCATE")
#define ndbcSQL_FN_STR_ASCII String::NewSymbol("SQL_FN_STR_ASCII")
#define ndbcSQL_FN_STR_BIT_LENGTH String::NewSymbol("SQL_FN_STR_BIT_LENGTH")
#define ndbcSQL_FN_STR_CHAR String::NewSymbol("SQL_FN_STR_CHAR")
#define ndbcSQL_FN_STR_CHAR_LENGTH String::NewSymbol("SQL_FN_STR_CHAR_LENGTH")
#define ndbcSQL_FN_STR_CHARACTER_LENGTH String::NewSymbol("SQL_FN_STR_CHARACTER_LENGTH")
#define ndbcSQL_FN_STR_CONCAT String::NewSymbol("SQL_FN_STR_CONCAT")
#define ndbcSQL_FN_STR_DIFFERENCE String::NewSymbol("SQL_FN_STR_DIFFERENCE")
#define ndbcSQL_FN_STR_INSERT String::NewSymbol("SQL_FN_STR_INSERT")
#define ndbcSQL_FN_STR_LCASE String::NewSymbol("SQL_FN_STR_LCASE")
#define ndbcSQL_FN_STR_LEFT String::NewSymbol("SQL_FN_STR_LEFT")
#define ndbcSQL_FN_STR_LENGTH String::NewSymbol("SQL_FN_STR_LENGTH")
#define ndbcSQL_FN_STR_LOCATE String::NewSymbol("SQL_FN_STR_LOCATE")
#define ndbcSQL_FN_STR_LOCATE_2 String::NewSymbol("SQL_FN_STR_LOCATE_2")
#define ndbcSQL_FN_STR_LTRIM String::NewSymbol("SQL_FN_STR_LTRIM")
#define ndbcSQL_FN_STR_OCTET_LENGTH String::NewSymbol("SQL_FN_STR_OCTET_LENGTH")
#define ndbcSQL_FN_STR_POSITION String::NewSymbol("SQL_FN_STR_POSITION")
#define ndbcSQL_FN_STR_REPEAT String::NewSymbol("SQL_FN_STR_REPEAT")
#define ndbcSQL_FN_STR_REPLACE String::NewSymbol("SQL_FN_STR_REPLACE")
#define ndbcSQL_FN_STR_RIGHT String::NewSymbol("SQL_FN_STR_RIGHT")
#define ndbcSQL_FN_STR_RTRIM String::NewSymbol("SQL_FN_STR_RTRIM")
#define ndbcSQL_FN_STR_SOUNDEX String::NewSymbol("SQL_FN_STR_SOUNDEX")
#define ndbcSQL_FN_STR_SPACE String::NewSymbol("SQL_FN_STR_SPACE")
#define ndbcSQL_FN_STR_SUBSTRING String::NewSymbol("SQL_FN_STR_SUBSTRING")
#define ndbcSQL_FN_STR_UCASE String::NewSymbol("SQL_FN_STR_UCASE")
#define ndbcSQL_FN_SYS_DBNAME String::NewSymbol("SQL_FN_SYS_DBNAME")
#define ndbcSQL_FN_SYS_IFNULL String::NewSymbol("SQL_FN_SYS_IFNULL")
#define ndbcSQL_FN_SYS_USERNAME String::NewSymbol("SQL_FN_SYS_USERNAME")
#define ndbcSQL_FN_TD_CURDATE String::NewSymbol("SQL_FN_TD_CURDATE")
#define ndbcSQL_FN_TD_CURRENT_DATE String::NewSymbol("SQL_FN_TD_CURRENT_DATE")
#define ndbcSQL_FN_TD_CURRENT_TIME String::NewSymbol("SQL_FN_TD_CURRENT_TIME")
#define ndbcSQL_FN_TD_CURRENT_TIMESTAMP String::NewSymbol("SQL_FN_TD_CURRENT_TIMESTAMP")
#define ndbcSQL_FN_TD_CURTIME String::NewSymbol("SQL_FN_TD_CURTIME")
#define ndbcSQL_FN_TD_DAYNAME String::NewSymbol("SQL_FN_TD_DAYNAME")
#define ndbcSQL_FN_TD_DAYOFMONTH String::NewSymbol("SQL_FN_TD_DAYOFMONTH")
#define ndbcSQL_FN_TD_DAYOFWEEK String::NewSymbol("SQL_FN_TD_DAYOFWEEK")
#define ndbcSQL_FN_TD_DAYOFYEAR String::NewSymbol("SQL_FN_TD_DAYOFYEAR")
#define ndbcSQL_FN_TD_EXTRACT String::NewSymbol("SQL_FN_TD_EXTRACT")
#define ndbcSQL_FN_TD_HOUR String::NewSymbol("SQL_FN_TD_HOUR")
#define ndbcSQL_FN_TD_MINUTE String::NewSymbol("SQL_FN_TD_MINUTE")
#define ndbcSQL_FN_TD_MONTH String::NewSymbol("SQL_FN_TD_MONTH")
#define ndbcSQL_FN_TD_MONTHNAME String::NewSymbol("SQL_FN_TD_MONTHNAME")
#define ndbcSQL_FN_TD_NOW String::NewSymbol("SQL_FN_TD_NOW")
#define ndbcSQL_FN_TD_QUARTER String::NewSymbol("SQL_FN_TD_QUARTER")
#define ndbcSQL_FN_TD_SECOND String::NewSymbol("SQL_FN_TD_SECOND")
#define ndbcSQL_FN_TD_TIMESTAMPADD String::NewSymbol("SQL_FN_TD_TIMESTAMPADD")
#define ndbcSQL_FN_TD_TIMESTAMPDIFF String::NewSymbol("SQL_FN_TD_TIMESTAMPDIFF")
#define ndbcSQL_FN_TD_WEEK String::NewSymbol("SQL_FN_TD_WEEK")
#define ndbcSQL_FN_TD_YEAR String::NewSymbol("SQL_FN_TD_YEAR")
#define ndbcSQL_FN_TSI_DAY String::NewSymbol("SQL_FN_TSI_DAY")
#define ndbcSQL_FN_TSI_FRAC_SECOND String::NewSymbol("SQL_FN_TSI_FRAC_SECOND")
#define ndbcSQL_FN_TSI_HOUR String::NewSymbol("SQL_FN_TSI_HOUR")
#define ndbcSQL_FN_TSI_MINUTE String::NewSymbol("SQL_FN_TSI_MINUTE")
#define ndbcSQL_FN_TSI_MONTH String::NewSymbol("SQL_FN_TSI_MONTH")
#define ndbcSQL_FN_TSI_QUARTER String::NewSymbol("SQL_FN_TSI_QUARTER")
#define ndbcSQL_FN_TSI_SECOND String::NewSymbol("SQL_FN_TSI_SECOND")
#define ndbcSQL_FN_TSI_WEEK String::NewSymbol("SQL_FN_TSI_WEEK")
#define ndbcSQL_FN_TSI_YEAR String::NewSymbol("SQL_FN_TSI_YEAR")
#define ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1 String::NewSymbol("SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1")
#define ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2 String::NewSymbol("SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2")
#define ndbcSQL_GB_COLLATE String::NewSymbol("SQL_GB_COLLATE")
#define ndbcSQL_GB_GROUP_BY_CONTAINS_SELECT String::NewSymbol("SQL_GB_GROUP_BY_CONTAINS_SELECT")
#define ndbcSQL_GB_GROUP_BY_EQUALS_SELECT String::NewSymbol("SQL_GB_GROUP_BY_EQUALS_SELECT")
#define ndbcSQL_GB_NO_RELATION String::NewSymbol("SQL_GB_NO_RELATION")
#define ndbcSQL_GB_NOT_SUPPORTED String::NewSymbol("SQL_GB_NOT_SUPPORTED")
#define ndbcSQL_GD_ANY_COLUMN String::NewSymbol("SQL_GD_ANY_COLUMN")
#define ndbcSQL_GD_ANY_ORDER String::NewSymbol("SQL_GD_ANY_ORDER")
#define ndbcSQL_GD_BLOCK String::NewSymbol("SQL_GD_BLOCK")
#define ndbcSQL_GD_BOUND String::NewSymbol("SQL_GD_BOUND")
#define ndbcSQL_GD_OUTPUT_PARAMS String::NewSymbol("SQL_GD_OUTPUT_PARAMS")
#define ndbcSQL_GETDATA_EXTENSIONS String::NewSymbol("SQL_GETDATA_EXTENSIONS")
#define ndbcSQL_GROUP_BY String::NewSymbol("SQL_GROUP_BY")
#define ndbcSQL_HANDLE_DBC String::NewSymbol("SQL_HANDLE_DBC")
#define ndbcSQL_HANDLE_DESC String::NewSymbol("SQL_HANDLE_DESC")
#define ndbcSQL_HANDLE_ENV String::NewSymbol("SQL_HANDLE_ENV")
#define ndbcSQL_HANDLE_STMT String::NewSymbol("SQL_HANDLE_STMT")
#define ndbcSQL_IC_LOWER String::NewSymbol("SQL_IC_LOWER")
#define ndbcSQL_IC_MIXED String::NewSymbol("SQL_IC_MIXED")
#define ndbcSQL_IC_SENSITIVE String::NewSymbol("SQL_IC_SENSITIVE")
#define ndbcSQL_IC_UPPER String::NewSymbol("SQL_IC_UPPER")
#define ndbcSQL_IDENTIFIER_CASE String::NewSymbol("SQL_IDENTIFIER_CASE")
#define ndbcSQL_IDENTIFIER_QUOTE_CHAR String::NewSymbol("SQL_IDENTIFIER_QUOTE_CHAR")
#define ndbcSQL_IK_ALL String::NewSymbol("SQL_IK_ALL")
#define ndbcSQL_IK_ASC String::NewSymbol("SQL_IK_ASC")
#define ndbcSQL_IK_DESC String::NewSymbol("SQL_IK_DESC")
#define ndbcSQL_IK_NONE String::NewSymbol("SQL_IK_NONE")
#define ndbcSQL_INDEX_KEYWORDS String::NewSymbol("SQL_INDEX_KEYWORDS")
#define ndbcSQL_INFO_SCHEMA_VIEWS String::NewSymbol("SQL_INFO_SCHEMA_VIEWS")
#define ndbcSQL_INSENSITIVE String::NewSymbol("SQL_INSENSITIVE")
#define ndbcSQL_INSERT_STATEMENT String::NewSymbol("SQL_INSERT_STATEMENT")
#define ndbcSQL_INTEGRITY String::NewSymbol("SQL_INTEGRITY")
#define ndbcSQL_INVALID_HANDLE String::NewSymbol("SQL_INVALID_HANDLE")
#define ndbcSQL_IS_INSERT_LITERALS String::NewSymbol("SQL_IS_INSERT_LITERALS")
#define ndbcSQL_IS_INSERT_SEARCHED String::NewSymbol("SQL_IS_INSERT_SEARCHED")
#define ndbcSQL_IS_SELECT_INTO String::NewSymbol("SQL_IS_SELECT_INTO")
#define ndbcSQL_ISV_ASSERTIONS String::NewSymbol("SQL_ISV_ASSERTIONS")
#define ndbcSQL_ISV_CHARACTER_SETS String::NewSymbol("SQL_ISV_CHARACTER_SETS")
#define ndbcSQL_ISV_CHECK_CONSTRAINTS String::NewSymbol("SQL_ISV_CHECK_CONSTRAINTS")
#define ndbcSQL_ISV_COLLATIONS String::NewSymbol("SQL_ISV_COLLATIONS")
#define ndbcSQL_ISV_COLUMN_DOMAIN_USAGE String::NewSymbol("SQL_ISV_COLUMN_DOMAIN_USAGE")
#define ndbcSQL_ISV_COLUMN_PRIVILEGES String::NewSymbol("SQL_ISV_COLUMN_PRIVILEGES")
#define ndbcSQL_ISV_COLUMNS String::NewSymbol("SQL_ISV_COLUMNS")
#define ndbcSQL_ISV_CONSTRAINT_COLUMN_USAGE String::NewSymbol("SQL_ISV_CONSTRAINT_COLUMN_USAGE")
#define ndbcSQL_ISV_CONSTRAINT_TABLE_USAGE String::NewSymbol("SQL_ISV_CONSTRAINT_TABLE_USAGE")
#define ndbcSQL_ISV_DOMAIN_CONSTRAINTS String::NewSymbol("SQL_ISV_DOMAIN_CONSTRAINTS")
#define ndbcSQL_ISV_DOMAINS String::NewSymbol("SQL_ISV_DOMAINS")
#define ndbcSQL_ISV_KEY_COLUMN_USAGE String::NewSymbol("SQL_ISV_KEY_COLUMN_USAGE")
#define ndbcSQL_ISV_REFERENTIAL_CONSTRAINTS String::NewSymbol("SQL_ISV_REFERENTIAL_CONSTRAINTS")
#define ndbcSQL_ISV_SCHEMATA String::NewSymbol("SQL_ISV_SCHEMATA")
#define ndbcSQL_ISV_SQL_LANGUAGES String::NewSymbol("SQL_ISV_SQL_LANGUAGES")
#define ndbcSQL_ISV_TABLE_CONSTRAINTS String::NewSymbol("SQL_ISV_TABLE_CONSTRAINTS")
#define ndbcSQL_ISV_TABLE_PRIVILEGES String::NewSymbol("SQL_ISV_TABLE_PRIVILEGES")
#define ndbcSQL_ISV_TABLES String::NewSymbol("SQL_ISV_TABLES")
#define ndbcSQL_ISV_TRANSLATIONS String::NewSymbol("SQL_ISV_TRANSLATIONS")
#define ndbcSQL_ISV_USAGE_PRIVILEGES String::NewSymbol("SQL_ISV_USAGE_PRIVILEGES")
#define ndbcSQL_ISV_VIEW_COLUMN_USAGE String::NewSymbol("SQL_ISV_VIEW_COLUMN_USAGE")
#define ndbcSQL_ISV_VIEW_TABLE_USAGE String::NewSymbol("SQL_ISV_VIEW_TABLE_USAGE")
#define ndbcSQL_ISV_VIEWS String::NewSymbol("SQL_ISV_VIEWS")
#define ndbcSQL_KEYSET_CURSOR_ATTRIBUTES1 String::NewSymbol("SQL_KEYSET_CURSOR_ATTRIBUTES1")
#define ndbcSQL_KEYSET_CURSOR_ATTRIBUTES2 String::NewSymbol("SQL_KEYSET_CURSOR_ATTRIBUTES2")
#define ndbcSQL_KEYWORDS String::NewSymbol("SQL_KEYWORDS")
#define ndbcSQL_LIKE_ESCAPE_CLAUSE String::NewSymbol("SQL_LIKE_ESCAPE_CLAUSE")
#define ndbcSQL_MAX_ASYNC_CONCURRENT_STATEMENTS String::NewSymbol("SQL_MAX_ASYNC_CONCURRENT_STATEMENTS")
#define ndbcSQL_MAX_BINARY_LITERAL_LEN String::NewSymbol("SQL_MAX_BINARY_LITERAL_LEN")
#define ndbcSQL_MAX_CATALOG_NAME_LEN String::NewSymbol("SQL_MAX_CATALOG_NAME_LEN")
#define ndbcSQL_MAX_CHAR_LITERAL_LEN String::NewSymbol("SQL_MAX_CHAR_LITERAL_LEN")
#define ndbcSQL_MAX_COLUMN_NAME_LEN String::NewSymbol("SQL_MAX_COLUMN_NAME_LEN")
#define ndbcSQL_MAX_COLUMNS_IN_GROUP_BY String::NewSymbol("SQL_MAX_COLUMNS_IN_GROUP_BY")
#define ndbcSQL_MAX_COLUMNS_IN_INDEX String::NewSymbol("SQL_MAX_COLUMNS_IN_INDEX")
#define ndbcSQL_MAX_COLUMNS_IN_ORDER_BY String::NewSymbol("SQL_MAX_COLUMNS_IN_ORDER_BY")
#define ndbcSQL_MAX_COLUMNS_IN_SELECT String::NewSymbol("SQL_MAX_COLUMNS_IN_SELECT")
#define ndbcSQL_MAX_COLUMNS_IN_TABLE String::NewSymbol("SQL_MAX_COLUMNS_IN_TABLE")
#define ndbcSQL_MAX_CONCURRENT_ACTIVITIES String::NewSymbol("SQL_MAX_CONCURRENT_ACTIVITIES")
#define ndbcSQL_MAX_CURSOR_NAME_LEN String::NewSymbol("SQL_MAX_CURSOR_NAME_LEN")
#define ndbcSQL_MAX_DRIVER_CONNECTIONS String::NewSymbol("SQL_MAX_DRIVER_CONNECTIONS")
#define ndbcSQL_MAX_IDENTIFIER_LEN String::NewSymbol("SQL_MAX_IDENTIFIER_LEN")
#define ndbcSQL_MAX_INDEX_SIZE String::NewSymbol("SQL_MAX_INDEX_SIZE")
#define ndbcSQL_MAX_PROCEDURE_NAME_LEN String::NewSymbol("SQL_MAX_PROCEDURE_NAME_LEN")
#define ndbcSQL_MAX_ROW_SIZE String::NewSymbol("SQL_MAX_ROW_SIZE")
#define ndbcSQL_MAX_ROW_SIZE_INCLUDES_LONG String::NewSymbol("SQL_MAX_ROW_SIZE_INCLUDES_LONG")
#define ndbcSQL_MAX_SCHEMA_NAME_LEN String::NewSymbol("SQL_MAX_SCHEMA_NAME_LEN")
#define ndbcSQL_MAX_STATEMENT_LEN String::NewSymbol("SQL_MAX_STATEMENT_LEN")
#define ndbcSQL_MAX_TABLE_NAME_LEN String::NewSymbol("SQL_MAX_TABLE_NAME_LEN")
#define ndbcSQL_MAX_TABLES_IN_SELECT String::NewSymbol("SQL_MAX_TABLES_IN_SELECT")
#define ndbcSQL_MAX_USER_NAME_LEN String::NewSymbol("SQL_MAX_USER_NAME_LEN")
#define ndbcSQL_MODE_READ_ONLY String::NewSymbol("SQL_MODE_READ_ONLY")
#define ndbcSQL_MODE_READ_WRITE String::NewSymbol("SQL_MODE_READ_WRITE")
#define ndbcSQL_MULT_RESULT_SETS String::NewSymbol("SQL_MULT_RESULT_SETS")
#define ndbcSQL_MULTIPLE_ACTIVE_TXN String::NewSymbol("SQL_MULTIPLE_ACTIVE_TXN")
#define ndbcSQL_NC_END String::NewSymbol("SQL_NC_END")
#define ndbcSQL_NC_HIGH String::NewSymbol("SQL_NC_HIGH")
#define ndbcSQL_NC_LOW String::NewSymbol("SQL_NC_LOW")
#define ndbcSQL_NC_START String::NewSymbol("SQL_NC_START")
#define ndbcSQL_NEED_DATA String::NewSymbol("SQL_NEED_DATA")
#define ndbcSQL_NEED_LONG_DATA_LEN String::NewSymbol("SQL_NEED_LONG_DATA_LEN")
#define ndbcSQL_NNC_NON_NULL String::NewSymbol("SQL_NNC_NON_NULL")
#define ndbcSQL_NNC_NULL String::NewSymbol("SQL_NNC_NULL")
#define ndbcSQL_NO_DATA String::NewSymbol("SQL_NO_DATA")
#define ndbcSQL_NON_NULLABLE_COLUMNS String::NewSymbol("SQL_NON_NULLABLE_COLUMNS")
#define ndbcSQL_NONSCROLLABLE String::NewSymbol("SQL_NONSCROLLABLE")
#define ndbcSQL_NOSCAN_OFF String::NewSymbol("SQL_NOSCAN_OFF")
#define ndbcSQL_NOSCAN_ON String::NewSymbol("SQL_NOSCAN_ON")
#define ndbcSQL_NULL_COLLATION String::NewSymbol("SQL_NULL_COLLATION")
#define ndbcSQL_NUMERIC_FUNCTIONS String::NewSymbol("SQL_NUMERIC_FUNCTIONS")
#define ndbcSQL_ODBC_INTERFACE_CONFORMANCE String::NewSymbol("SQL_ODBC_INTERFACE_CONFORMANCE")
#define ndbcSQL_ODBC_VER String::NewSymbol("SQL_ODBC_VER")
#define ndbcSQL_OIC_CORE String::NewSymbol("SQL_OIC_CORE")
#define ndbcSQL_OIC_LEVEL1 String::NewSymbol("SQL_OIC_LEVEL1")
#define ndbcSQL_OIC_LEVEL2 String::NewSymbol("SQL_OIC_LEVEL2")
#define ndbcSQL_OJ_ALL_COMPARISON_OPS String::NewSymbol("SQL_OJ_ALL_COMPARISON_OPS")
#define ndbcSQL_OJ_CAPABILITIES String::NewSymbol("SQL_OJ_CAPABILITIES")
#define ndbcSQL_OJ_FULL String::NewSymbol("SQL_OJ_FULL")
#define ndbcSQL_OJ_INNTER String::NewSymbol("SQL_OJ_INNTER")
#define ndbcSQL_OJ_LEFT String::NewSymbol("SQL_OJ_LEFT")
#define ndbcSQL_OJ_NESTED String::NewSymbol("SQL_OJ_NESTED")
#define ndbcSQL_OJ_NOT_ORDERED String::NewSymbol("SQL_OJ_NOT_ORDERED")
#define ndbcSQL_OJ_RIGHT String::NewSymbol("SQL_OJ_RIGHT")
#define ndbcSQL_OPT_TRACE_OFF String::NewSymbol("SQL_OPT_TRACE_OFF")
#define ndbcSQL_OPT_TRACE_ON String::NewSymbol("SQL_OPT_TRACE_ON")
#define ndbcSQL_ORDER_BY_COLUMNS_IN_SELECT String::NewSymbol("SQL_ORDER_BY_COLUMNS_IN_SELECT")
#define ndbcSQL_OV_ODBC2 String::NewSymbol("SQL_OV_ODBC2")
#define ndbcSQL_OV_ODBC3 String::NewSymbol("SQL_OV_ODBC3")
#define ndbcSQL_OV_ODBC3_80 String::NewSymbol("SQL_OV_ODBC3_80")
#define ndbcSQL_PARAM_ARRAY_ROW_COUNTS String::NewSymbol("SQL_PARAM_ARRAY_ROW_COUNTS")
#define ndbcSQL_PARAM_ARRAY_SELECTS String::NewSymbol("SQL_PARAM_ARRAY_SELECTS")
#define ndbcSQL_PARAM_BIND_BY_COLUMN String::NewSymbol("SQL_PARAM_BIND_BY_COLUMN")
#define ndbcSQL_PARAM_DATA_AVAILABLE String::NewSymbol("SQL_PARAM_DATA_AVAILABLE")
#define ndbcSQL_PARAM_DIAG_UNAVAILABLE String::NewSymbol("SQL_PARAM_DIAG_UNAVAILABLE")
#define ndbcSQL_PARAM_ERROR String::NewSymbol("SQL_PARAM_ERROR")
#define ndbcSQL_PARAM_IGNORE String::NewSymbol("SQL_PARAM_IGNORE")
#define ndbcSQL_PARAM_PROCEED String::NewSymbol("SQL_PARAM_PROCEED")
#define ndbcSQL_PARAM_SUCCESS String::NewSymbol("SQL_PARAM_SUCCESS")
#define ndbcSQL_PARAM_SUCCESS_WITH_INFO String::NewSymbol("SQL_PARAM_SUCCESS_WITH_INFO")
#define ndbcSQL_PARAM_UNUSED String::NewSymbol("SQL_PARAM_UNUSED")
#define ndbcSQL_PARC_BATCH String::NewSymbol("SQL_PARC_BATCH")
#define ndbcSQL_PARC_NO_BATCH String::NewSymbol("SQL_PARC_NO_BATCH")
#define ndbcSQL_PAS_BATCH String::NewSymbol("SQL_PAS_BATCH")
#define ndbcSQL_PAS_NO_BATCH String::NewSymbol("SQL_PAS_NO_BATCH")
#define ndbcSQL_PAS_NO_SELECT String::NewSymbol("SQL_PAS_NO_SELECT")
#define ndbcSQL_POS_ADD String::NewSymbol("SQL_POS_ADD")
#define ndbcSQL_POS_DELETE String::NewSymbol("SQL_POS_DELETE")
#define ndbcSQL_POS_OPERATIONS String::NewSymbol("SQL_POS_OPERATIONS")
#define ndbcSQL_POS_POSITION String::NewSymbol("SQL_POS_POSITION")
#define ndbcSQL_POS_REFRESH String::NewSymbol("SQL_POS_REFRESH")
#define ndbcSQL_POS_UPDATE String::NewSymbol("SQL_POS_UPDATE")
#define ndbcSQL_PROCEDURE_TERM String::NewSymbol("SQL_PROCEDURE_TERM")
#define ndbcSQL_PROCEDURES String::NewSymbol("SQL_PROCEDURES")
#define ndbcSQL_QUOTED_IDENTIFIER_CASE String::NewSymbol("SQL_QUOTED_IDENTIFIER_CASE")
#define ndbcSQL_RD_OFF String::NewSymbol("SQL_RD_OFF")
#define ndbcSQL_RD_ON String::NewSymbol("SQL_RD_ON")
#define ndbcSQL_ROW_IGNORE String::NewSymbol("SQL_ROW_IGNORE")
#define ndbcSQL_ROW_PROCEED String::NewSymbol("SQL_ROW_PROCEED")
#define ndbcSQL_ROW_UPDATES String::NewSymbol("SQL_ROW_UPDATES")
#define ndbcSQL_SC_FIPS127_2_TRANSITIONAL String::NewSymbol("SQL_SC_FIPS127_2_TRANSITIONAL")
#define ndbcSQL_SC_NON_UNIQUE String::NewSymbol("SQL_SC_NON_UNIQUE")
#define ndbcSQL_SC_SQL92_ENTRY String::NewSymbol("SQL_SC_SQL92_ENTRY")
#define ndbcSQL_SC_SQL92_FULL String::NewSymbol("SQL_SC_SQL92_FULL")
#define ndbcSQL_SC_SQL92_INTERMEDIATE String::NewSymbol("SQL_SC_SQL92_INTERMEDIATE")
#define ndbcSQL_SC_TRY_UNIQUE String::NewSymbol("SQL_SC_TRY_UNIQUE")
#define ndbcSQL_SC_UNIQUE String::NewSymbol("SQL_SC_UNIQUE")
#define ndbcSQL_SCC_ISO92_CLI String::NewSymbol("SQL_SCC_ISO92_CLI")
#define ndbcSQL_SCC_XOPEN_CLI_VERSION1 String::NewSymbol("SQL_SCC_XOPEN_CLI_VERSION1")
#define ndbcSQL_SCHEMA_TERM String::NewSymbol("SQL_SCHEMA_TERM")
#define ndbcSQL_SCHEMA_USAGE String::NewSymbol("SQL_SCHEMA_USAGE")
#define ndbcSQL_SCROLL_OPTIONS String::NewSymbol("SQL_SCROLL_OPTIONS")
#define ndbcSQL_SCROLLABLE String::NewSymbol("SQL_SCROLLABLE")
#define ndbcSQL_SDF_CURRENT_DATE String::NewSymbol("SQL_SDF_CURRENT_DATE")
#define ndbcSQL_SDF_CURRENT_TIME String::NewSymbol("SQL_SDF_CURRENT_TIME")
#define ndbcSQL_SDF_CURRENT_TIMESTAMP String::NewSymbol("SQL_SDF_CURRENT_TIMESTAMP")
#define ndbcSQL_SEARCH_PATTERN_ESCAPE String::NewSymbol("SQL_SEARCH_PATTERN_ESCAPE")
#define ndbcSQL_SENSITIVE String::NewSymbol("SQL_SENSITIVE")
#define ndbcSQL_SERVER_NAME String::NewSymbol("SQL_SERVER_NAME")
#define ndbcSQL_SFKD_CASCADE String::NewSymbol("SQL_SFKD_CASCADE")
#define ndbcSQL_SFKD_NO_ACTION String::NewSymbol("SQL_SFKD_NO_ACTION")
#define ndbcSQL_SFKD_SET_DEFAULT String::NewSymbol("SQL_SFKD_SET_DEFAULT")
#define ndbcSQL_SFKD_SET_NULL String::NewSymbol("SQL_SFKD_SET_NULL")
#define ndbcSQL_SFKU_CASCADE String::NewSymbol("SQL_SFKU_CASCADE")
#define ndbcSQL_SFKU_NO_ACTION String::NewSymbol("SQL_SFKU_NO_ACTION")
#define ndbcSQL_SFKU_SET_DEFAULT String::NewSymbol("SQL_SFKU_SET_DEFAULT")
#define ndbcSQL_SFKU_SET_NULL String::NewSymbol("SQL_SFKU_SET_NULL")
#define ndbcSQL_SG_DELETE_TABLE String::NewSymbol("SQL_SG_DELETE_TABLE")
#define ndbcSQL_SG_INSERT_COLUMN String::NewSymbol("SQL_SG_INSERT_COLUMN")
#define ndbcSQL_SG_INSERT_TABLE String::NewSymbol("SQL_SG_INSERT_TABLE")
#define ndbcSQL_SG_REFERENCES_COLUMN String::NewSymbol("SQL_SG_REFERENCES_COLUMN")
#define ndbcSQL_SG_REFERENCES_TABLE String::NewSymbol("SQL_SG_REFERENCES_TABLE")
#define ndbcSQL_SG_SELECT_TABLE String::NewSymbol("SQL_SG_SELECT_TABLE")
#define ndbcSQL_SG_UPDATE_COLUMN String::NewSymbol("SQL_SG_UPDATE_COLUMN")
#define ndbcSQL_SG_UPDATE_TABLE String::NewSymbol("SQL_SG_UPDATE_TABLE")
#define ndbcSQL_SG_USAGE_ON_CHARACTER_SET String::NewSymbol("SQL_SG_USAGE_ON_CHARACTER_SET")
#define ndbcSQL_SG_USAGE_ON_COLLATION String::NewSymbol("SQL_SG_USAGE_ON_COLLATION")
#define ndbcSQL_SG_USAGE_ON_DOMAIN String::NewSymbol("SQL_SG_USAGE_ON_DOMAIN")
#define ndbcSQL_SG_USAGE_ON_TRANSLATION String::NewSymbol("SQL_SG_USAGE_ON_TRANSLATION")
#define ndbcSQL_SG_WITH_GRANT_OPTION String::NewSymbol("SQL_SG_WITH_GRANT_OPTION")
#define ndbcSQL_SNVF_BIT_LENGTH String::NewSymbol("SQL_SNVF_BIT_LENGTH")
#define ndbcSQL_SNVF_CHAR_LENGTH String::NewSymbol("SQL_SNVF_CHAR_LENGTH")
#define ndbcSQL_SNVF_CHARACTER_LENGTH String::NewSymbol("SQL_SNVF_CHARACTER_LENGTH")
#define ndbcSQL_SNVF_EXTRACT String::NewSymbol("SQL_SNVF_EXTRACT")
#define ndbcSQL_SNVF_OCTET_LENGTH String::NewSymbol("SQL_SNVF_OCTET_LENGTH")
#define ndbcSQL_SNVF_POSITION String::NewSymbol("SQL_SNVF_POSITION")
#define ndbcSQL_SO_DYNAMIC String::NewSymbol("SQL_SO_DYNAMIC")
#define ndbcSQL_SO_FORWARD_ONLY String::NewSymbol("SQL_SO_FORWARD_ONLY")
#define ndbcSQL_SO_KEYSET_DRIVEN String::NewSymbol("SQL_SO_KEYSET_DRIVEN")
#define ndbcSQL_SO_MIXED String::NewSymbol("SQL_SO_MIXED")
#define ndbcSQL_SO_STATIC String::NewSymbol("SQL_SO_STATIC")
#define ndbcSQL_SP_BETWEEN String::NewSymbol("SQL_SP_BETWEEN")
#define ndbcSQL_SP_COMPARISON String::NewSymbol("SQL_SP_COMPARISON")
#define ndbcSQL_SP_EXISTS String::NewSymbol("SQL_SP_EXISTS")
#define ndbcSQL_SP_IN String::NewSymbol("SQL_SP_IN")
#define ndbcSQL_SP_ISNOTNULL String::NewSymbol("SQL_SP_ISNOTNULL")
#define ndbcSQL_SP_ISNULL String::NewSymbol("SQL_SP_ISNULL")
#define ndbcSQL_SP_LIKE String::NewSymbol("SQL_SP_LIKE")
#define ndbcSQL_SP_MATCH_FULL String::NewSymbol("SQL_SP_MATCH_FULL")
#define ndbcSQL_SP_MATCH_PARTIAL String::NewSymbol("SQL_SP_MATCH_PARTIAL")
#define ndbcSQL_SP_MATCH_UNIQUE_FULL String::NewSymbol("SQL_SP_MATCH_UNIQUE_FULL")
#define ndbcSQL_SP_MATCH_UNIQUE_PARTIAL String::NewSymbol("SQL_SP_MATCH_UNIQUE_PARTIAL")
#define ndbcSQL_SP_OVERLAPS String::NewSymbol("SQL_SP_OVERLAPS")
#define ndbcSQL_SP_QUANTIFIED_COMPARISON String::NewSymbol("SQL_SP_QUANTIFIED_COMPARISON")
#define ndbcSQL_SP_UNIQUE String::NewSymbol("SQL_SP_UNIQUE")
#define ndbcSQL_SPECIAL_CHARACTERS String::NewSymbol("SQL_SPECIAL_CHARACTERS")
#define ndbcSQL_SQ_COMPARISON String::NewSymbol("SQL_SQ_COMPARISON")
#define ndbcSQL_SQ_CORRELATED_SUBQUERIES String::NewSymbol("SQL_SQ_CORRELATED_SUBQUERIES")
#define ndbcSQL_SQ_EXISTS String::NewSymbol("SQL_SQ_EXISTS")
#define ndbcSQL_SQ_IN String::NewSymbol("SQL_SQ_IN")
#define ndbcSQL_SQ_QUANTIFIED String::NewSymbol("SQL_SQ_QUANTIFIED")
#define ndbcSQL_SQL_CONFORMANCE String::NewSymbol("SQL_SQL_CONFORMANCE")
#define ndbcSQL_SQL92_DATETIME_FUNCTIONS String::NewSymbol("SQL_SQL92_DATETIME_FUNCTIONS")
#define ndbcSQL_SQL92_FOREIGN_KEY_DELETE_RULE String::NewSymbol("SQL_SQL92_FOREIGN_KEY_DELETE_RULE")
#define ndbcSQL_SQL92_FOREIGN_KEY_UPDATE_RULE String::NewSymbol("SQL_SQL92_FOREIGN_KEY_UPDATE_RULE")
#define ndbcSQL_SQL92_GRANT String::NewSymbol("SQL_SQL92_GRANT")
#define ndbcSQL_SQL92_NUMERIC_VALUE_FUNCTIONS String::NewSymbol("SQL_SQL92_NUMERIC_VALUE_FUNCTIONS")
#define ndbcSQL_SQL92_PREDICATES String::NewSymbol("SQL_SQL92_PREDICATES")
#define ndbcSQL_SQL92_RELATIONAL_JOIN_OPERATORS String::NewSymbol("SQL_SQL92_RELATIONAL_JOIN_OPERATORS")
#define ndbcSQL_SQL92_REVOKE String::NewSymbol("SQL_SQL92_REVOKE")
#define ndbcSQL_SQL92_ROW_VALUE_CONSTRUCTOR String::NewSymbol("SQL_SQL92_ROW_VALUE_CONSTRUCTOR")
#define ndbcSQL_SQL92_STRING_FUNCTIONS String::NewSymbol("SQL_SQL92_STRING_FUNCTIONS")
#define ndbcSQL_SQL92_VALUE_EXPRESSIONS String::NewSymbol("SQL_SQL92_VALUE_EXPRESSIONS")
#define ndbcSQL_SR_CASCADE String::NewSymbol("SQL_SR_CASCADE")
#define ndbcSQL_SR_DELETE_TABLE String::NewSymbol("SQL_SR_DELETE_TABLE")
#define ndbcSQL_SR_GRANT_OPTION_FOR String::NewSymbol("SQL_SR_GRANT_OPTION_FOR")
#define ndbcSQL_SR_INSERT_COLUMN String::NewSymbol("SQL_SR_INSERT_COLUMN")
#define ndbcSQL_SR_INSERT_TABLE String::NewSymbol("SQL_SR_INSERT_TABLE")
#define ndbcSQL_SR_REFERENCES_COLUMN String::NewSymbol("SQL_SR_REFERENCES_COLUMN")
#define ndbcSQL_SR_REFERENCES_TABLE String::NewSymbol("SQL_SR_REFERENCES_TABLE")
#define ndbcSQL_SR_RESTRICT String::NewSymbol("SQL_SR_RESTRICT")
#define ndbcSQL_SR_SELECT_TABLE String::NewSymbol("SQL_SR_SELECT_TABLE")
#define ndbcSQL_SR_UPDATE_COLUMN String::NewSymbol("SQL_SR_UPDATE_COLUMN")
#define ndbcSQL_SR_UPDATE_TABLE String::NewSymbol("SQL_SR_UPDATE_TABLE")
#define ndbcSQL_SR_USAGE_ON_CHARACTER_SET String::NewSymbol("SQL_SR_USAGE_ON_CHARACTER_SET")
#define ndbcSQL_SR_USAGE_ON_COLLATION String::NewSymbol("SQL_SR_USAGE_ON_COLLATION")
#define ndbcSQL_SR_USAGE_ON_DOMAIN String::NewSymbol("SQL_SR_USAGE_ON_DOMAIN")
#define ndbcSQL_SR_USAGE_ON_TRANSLATION String::NewSymbol("SQL_SR_USAGE_ON_TRANSLATION")
#define ndbcSQL_SRJO_CORRESPONDING_CLAUSE String::NewSymbol("SQL_SRJO_CORRESPONDING_CLAUSE")
#define ndbcSQL_SRJO_CROSS_JOIN String::NewSymbol("SQL_SRJO_CROSS_JOIN")
#define ndbcSQL_SRJO_EXCEPT_JOIN String::NewSymbol("SQL_SRJO_EXCEPT_JOIN")
#define ndbcSQL_SRJO_FULL_OUTER_JOIN String::NewSymbol("SQL_SRJO_FULL_OUTER_JOIN")
#define ndbcSQL_SRJO_INNER_JOIN String::NewSymbol("SQL_SRJO_INNER_JOIN")
#define ndbcSQL_SRJO_INTERSECT_JOIN String::NewSymbol("SQL_SRJO_INTERSECT_JOIN")
#define ndbcSQL_SRJO_LEFT_OUTER_JOIN String::NewSymbol("SQL_SRJO_LEFT_OUTER_JOIN")
#define ndbcSQL_SRJO_NATURAL_JOIN String::NewSymbol("SQL_SRJO_NATURAL_JOIN")
#define ndbcSQL_SRJO_RIGHT_OUTER_JOIN String::NewSymbol("SQL_SRJO_RIGHT_OUTER_JOIN")
#define ndbcSQL_SRJO_UNION_JOIN String::NewSymbol("SQL_SRJO_UNION_JOIN")
#define ndbcSQL_SRVC_DEFAULT String::NewSymbol("SQL_SRVC_DEFAULT")
#define ndbcSQL_SRVC_NULL String::NewSymbol("SQL_SRVC_NULL")
#define ndbcSQL_SRVC_ROW_SUBQUERY String::NewSymbol("SQL_SRVC_ROW_SUBQUERY")
#define ndbcSQL_SRVC_VALUE_EXPRESSION String::NewSymbol("SQL_SRVC_VALUE_EXPRESSION")
#define ndbcSQL_SSF_CONVERT String::NewSymbol("SQL_SSF_CONVERT")
#define ndbcSQL_SSF_LOWER String::NewSymbol("SQL_SSF_LOWER")
#define ndbcSQL_SSF_SUBSTRING String::NewSymbol("SQL_SSF_SUBSTRING")
#define ndbcSQL_SSF_TRANSLATE String::NewSymbol("SQL_SSF_TRANSLATE")
#define ndbcSQL_SSF_TRIM_BOTH String::NewSymbol("SQL_SSF_TRIM_BOTH")
#define ndbcSQL_SSF_TRIM_LEADING String::NewSymbol("SQL_SSF_TRIM_LEADING")
#define ndbcSQL_SSF_TRIM_TRAILING String::NewSymbol("SQL_SSF_TRIM_TRAILING")
#define ndbcSQL_SSF_UPPER String::NewSymbol("SQL_SSF_UPPER")
#define ndbcSQL_STANDARD_CLI_CONFORMANCE String::NewSymbol("SQL_STANDARD_CLI_CONFORMANCE")
#define ndbcSQL_STATIC_CURSOR_ATTRIBUTES1 String::NewSymbol("SQL_STATIC_CURSOR_ATTRIBUTES1")
#define ndbcSQL_STATIC_CURSOR_ATTRIBUTES2 String::NewSymbol("SQL_STATIC_CURSOR_ATTRIBUTES2")
#define ndbcSQL_STILL_EXECUTING String::NewSymbol("SQL_STILL_EXECUTING")
#define ndbcSQL_STRING_FUNCTIONS String::NewSymbol("SQL_STRING_FUNCTIONS")
#define ndbcSQL_SU_DML_STATEMENTS String::NewSymbol("SQL_SU_DML_STATEMENTS")
#define ndbcSQL_SU_INDEX_DEFINITION String::NewSymbol("SQL_SU_INDEX_DEFINITION")
#define ndbcSQL_SU_PRIVILEGE_DEFINITION String::NewSymbol("SQL_SU_PRIVILEGE_DEFINITION")
#define ndbcSQL_SU_PROCEDURE_INVOCATION String::NewSymbol("SQL_SU_PROCEDURE_INVOCATION")
#define ndbcSQL_SU_TABLE_DEFINITION String::NewSymbol("SQL_SU_TABLE_DEFINITION")
#define ndbcSQL_SUBQUERIES String::NewSymbol("SQL_SUBQUERIES")
#define ndbcSQL_SUCCESS String::NewSymbol("SQL_SUCCESS")
#define ndbcSQL_SVE_CASE String::NewSymbol("SQL_SVE_CASE")
#define ndbcSQL_SVE_CAST String::NewSymbol("SQL_SVE_CAST")
#define ndbcSQL_SVE_COALESCE String::NewSymbol("SQL_SVE_COALESCE")
#define ndbcSQL_SVE_NULLIF String::NewSymbol("SQL_SVE_NULLIF")
#define ndbcSQL_SYSTEM_FUNCTIONS String::NewSymbol("SQL_SYSTEM_FUNCTIONS")
#define ndbcSQL_TABLE_TERM String::NewSymbol("SQL_TABLE_TERM")
#define ndbcSQL_TC_ALL String::NewSymbol("SQL_TC_ALL")
#define ndbcSQL_TC_DDL_COMMIT String::NewSymbol("SQL_TC_DDL_COMMIT")
#define ndbcSQL_TC_DDL_IGNORE String::NewSymbol("SQL_TC_DDL_IGNORE")
#define ndbcSQL_TC_DML String::NewSymbol("SQL_TC_DML")
#define ndbcSQL_TC_NONE String::NewSymbol("SQL_TC_NONE")
#define ndbcSQL_TIMEDATE_ADD_INTERVALS String::NewSymbol("SQL_TIMEDATE_ADD_INTERVALS")
#define ndbcSQL_TIMEDATE_DIFF_INTERVALS String::NewSymbol("SQL_TIMEDATE_DIFF_INTERVALS")
#define ndbcSQL_TIMEDATE_FUNCTIONS String::NewSymbol("SQL_TIMEDATE_FUNCTIONS")
#define ndbcSQL_TRUE String::NewSymbol("SQL_TRUE")
#define ndbcSQL_TXN_CAPABLE String::NewSymbol("SQL_TXN_CAPABLE")
#define ndbcSQL_TXN_ISOLATION_OPTION String::NewSymbol("SQL_TXN_ISOLATION_OPTION")
#define ndbcSQL_TXN_READ_COMMITTED String::NewSymbol("SQL_TXN_READ_COMMITTED")
#define ndbcSQL_TXN_READ_UNCOMMITTED String::NewSymbol("SQL_TXN_READ_UNCOMMITTED")
#define ndbcSQL_TXN_REPEATABLE_READ String::NewSymbol("SQL_TXN_REPEATABLE_READ")
#define ndbcSQL_TXN_SERIALIZABLE String::NewSymbol("SQL_TXN_SERIALIZABLE")
#define ndbcSQL_U_UNION String::NewSymbol("SQL_U_UNION")
#define ndbcSQL_U_UNION_ALL String::NewSymbol("SQL_U_UNION_ALL")
#define ndbcSQL_UB_OFF String::NewSymbol("SQL_UB_OFF")
#define ndbcSQL_UB_VARIABLE String::NewSymbol("SQL_UB_VARIABLE")
#define ndbcSQL_UNION String::NewSymbol("SQL_UNION")
#define ndbcSQL_UNSPECIFIED String::NewSymbol("SQL_UNSPECIFIED")
#define ndbcSQL_USER_NAME String::NewSymbol("SQL_USER_NAME")
#define ndbcSQL_XOPEN_CLI_YEAR String::NewSymbol("SQL_XOPEN_CLI_YEAR")

/* String representations of ndbc specific constants.
 */
#define ndbcINVALID_ARGUMENT String::NewSymbol("INVALID_ARGUMENT")
#define ndbcINVALID_RETURN String::NewSymbol("INVALID_RETURN")
#define ndbcINTERNAL_ERROR String::NewSymbol("INTERNAL_ERROR")

/* Mapping for SQLAllocHandle.
 * SQLAllocHandle(type, handle)
 * type - The handle type to allocate.
 *        Strings representing API constants will be translated into their constant values.
 *   SQL_HANDLE_ENV: Environment handle.
 *   SQL_HANDLE_DBC: Connection handle.
 *   SQL_HANDLE_STMT: Statement handle.
 *   SQL_HANDLE_DESC: Descriptor handle.
 * handle - Some handles require a handle as input.
 *   To allocate an environment handle, pass zero (which corresponds to the SQL null pointer).
 *   To allocate a connection handle, pass an environment handle.
 *   To allocate a statement handle, pass a connection handle.
 *   To allocate a descriptor handle, pass a connection handle.
 *
 * Declares, allocates, then returns a handle of the requested type.
 * If the function fails, returns a string describing the error.  Use typeof to determine success.
 * The returned handle is an external value, do NOT attempt to modify it in javascript.
 */
Handle<Value> ndbcSQLAllocHandle(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLHANDLE newHandle;
  SQLSMALLINT handleType;
  bool ok = true;

  /* Translate string inputs into constant values.
   */
  if (args[0]->ToString() == ndbcSQL_HANDLE_ENV) {
    handleType = SQL_HANDLE_ENV;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_DBC) {
    handleType = SQL_HANDLE_DBC;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_STMT) {
    handleType = SQL_HANDLE_STMT;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_DESC) {
    handleType = SQL_HANDLE_DESC;
  } else {
    ok = false;
    retVal = ndbcINVALID_ARGUMENT;
  }
  if (ok) {
    switch (SQLAllocHandle(handleType, (SQLHANDLE) External::Unwrap(args[1]), &newHandle)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      retVal = External::Wrap(newHandle);
    }
  }
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLFreeHandle.
 * SQLFreeHandle(type, handle)
 * type - The handle type to free.
 *        Strings representing API constants will be translated into their constant values.
 *   SQL_HANDLE_ENV: Environment handle.
 *   SQL_HANDLE_DBC: Connection handle.
 *   SQL_HANDLE_STMT: Statement handle.
 *   SQL_HANDLE_DESC: Descriptor handle.
 * handle - The handle to be freed.
 *
 * Frees the specified handle.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Any other return value indicates failure.
 */
Handle<Value> ndbcSQLFreeHandle(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLSMALLINT handleType;
  bool ok = true;

  /* Translate string inputs into constant values.
   */
  if (args[0]->ToString() == ndbcSQL_HANDLE_ENV) {
    handleType = SQL_HANDLE_ENV;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_DBC) {
    handleType = SQL_HANDLE_DBC;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_STMT) {
    handleType = SQL_HANDLE_STMT;
  } else if (args[0]->ToString() == ndbcSQL_HANDLE_DESC) {
    handleType = SQL_HANDLE_DESC;
  } else {
    ok = false;
    retVal = ndbcINVALID_ARGUMENT;
  }
  if (ok) {
    switch (SQLFreeHandle(handleType, (SQLHANDLE) External::Unwrap(args[1]))) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      retVal = ndbcSQL_SUCCESS;
    }
  }
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLSetEnvAttr
 * SQLSetEnvAttr(environment, attribute, value)
 * environment - An environment handle created with SQLAllocHandle.
 * attribute - The attribute to set.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_CONNECTION_POOLING: Enables or disables connection pooling at the environment level.
 *   SQL_ATTR_CP_MATCH: Determines how a connection is chosen from the connection pool.
 *   SQL_ATTR_ODBC_VERSION: Determines which ODBC features are available.
 *                          This value must be set prior to using the environment handle.
 *   SQL_ATTR_OUTPUT_NTS: Determines whether the string data returned by the driver is null-terminated.
 *                        Apparently read-only, don't try to set this to anything.
 * value - The string or numeric value to set.
 *         Strings representing API constants will be translated into their constant values.
 *         Different attribute types have different available values.
 *   SQL_ATTR_CONNECTION_POOLING
 *     SQL_CP_OFF: No connection pooling.
 *     SQL_CP_ONE_PER_DRIVER: Set up one connection pool for each driver.
 *     SQL_CP_ONE_PER_HENV: Set up one connection pool for each environment.
 *   SQL_ATTR_CP_MATCH
 *     SQL_CP_STRICT_MATCH: Match based on connection string and connection attributes.
 *     SQL_CP_RELAXED_MATCH: Match based on connection string alone.
 *   SQL_ATTR_ODBC_VERSION
 *     SQL_OV_ODBC3_80: OBDC 3.80
 *     SQL_OV_ODBC3: ODBC 3.0
 *     SQL_OV_ODBC2: ODBC 2.0
 *   SQL_ATTR_OUTPUT_NTS
 *     SQL_TRUE: Use null-terminated strings in database output.
 *     SQL_FALSE: Do not use null-terminated strings in database output.  Doesn't seem to be supported.
 *
 * Attempts to set the chosen attribute type to the value provided.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Any other return value indicates failure.
 */
Handle<Value> ndbcSQLSetEnvAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen = 0;
  bool ok = true;

  /* Translate string inputs into constant values.
   */
  if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_POOLING) {
    attrType = SQL_ATTR_CONNECTION_POOLING;
    if (args[2]->ToString() == ndbcSQL_CP_OFF) {
      attrVal = (SQLPOINTER) SQL_CP_OFF;
    } else if (args[2]->ToString() == ndbcSQL_CP_ONE_PER_DRIVER) {
      attrVal = (SQLPOINTER) SQL_CP_ONE_PER_DRIVER;
    } else if (args[2]->ToString() == ndbcSQL_CP_ONE_PER_HENV) {
      attrVal = (SQLPOINTER) SQL_CP_ONE_PER_HENV;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CP_MATCH) {
    attrType = SQL_ATTR_CP_MATCH;
    if (args[2]->ToString() == ndbcSQL_CP_STRICT_MATCH) {
      attrVal = (SQLPOINTER) SQL_CP_STRICT_MATCH;
    } else if (args[2]->ToString() == ndbcSQL_CP_RELAXED_MATCH) {
      attrVal = (SQLPOINTER) SQL_CP_RELAXED_MATCH;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ODBC_VERSION) {
    attrType = SQL_ATTR_ODBC_VERSION;
    if (args[2]->ToString() == ndbcSQL_OV_ODBC3_80) {
      attrVal = (SQLPOINTER) SQL_OV_ODBC3_80;
    } else if (args[2]->ToString() == ndbcSQL_OV_ODBC3) {
      attrVal = (SQLPOINTER) SQL_OV_ODBC3;
    } else if (args[2]->ToString() == ndbcSQL_OV_ODBC2) {
      attrVal = (SQLPOINTER) SQL_OV_ODBC2;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_OUTPUT_NTS) {
    attrType = SQL_ATTR_OUTPUT_NTS;
    if (args[2]->ToString() == ndbcSQL_TRUE) {
      attrVal = (SQLPOINTER) SQL_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_FALSE) {
      attrVal = (SQLPOINTER) SQL_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLSetEnvAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      retVal = ndbcSQL_SUCCESS;
    }
  }
  
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLGetEnvAttr
 * SQLGetEnvAttr(environment, attribute, [length])
 * environment - An environment handle created with SQLAllocHandle.
 * attribute - The attribute to set.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_CONNECTION_POOLING: Enables or disables connection pooling at the environment level.
 *   SQL_ATTR_CP_MATCH: Determines how a connection is chosen from the connection pool.
 *   SQL_ATTR_ODBC_VERSION: Determines which ODBC features are available.
 *                          This value must be set prior to using the environment handle.
 *   SQL_ATTR_OUTPUT_NTS: Determines whether the string data returned by the driver is null-terminated.
 *                        Apparently always returns SQL_TRUE.
 * length - The maximum length of returned text data.  Defaults to 255.
 *
 * Returns the value of the specified attribute.  Possible return values for each attribute are:
 *   SQL_ATTR_CONNECTION_POOLING
 *     SQL_CP_OFF: No connection pooling.
 *     SQL_CP_ONE_PER_DRIVER: Set up one connection pool for each driver.
 *     SQL_CP_ONE_PER_HENV: Set up one connection pool for each environment.
 *   SQL_ATTR_CP_MATCH
 *     SQL_CP_STRICT_MATCH: Match based on connection string and connection attributes.
 *     SQL_CP_RELAXED_MATCH: Match based on connection string alone.
 *   SQL_ATTR_ODBC_VERSION
 *     SQL_OV_ODBC3_80: OBDC 3.80
 *     SQL_OV_ODBC3: ODBC 3.0
 *     SQL_OV_ODBC2: ODBC 2.0
 *   SQL_ATTR_OUTPUT_NTS
 *     SQL_TRUE: Use null-terminated strings in database output.
 *     SQL_FALSE: Do not use null-terminated strings in database output.  Doesn't seem to be supported.
 *
 * Can also return SLQ_NO_DATA if no data is found, which could be considered a success.
 * Other returned values should be considered errors.
 */
Handle<Value> ndbcSQLGetEnvAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen;
  SQLINTEGER strLenPtr;
  bool ok = true;

  /* Allocate return buffer.
   */
  if (args.Length() == 3) {
    valLen = (SQLINTEGER) args[2]->Uint32Value();
  } else {
    valLen = 255;
  }
  attrVal = (SQLPOINTER) malloc(valLen + 1);

  /* Translate string inputs into constant values.
   */
  if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_POOLING) {
    attrType = SQL_ATTR_CONNECTION_POOLING;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CP_MATCH) {
    attrType = SQL_ATTR_CP_MATCH;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ODBC_VERSION) {
    attrType = SQL_ATTR_ODBC_VERSION;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_OUTPUT_NTS) {
    attrType = SQL_ATTR_OUTPUT_NTS;
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLGetEnvAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen, &strLenPtr)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      /* Translate constant values into string outputs.
       */
      switch (attrType) {
      case SQL_ATTR_CONNECTION_POOLING:
        switch (*(SQLUINTEGER*) attrVal) {
        case SQL_CP_OFF:
          retVal = ndbcSQL_CP_OFF;
          break;
        case SQL_CP_ONE_PER_DRIVER:
          retVal = ndbcSQL_CP_ONE_PER_DRIVER;
          break;
        case SQL_CP_ONE_PER_HENV:
          retVal = ndbcSQL_CP_ONE_PER_HENV;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CP_MATCH:
        switch (*(SQLUINTEGER*) attrVal) {
        case SQL_CP_STRICT_MATCH:
          retVal = ndbcSQL_CP_STRICT_MATCH;
          break;
        case SQL_CP_RELAXED_MATCH:
          retVal = ndbcSQL_CP_RELAXED_MATCH;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_ODBC_VERSION:
        switch (*(SQLINTEGER*) attrVal) {
        case SQL_OV_ODBC3_80:
          retVal = ndbcSQL_OV_ODBC3_80;
          break;
        case SQL_OV_ODBC3:
          retVal = ndbcSQL_OV_ODBC3;
          break;
        case SQL_OV_ODBC2:
          retVal = ndbcSQL_OV_ODBC2;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_OUTPUT_NTS:
        switch (*(SQLINTEGER*) attrVal) {
        case SQL_TRUE:
          retVal = ndbcSQL_TRUE;
          break;
        case SQL_FALSE:
          retVal = ndbcSQL_FALSE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      default:
        retVal = ndbcINVALID_ARGUMENT;
      }
    }
  }

  free(attrVal);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLConnect
 * SQLConnect(connection, dsn, user, password)
 * connection - A connection handle created with SQLAllocHandle.
 * dsn - The DSN to connect to.
 * user - The username to log in with.
 * password - The password to authenticate the user.
 *
 * Attempts to connect to the specified DSN with the supplied credentials.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Returns the string value 'SQL_STILL_EXECUTING' if it is connecting asynchronously and the
 * connection is still being attempted.
 * Other return values should be considered errors.
 */
Handle<Value> ndbcSQLConnect(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  String::AsciiValue dsn(args[1]->ToString());
  String::AsciiValue user(args[2]->ToString());
  String::AsciiValue password(args[3]->ToString());

  switch (SQLConnect((SQLHDBC) External::Unwrap(args[0]),
    (SQLCHAR *) *dsn, (SQLSMALLINT) dsn.length(),
    (SQLCHAR *) *user, (SQLSMALLINT) user.length(),
    (SQLCHAR *) *password, (SQLSMALLINT) password.length())) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  case SQL_STILL_EXECUTING:
    retVal = ndbcSQL_STILL_EXECUTING;
    break;
  default:
    retVal = ndbcSQL_SUCCESS;
  }

}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLDisconnect
 * SQLDisconnect(connection)
 * connection - A connection handle created with SQLAllocHandle.
 *
 * Attempts to disconnect from the specified DSN.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Returns the string value 'SQL_STILL_EXECUTING' if it is connecting asynchronously and the
 * connection is still being attempted.
 * Other return values should be considered errors.
 */
Handle<Value> ndbcSQLDisconnect(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {

  switch (SQLDisconnect((SQLHDBC) External::Unwrap(args[0]))) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  case SQL_STILL_EXECUTING:
    retVal = ndbcSQL_STILL_EXECUTING;
    break;
  default:
    retVal = ndbcSQL_SUCCESS;
  }

}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLSetConnectAttr
 * SQLSetConnectAttr(connection, attribute, value, [length])
 * connection - A connection handle created with SQLAllocHandle.
 * attribute - The attribute to set.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_ACCESS_MODE: Indicates that the connection is read-write or read-only.
 *   SQL_ATTR_ASYNC_ENABLE: Indicates that the connection runs synchronously or asynchronously.
 *   SQL_ATTR_AUTO_IPD: Indicates whether the connection supports automatically populating
 *                      implementation parameter descriptors when SQLPrepare is called.  Read-only.
 *   SQL_ATTR_AUTOCOMMIT: Indicates whether statements sent through this connection are immediately
 *                        committed to the database.
 *   SQL_ATTR_CONNECTION_DEAD: Indicates whether the connection is active.
 *   SQL_ATTR_CONNECTION_TIMEOUT: The number of seconds to wait before aborting an action on this connection.
 *                                0 indicates no timeout.
 *   SQL_ATTR_CURRENT_CATALOG: The current catalog / database the connection is using.
 *   SQL_ATTR_ENLIST_IN_DTC: Set this to use a transaction in Microsoft Distributed Transaction Coordinator.
 *   SQL_ATTR_LOGIN_TIMEOUT: The number of seconds to wait before aborting a logon attempt.
 *                           0 indicates no timeout.
 *   SQL_ATTR_METADATA_ID: Indicates whether the arguments of catalog functions will be treated as case-insensitive identifiers.
 *   SQL_ATTR_ODBC_CURSORS: Indicates whether to use the ODBC cursor implementation or the driver implementation.
 *                          ODBC cursor implementation is deprecated, so don't use this.
 *   SQL_ATTR_PACKET_SIZE: The size of network packets in bytes.  Set prior to connecting.
 *   SQL_ATTR_QUIET_MODE: Set this to a null pointer to prevent the driver from displaying dialog boxes.
 *   SQL_ATTR_TRACE: Indicates whether ODBC function calls will be sent to a log file.
 *   SQL_ATTR_TRACEFILE: Indicates the path of the trace log file.
 *   SQL_ATTR_TRANSLATE_LIB: Indicates the location of a translation library to translate between character sets.
 *   SQL_ATTR_TRANSLATE_OPTION: Passes a flag value to the translation library.
 *   SQL_ATTR_TXN_ISOLATION: Indicates the transaction isolation level of this library.
 * value - The string or numeric value to set.
 *         Strings representing API constants will be translated into their constant values.
 *         Different attribute types have different available values.
 *   SQL_ATTR_ACCESS_MODE
 *     SQL_MODE_READ_ONLY: Read-only connection.
 *     SQL_MODE_READ_WRITE: Read-write connection.
 *   SQL_ATTR_ASYNC_ENABLE
 *     SQL_ASYNC_ENABLE_OFF: Synchronous execution.
 *     SQL_ASYNC_ENABLE_ON: Asynchronous execution.
 *   SQL_ATTR_AUTO_IPD (Read-only)
 *     SQL_TRUE: Auto-IPD is supported.
 *     SQL_FALSE: Auto-IPD is not supported.
 *   SQL_ATTR_AUTOCOMMIT
 *     SQL_AUTOCOMMIT_OFF: Manually commit by calling SQLEndTrans.
 *     SQL_AUTOCOMMIT_ON: Automatically commit all statements immediately.
 *   SQL_ATTR_CONNECTION_DEAD (Read-only)
 *     SQL_CD_TRUE: Connection has been lost.
 *     SQL_CD_FALSE: Connection is active.
 *   SQL_ATTR_CONNECTION_TIMEOUT (supply an integer value, 0 indicates no timeout)
 *   SQL_ATTR_CURRENT_CATALOG (supply a string value)
 *   SQL_ATTR_ENLIST_IN_DTC
 *     (supply a DTC OLE transaction object to begin using it)
 *     SQL_DTC_DONE: Complete the DTC transaction.
 *   SQL_ATTR_LOGIN_TIMEOUT (supply an integer value, 0 indicates no timeout)
 *   SQL_ATTR_METADATA_ID
 *     SQL_TRUE: Treat catalog function arguments as case-insensitive identifiers.
 *     SQL_FALSE: Treat catalog function arguments as strings that may include search patterns.
 *   SQL_ATTR_ODBC_CURSORS (deprecated)
 *     SQL_CUR_USE_IF_NEEDED: Use driver implementation unless it doesn't support SQL_FETCH_PRIOR.
 *     SQL_CUR_USE_ODBC: Use ODBC implementation.
 *     SQL_CUR_USE_DRIVER: Use driver implementation.
 *   SQL_ATTR_PACKET_SIZE (supply an integer before connecting)
 *   SQL_ATTR_QUIET_MODE (supply a null pointer to disable most driver dialogs)
 *   SQL_ATTR_TRACE
 *     SQL_OPT_TRACE_OFF: No tracing.
 *     SQL_OPT_TRACE_ON: Log all ODBC function calls to a log file.
 *   SQL_ATTR_TRACEFILE (supply a string value)
 *   SQL_ATTR_TRANSLATE_LIB (supply a string value)
 *   SQL_ATTR_TRANSLATE_OPTION (see translate lib documentation)
 *   SQL_ATTR_TXN_ISOLATION (not all levels supported by all drivers)
 *     SQL_TXN_READ_UNCOMMITTED: Allow dirty reads, non-repeatable reads, and phantoms.
 *     SQL_TXN_READ_COMMITTED: Allow non-repeatable reads and phantoms.
 *     SQL_TXN_REPEATABLE_READ: Allow phantoms.
 *     SQL_TXN_SERIALIZABLE: Do not allow any transaction serialization violations.
 *
 * Attempts to set the chosen attribute type to the value provided.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Any other return value indicates failure.
 */
Handle<Value> ndbcSQLSetConnectAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen;
  bool ok = true;

  /* Translate string inputs into constant values.
   */
  if (args[1]->ToString() == ndbcSQL_ATTR_ACCESS_MODE) {
    attrType = SQL_ATTR_ACCESS_MODE;
    if (args[2]->ToString() == ndbcSQL_MODE_READ_ONLY) {
      attrVal = (SQLPOINTER) SQL_MODE_READ_ONLY;
    } else if (args[2]->ToString() == ndbcSQL_MODE_READ_WRITE) {
      attrVal = (SQLPOINTER) SQL_MODE_READ_WRITE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ASYNC_ENABLE) {
    attrType = SQL_ATTR_ASYNC_ENABLE;
    if (args[2]->ToString() == ndbcSQL_ASYNC_ENABLE_OFF) {
      attrVal = (SQLPOINTER) SQL_ASYNC_ENABLE_OFF;
    } else if (args[2]->ToString() == ndbcSQL_ASYNC_ENABLE_ON) {
      attrVal = (SQLPOINTER) SQL_ASYNC_ENABLE_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_AUTO_IPD) {
    attrType = SQL_ATTR_AUTO_IPD;
    if (args[2]->ToString() == ndbcSQL_TRUE) {
      attrVal = (SQLPOINTER) SQL_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_FALSE) {
      attrVal = (SQLPOINTER) SQL_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_AUTOCOMMIT) {
    attrType = SQL_ATTR_AUTOCOMMIT;
    if (args[2]->ToString() == ndbcSQL_AUTOCOMMIT_OFF) {
      attrVal = (SQLPOINTER) SQL_AUTOCOMMIT_OFF;
    } else if (args[2]->ToString() == ndbcSQL_AUTOCOMMIT_ON) {
      attrVal = (SQLPOINTER) SQL_AUTOCOMMIT_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_DEAD) {
    attrType = SQL_ATTR_CONNECTION_DEAD;
    if (args[2]->ToString() == ndbcSQL_CD_TRUE) {
      attrVal = (SQLPOINTER) SQL_CD_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_CD_FALSE) {
      attrVal = (SQLPOINTER) SQL_CD_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_TIMEOUT) {
    attrType = SQL_ATTR_CONNECTION_TIMEOUT;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURRENT_CATALOG) {
    attrType = SQL_ATTR_CURRENT_CATALOG;
    String::AsciiValue rawVal(args[2]->ToString());
    attrVal = (SQLPOINTER) *rawVal;
    valLen = (SQLINTEGER) rawVal.length();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ENLIST_IN_DTC) {
    attrType = SQL_ATTR_ENLIST_IN_DTC;
    if (args[2]->IsString() && args[2]->ToString() == ndbcSQL_DTC_DONE) {
      attrVal = (SQLPOINTER) SQL_DTC_DONE;
    } else if (args[2]->IsExternal()) {
      attrVal = (SQLPOINTER) External::Unwrap(args[2]);
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_LOGIN_TIMEOUT) {
    attrType = SQL_ATTR_LOGIN_TIMEOUT;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_METADATA_ID) {
    attrType = SQL_ATTR_METADATA_ID;
    if (args[2]->ToString() == ndbcSQL_TRUE) {
      attrVal = (SQLPOINTER) SQL_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_FALSE) {
      attrVal = (SQLPOINTER) SQL_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ODBC_CURSORS) {
    attrType = SQL_ATTR_ODBC_CURSORS;
    if (args[2]->ToString() == ndbcSQL_CUR_USE_IF_NEEDED) {
      attrVal = (SQLPOINTER) SQL_CUR_USE_IF_NEEDED;
    } else if (args[2]->ToString() == ndbcSQL_CUR_USE_ODBC) {
      attrVal = (SQLPOINTER) SQL_CUR_USE_ODBC;
    } else if (args[2]->ToString() == ndbcSQL_CUR_USE_DRIVER) {
      attrVal = (SQLPOINTER) SQL_CUR_USE_DRIVER;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PACKET_SIZE) {
    attrType = SQL_ATTR_PACKET_SIZE;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_QUIET_MODE) {
    attrType = SQL_ATTR_QUIET_MODE;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRACE) {
    attrType = SQL_ATTR_TRACE;
    if (args[2]->ToString() == ndbcSQL_OPT_TRACE_OFF) {
      attrVal = (SQLPOINTER) SQL_OPT_TRACE_OFF;
    } else if (args[2]->ToString() == ndbcSQL_OPT_TRACE_ON) {
      attrVal = (SQLPOINTER) SQL_OPT_TRACE_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRACEFILE) {
    attrType = SQL_ATTR_TRACEFILE;
    String::AsciiValue rawVal(args[2]->ToString());
    attrVal = (SQLPOINTER) *rawVal;
    valLen = (SQLINTEGER) rawVal.length();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRANSLATE_LIB) {
    attrType = SQL_ATTR_TRANSLATE_LIB;
    String::AsciiValue rawVal(args[2]->ToString());
    attrVal = (SQLPOINTER) *rawVal;
    valLen = (SQLINTEGER) rawVal.length();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRANSLATE_OPTION) {
    attrType = SQL_ATTR_TRANSLATE_OPTION;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TXN_ISOLATION) {
    attrType = SQL_ATTR_TXN_ISOLATION;
    if (args[2]->ToString() == ndbcSQL_TXN_READ_UNCOMMITTED) {
      attrVal = (SQLPOINTER) SQL_TXN_READ_UNCOMMITTED;
    } else if (args[2]->ToString() == ndbcSQL_TXN_READ_COMMITTED) {
      attrVal = (SQLPOINTER) SQL_TXN_READ_COMMITTED;
    } else if (args[2]->ToString() == ndbcSQL_TXN_REPEATABLE_READ) {
      attrVal = (SQLPOINTER) SQL_TXN_REPEATABLE_READ;
    } else if (args[2]->ToString() == ndbcSQL_TXN_SERIALIZABLE) {
      attrVal = (SQLPOINTER) SQL_TXN_SERIALIZABLE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLSetConnectAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      retVal = ndbcSQL_SUCCESS;
    }
  }
  
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLGetConnectAttr
 * SQLGetConnectAttr(connection, attribute, [length])
 * connection - A connection handle created with SQLAllocHandle.
 * attribute - The attribute to retrieve the value of.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_ACCESS_MODE: Indicates that the connection is read-write or read-only.
 *   SQL_ATTR_ASYNC_ENABLE: Indicates that the connection runs synchronously or asynchronously.
 *   SQL_ATTR_AUTO_IPD: Indicates whether the connection supports automatically populating
 *                      implementation parameter descriptors when SQLPrepare is called.  Read-only.
 *   SQL_ATTR_AUTOCOMMIT: Indicates whether statements sent through this connection are immediately
 *                        committed to the database.
 *   SQL_ATTR_CONNECTION_DEAD: Indicates whether the connection is active.
 *   SQL_ATTR_CONNECTION_TIMEOUT: The number of seconds to wait before aborting an action on this connection.
 *                                0 indicates no timeout.
 *   SQL_ATTR_CURRENT_CATALOG: The current catalog / database the connection is using.
 *   SQL_ATTR_ENLIST_IN_DTC: Set this to use a transaction in Microsoft Distributed Transaction Coordinator.
 *   SQL_ATTR_LOGIN_TIMEOUT: The number of seconds to wait before aborting a logon attempt.
 *                           0 indicates no timeout.
 *   SQL_ATTR_METADATA_ID: Indicates whether the arguments of catalog functions will be treated as case-insensitive identifiers.
 *   SQL_ATTR_ODBC_CURSORS: Indicates whether to use the ODBC cursor implementation or the driver implementation.
 *                          ODBC cursor implementation is deprecated, so don't use this.
 *   SQL_ATTR_PACKET_SIZE: The size of network packets in bytes.  Set prior to connecting.
 *   SQL_ATTR_QUIET_MODE: Set this to a null pointer to prevent the driver from displaying dialog boxes.
 *   SQL_ATTR_TRACE: Indicates whether ODBC function calls will be sent to a log file.
 *   SQL_ATTR_TRACEFILE: Indicates the path of the trace log file.
 *   SQL_ATTR_TRANSLATE_LIB: Indicates the location of a translation library to translate between character sets.
 *   SQL_ATTR_TRANSLATE_OPTION: Passes a flag value to the translation library.
 *   SQL_ATTR_TXN_ISOLATION: Indicates the transaction isolation level of this library.
 * length - The maximum length of returned text data.  Defaults to 255.
 *
 * Returns the value of the specified attribute.  Possible return values for each attribute are:
 *   SQL_ATTR_ACCESS_MODE
 *     SQL_MODE_READ_ONLY: Read-only connection.
 *     SQL_MODE_READ_WRITE: Read-write connection.
 *   SQL_ATTR_ASYNC_ENABLE
 *     SQL_ASYNC_ENABLE_OFF: Synchronous execution.
 *     SQL_ASYNC_ENABLE_ON: Asynchronous execution.
 *   SQL_ATTR_AUTO_IPD (Read-only)
 *     SQL_TRUE: Auto-IPD is supported.
 *     SQL_FALSE: Auto-IPD is not supported.
 *   SQL_ATTR_AUTOCOMMIT
 *     SQL_AUTOCOMMIT_OFF: Manually commit by calling SQLEndTrans.
 *     SQL_AUTOCOMMIT_ON: Automatically commit all statements immediately.
 *   SQL_ATTR_CONNECTION_DEAD (Read-only)
 *     SQL_CD_TRUE: Connection has been lost.
 *     SQL_CD_FALSE: Connection is active.
 *   SQL_ATTR_CONNECTION_TIMEOUT (returns an integer value, 0 indicates no timeout)
 *   SQL_ATTR_CURRENT_CATALOG (returns a string value)
 *   SQL_ATTR_ENLIST_IN_DTC (this probably can't be read)
 *   SQL_ATTR_LOGIN_TIMEOUT (returns an integer value, 0 indicates no timeout)
 *   SQL_ATTR_METADATA_ID
 *     SQL_TRUE: Treat catalog function arguments as case-insensitive identifiers.
 *     SQL_FALSE: Treat catalog function arguments as strings that may include search patterns.
 *   SQL_ATTR_ODBC_CURSORS (deprecated)
 *     SQL_CUR_USE_IF_NEEDED: Use driver implementation unless it doesn't support SQL_FETCH_PRIOR.
 *     SQL_CUR_USE_ODBC: Use ODBC implementation.
 *     SQL_CUR_USE_DRIVER: Use driver implementation.
 *   SQL_ATTR_PACKET_SIZE (returns an integer before connecting)
 *   SQL_ATTR_QUIET_MODE (returns a null pointer if driver dialogs are disabled)
 *   SQL_ATTR_TRACE
 *     SQL_OPT_TRACE_OFF: No tracing.
 *     SQL_OPT_TRACE_ON: Log all ODBC function calls to a log file.
 *   SQL_ATTR_TRACEFILE (returns a string value)
 *   SQL_ATTR_TRANSLATE_LIB (returns a string value)
 *   SQL_ATTR_TRANSLATE_OPTION (see translate lib documentation)
 *   SQL_ATTR_TXN_ISOLATION (not all levels supported by all drivers)
 *     SQL_TXN_READ_UNCOMMITTED: Allow dirty reads, non-repeatable reads, and phantoms.
 *     SQL_TXN_READ_COMMITTED: Allow non-repeatable reads and phantoms.
 *     SQL_TXN_REPEATABLE_READ: Allow phantoms.
 *     SQL_TXN_SERIALIZABLE: Do not allow any transaction serialization violations.
 *
 * Can also return SLQ_NO_DATA if no data is found, which could be considered a success.
 * Other returned values should be considered errors.
 */
Handle<Value> ndbcSQLGetConnectAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen;
  SQLINTEGER strLenPtr;
  bool ok = true;

  /* Allocate return buffer.
   */
  if (args.Length() == 3) {
    valLen = (SQLINTEGER) args[2]->Uint32Value();
  } else {
    valLen = 255;
  }
  attrVal = (SQLPOINTER) malloc(valLen + 1);

  /* Translate string inputs into constant values.
   */
  if (args[1]->ToString() == ndbcSQL_ATTR_ACCESS_MODE) {
    attrType = SQL_ATTR_ACCESS_MODE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ASYNC_ENABLE) {
    attrType = SQL_ATTR_ASYNC_ENABLE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_AUTO_IPD) {
    attrType = SQL_ATTR_AUTO_IPD;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_AUTOCOMMIT) {
    attrType = SQL_ATTR_AUTOCOMMIT;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_DEAD) {
    attrType = SQL_ATTR_CONNECTION_DEAD;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CONNECTION_TIMEOUT) {
    attrType = SQL_ATTR_CONNECTION_TIMEOUT;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURRENT_CATALOG) {
    attrType = SQL_ATTR_CURRENT_CATALOG;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ENLIST_IN_DTC) {
    attrType = SQL_ATTR_ENLIST_IN_DTC;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_LOGIN_TIMEOUT) {
    attrType = SQL_ATTR_LOGIN_TIMEOUT;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ODBC_CURSORS) {
    attrType = SQL_ATTR_ODBC_CURSORS;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PACKET_SIZE) {
    attrType = SQL_ATTR_PACKET_SIZE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_QUIET_MODE) {
    attrType = SQL_ATTR_QUIET_MODE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRACE) {
    attrType = SQL_ATTR_TRACE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRACEFILE) {
    attrType = SQL_ATTR_TRACEFILE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRANSLATE_LIB) {
    attrType = SQL_ATTR_TRANSLATE_LIB;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TRANSLATE_OPTION) {
    attrType = SQL_ATTR_TRANSLATE_OPTION;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_TXN_ISOLATION) {
    attrType = SQL_ATTR_TXN_ISOLATION;
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLGetConnectAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen, &strLenPtr)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      /* Translate constant values into string outputs.
       */
      switch (attrType) {
      case SQL_ATTR_ACCESS_MODE:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_MODE_READ_ONLY:
          retVal = ndbcSQL_MODE_READ_ONLY;
          break;
        case SQL_MODE_READ_WRITE:
          retVal = ndbcSQL_MODE_READ_WRITE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_ASYNC_ENABLE:
        switch (*(SQLULEN*)attrVal) {
        case SQL_ASYNC_ENABLE_OFF:
          retVal = ndbcSQL_ASYNC_ENABLE_OFF;
          break;
        case SQL_ASYNC_ENABLE_ON:
          retVal = ndbcSQL_ASYNC_ENABLE_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_AUTO_IPD:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_TRUE:
          retVal = ndbcSQL_TRUE;
          break;
        case SQL_FALSE:
          retVal = ndbcSQL_FALSE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_AUTOCOMMIT:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_AUTOCOMMIT_OFF:
          retVal = ndbcSQL_AUTOCOMMIT_OFF;
          break;
        case SQL_AUTOCOMMIT_ON:
          retVal = ndbcSQL_AUTOCOMMIT_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CONNECTION_DEAD:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_CD_TRUE:
          retVal = ndbcSQL_CD_TRUE;
          break;
        case SQL_CD_FALSE:
          retVal = ndbcSQL_CD_FALSE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CONNECTION_TIMEOUT:
        retVal = Integer::NewFromUnsigned(*(SQLUINTEGER*)attrVal);
        break;
      case SQL_ATTR_CURRENT_CATALOG:
        retVal = String::New((char*) attrVal);
        break;
      case SQL_ATTR_ENLIST_IN_DTC:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_DTC_DONE:
          retVal = ndbcSQL_DTC_DONE;
          break;
        default:
          retVal = External::Wrap((void*) *(SQLINTEGER*)attrVal);
        }
        break;
      case SQL_ATTR_LOGIN_TIMEOUT:
        retVal = Integer::NewFromUnsigned(*(SQLUINTEGER*)attrVal);
        break;
      case SQL_ATTR_METADATA_ID:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_TRUE:
          retVal = ndbcSQL_TRUE;
          break;
        case SQL_FALSE:
          retVal = ndbcSQL_FALSE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_ODBC_CURSORS:
        switch (*(SQLULEN*)attrVal) {
        case SQL_CUR_USE_IF_NEEDED:
          retVal = ndbcSQL_CUR_USE_IF_NEEDED;
          break;
        case SQL_CUR_USE_ODBC:
          retVal = ndbcSQL_CUR_USE_ODBC;
          break;
        case SQL_CUR_USE_DRIVER:
          retVal = ndbcSQL_CUR_USE_DRIVER;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_PACKET_SIZE:
        retVal = Integer::NewFromUnsigned(*(SQLUINTEGER*)attrVal);
        break;
      case SQL_ATTR_QUIET_MODE:
        retVal = External::Wrap((void*) *(SQLHWND*)attrVal);
        break;
      case SQL_ATTR_TRACE:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_OPT_TRACE_OFF:
          retVal = ndbcSQL_OPT_TRACE_OFF;
          break;
        case SQL_OPT_TRACE_ON:
          retVal = ndbcSQL_OPT_TRACE_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_TRACEFILE:
        retVal = String::New((char*) attrVal);
        break;
      case SQL_ATTR_TRANSLATE_LIB:
        retVal = String::New((char*) attrVal);
        break;
      case SQL_ATTR_TRANSLATE_OPTION:
        retVal = External::Wrap((void*) *(SQLUINTEGER*)attrVal);
        break;
      case SQL_ATTR_TXN_ISOLATION:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_TXN_READ_UNCOMMITTED:
          retVal = ndbcSQL_TXN_READ_UNCOMMITTED;
          break;
        case SQL_TXN_READ_COMMITTED:
          retVal = ndbcSQL_TXN_READ_COMMITTED;
          break;
        case SQL_TXN_REPEATABLE_READ:
          retVal = ndbcSQL_TXN_REPEATABLE_READ;
          break;
        case SQL_TXN_SERIALIZABLE:
          retVal = ndbcSQL_TXN_SERIALIZABLE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      default:
        retVal = ndbcINVALID_ARGUMENT;
      }
    }
  }

  free(attrVal);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLGetInfo
 * SQLGetInfo(connection, attribute, [input], [length])
 * connection - A connection handle created with SQLAllocHandle.
 * attribute - The attribute to retrieve the value of.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ACCESSIBLE_PROCEDURES
 *     Indicates whether user can execute all procedures returned by SQLProcedures.
 *   SQL_ACCESSIBLE_TABLES
 *     Indicates whether user can execute all tables returned by SQLTables.
 *   SQL_ACTIVE_ENVIRONMENTS
 *     The maximum number of active environments that the driver can support.
 *   SQL_AGGREGATE_FUNCTIONS
 *     Describes support for aggregation functions.
 *   SQL_ALTER_DOMAIN
 *     Describes support for the ALTER DOMAIN statement.
 *   SQL_ALTER_TABLE
 *     Describes support for the ALTER TABLE statement.
 *   SQL_ASYNC_MODE
 *     Indicates the level of asynchronous support in the driver.
 *   SQL_ASYNC_NOTIFICATION
 *     Indicates whether the driver supports asynchronous notification (ODBC3.8 only?)
 *   SQL_BATCH_ROW_COUNT
 *     Describes the availability of row counts when running statements with this driver.
 *   SQL_BATCH_SUPPORT
 *     Describes the driver's support for statement batches and procedures.
 *   SQL_BOOKMARK_PERSISTENCE
 *     Describes the operations through which bookmarks remain available.
 *   SQL_CATALOG_LOCATION
 *     Indicates the position of the catalog in a qualified table name.
 *   SQL_CATALOG_NAME
 *     Indicates whether the server supports catalog names.
 *   SQL_CATALOG_NAME_SEPARATOR
 *     The string that separates catalog names from the rest of the identifier.  Usuallly ".".
 *   SQL_CATALOG_TERM
 *     The term this data source uses for a catalog (database, directory, etc).
 *   SQL_CATALOG_USAGE
 *     Describes the statements that support using catalog names.
 *   SQL_COLLATION_SEQ
 *     The name of the server's default collation sequence.  (This is usually a character set name.)
 *   SQL_COLUMN_ALIAS
 *     Indicates whether the server supports column aliases.
 *   SQL_CONCAT_NULL_BEHAVIOR
 *     Indicates what happens when NULL is concatenated with string data.
 *   SQL_CONVERT_BIGINT, SQL_CONVERT_BINARY, SQL_CONVERT_BIT , SQL_CONVERT_CHAR , SQL_CONVERT_GUID,
 *   SQL_CONVERT_DATE, SQL_CONVERT_DECIMAL, SQL_CONVERT_DOUBLE, SQL_CONVERT_FLOAT, SQL_CONVERT_INTEGER,
 *   SQL_CONVERT_INTERVAL_YEAR_MONTH, SQL_CONVERT_INTERVAL_DAY_TIME, SQL_CONVERT_LONGVARBINARY,
 *   SQL_CONVERT_LONGVARCHAR, SQL_CONVERT_NUMERIC, SQL_CONVERT_REAL, SQL_CONVERT_SMALLINT, SQL_CONVERT_TIME,
 *   SQL_CONVERT_TIMESTAMP, SQL_CONVERT_TINYINT, SQL_CONVERT_VARBINARY, SQL_CONVERT_VARCHAR
 *     Describes what data types each data type can be converted to.
 *   SQL_CONVERT_FUNCTIONS
 *     Describes which data conversion functions are supported.
 *   SQL_CORRELATION_NAME
 *     Indicates the level of support for table correlation names.
 *   SQL_CREATE_ASSERTION
 *     Describes the level of support for the CREATE ASSERTION statement.
 *   SQL_CREATE_CHARACTER_SET
 *     Describes the level of support for the CREATE CHARACTER SET statement.
 *   SQL_CREATE_COLLATION
 *     Describes the level of support for the CREATE COLLATION statement.
 *   SQL_CREATE_DOMAIN
 *     Describes the level of support for the CREATE DOMAIN statement.
 *   SQL_CREATE_SCHEMA
 *     Describes the level of support for the CREATE SCHEMA statement.
 *   SQL_CREATE_TABLE
 *     Describes the level of support for the CREATE TABLE statement.
 *   SQL_CREATE_TRANSLATION
 *     Describes the level of support for the CREATE TRANSLATION statement.
 *   SQL_CREATE_VIEW
 *     Describes the level of support for the CREATE VIEW statement.
 *   SQL_CURSOR_COMMIT_BEHAVIOR
 *     Indicates what happens to cursors and prepared statements when a database commits a related transaction.
 *   SQL_CURSOR_ROLLBACK_BEHAVIOR
 *     Indicates what happens to cursors and prepared statements when a database rolls back a related transaction.
 *   SQL_CURSOR_SENSITIVITY
 *     Indicates whether cursors are sensitive to changes made by other cursors in the same transaction.
 *   SQL_DATA_SOURCE_NAME
 *     The DSN of the current connection, if there is one.
 *   SQL_DATA_SOURCE_READ_ONLY
 *     Indicates whether the current connection is read-only.
 *   SQL_DATABASE_NAME
 *     The name of the current database.
 *   SQL_DATETIME_LITERALS
 *     Describes which date / time literals are supported.
 *   SQL_DBMS_NAME
 *     The name of the database software the driver is connected to.
 *   SQL_DBMS_VER
 *     The version of the currently connected database.
 *   SQL_DDL_INDEX
 *     Describes the level of support for creating and dropping indices.
 *   SQL_DEFAULT_TXN_ISOLATION
 *     Indicates the default transaction level used by the server.
 *   SQL_DESCRIBE_PARAMETER
 *     Indicates whether parameters can be described (eg the DESCRIBE INPUT statement).
 *   SQL_DM_VER
 *     The version of the current driver manager.
 *   SQL_DRIVER_HDBC
 *     The current connection handle.
 *   SQL_DRIVER_HENV
 *     The current environment handle.
 *   SQL_DRIVER_HDESC
 *     The driver's descriptor handle.  (?)
 *   SQL_DRIVER_HLIB
 *     The handle of the load library for the driver's DLL.  Valid for this connection only.
 *   SQL_DRIVER_HSTMT
 *     The driver's statement handle, based on the driver manager's statement handle.  (?)
 *   SQL_DRIVER_NAME
 *     The name of the current driver in use.
 *   SQL_DRIVER_ODBC_VER
 *     The ODBC version supported by the driver.
 *   SQL_DRIVER_VER
 *     The version of the current driver.
 *   SQL_DROP_ASSERTION
 *     Describes support for the DROP ASSERTION statement.
 *   SQL_DROP_CHARACTER_SET
 *     Describes support for the DROP CHARACTER SET statement.
 *   SQL_DROP_COLLATION
 *     Describes support for the DROP COLLATION statement.
 *   SQL_DROP_DOMAIN
 *     Describes support for the DROP DOMAIN statement.
 *   SQL_DROP_SCHEMA
 *     Describes support for the DROP SCHEMA statement.
 *   SQL_DROP_TABLE
 *     Describes support for the DROP TABLE statement.
 *   SQL_DROP_TRANSLATION
 *     Describes support for the DROP TRANSLATION statement.
 *   SQL_DROP_VIEW
 *     Describes support for the DROP VIEW statement.
 *   SQL_DYNAMIC_CURSOR_ATTRIBUTES1
 *     Describes support for the first of 2 sets of cursor features for dynamic cursors.
 *   SQL_DYNAMIC_CURSOR_ATTRIBUTES2
 *     Describes support for the second of 2 sets of cursor features for dynamic cursors.
 *   SQL_EXPRESSIONS_IN_ORDERBY
 *     Indicates support for expressions in the ORDER BY clause.
 *   SQL_FILE_USAGE
 *     Indicates how a single-tier driver treates files in a data source.
 *   SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1
 *     Describes support for the first of 2 sets of cursor features for forward-only cursors.
 *   SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2
 *     Describes support for the second of 2 sets of cursor features for forward-only cursors.
 *   SQL_GETDATA_EXTENSIONS
 *     Describes the level of support for SQLGetData extensions.
 *   SQL_GROUP_BY
 *     Describes the level of support for the GROUP BY clause.
 *   SQL_IDENTIFIER_CASE
 *     Indicates the natural case of identifiers on the server.
 *   SQL_IDENTIFIER_QUOTE_CHAR
 *     The string that delimits quoted identifiers.  Usually '"'.
 *   SQL_INDEX_KEYWORDS
 *     Describes the level of support for CREATE INDEX keywords.
 *   SQL_INFO_SCHEMA_VIEWS
 *     Describes the level of support for INFORMATION_SCHEMA views.
 *   SQL_INSERT_STATEMENT
 *     Describes the level of support for INSERT statements.
 *   SQL_INTEGRITY
 *     Indicates support for the Integrity Enhancement Facility.
 *   SQL_KEYSET_CURSOR_ATTRIBUTES1
 *     Describes support for the first of 2 sets of cursor features for keyset cursors.
 *   SQL_KEYSET_CURSOR_ATTRIBUTES2
 *     Describes support for the second of 2 sets of cursor features for keyset cursors.
 *   SQL_KEYWORDS
 *     A comma separated list of driver-specific reserved words.
 *   SQL_LIKE_ESCAPE_CLAUSE
 *     Indicates support for escaping the special characters % and _ in a LIKE predicate.
 *   SQL_MAX_ASYNC_CONCURRENT_STATEMENTS
 *     The maximum number of active concurrent asynchronous statements supported by the driver.  0 indicates no limit.
 *   SQL_MAX_BINARY_LITERAL_LEN
 *     The maximum hexadecimal length of a binary literal.  0 indicates no limit.
 *   SQL_MAX_CATALOG_NAME_LEN
 *     The maximum length of a catalog name.  0 indicates no limit.
 *   SQL_MAX_CHAR_LITERAL
 *     The maximum length of a character literal.  0 indicates no limit.
 *   SQL_MAX_COLUMN_NAME_LEN
 *     The maximum length of a column name.  0 indicates no limit.
 *   SQL_MAX_COLUMNS_IN_GROUP_BY
 *     The maximum number of columns that can be in a GROUP BY clause.  0 indicates no limit.
 *   SQL_MAX_COLUMNS_IN_INDEX
 *     The maximum number of columns that can be in an index.  0 indicates no limit.
 *   SQL_MAX_COLUMNS_IN_ORDER_BY
 *     The maximum number of columns that can be in an ORDER BY clause.  0 indicates no limit.
 *   SQL_MAX_COLUMNS_IN_SELECT
 *     The maximum number of columns that can be in a SELECT clause.  0 indicates no limit.
 *   SQL_MAX_COLUMNS_IN_TABLE
 *     The maximum number of columns that can be in a table.  0 indicates no limit.
 *   SQL_MAX_CONCURRENT_ACTIVITIES
 *     The maximum number of active statements supported by the driver for a connection.  0 indicates no limit.
 *   SQL_MAX_CURSOR_NAME_LEN
 *     The maximum length of a cursor name.  0 indicates no limit.
 *   SQL_MAX_DRIVER_CONNECTIONS
 *     The maximum number of active connections supported by the driver.  0 indicates no limit.
 *   SQL_MAX_IDENTIFIER_LEN
 *     The maximum length of an identifier.  0 indicates no limit.
 *   SQL_MAX_INDEX_SIZE
 *     The maximum number of bytes in an index.  0 indicates no limit.
 *   SQL_MAX_PROCEDURE_NAME_LEN
 *     The maximum length of a procedure name.  0 indicates no limit.
 *   SQL_MAX_ROW_SIZE
 *     The maximum number of byte in a row.  0 indicates no limit.
 *   SQL_MAX_ROW_SIZE_INCLUDES_LONG
 *     Indicates whether LONGVARCHAR and LONGVARBINARY sizes are included in the row size limit.
 *   SQL_MAX_SCHEMA_NAME_LEN
 *     The maximum length of a schema name.  0 indicates no limit.
 *   SQL_MAX_STATEMENT_LEN
 *     The maximum length of a SQL statement (including whitespace).  0 indicates no limit.
 *   SQL_MAX_TABLE_NAME_LEN
 *     The maximum length of a table name.  0 indicates no limit.
 *   SQL_MAX_TABLES_IN_SELECT
 *     The maximum number of tables that can be in a FROM clause.  0 indicates no limit.
 *   SQL_MAX_USER_NAME_LEN
 *     The maximum length of a user name.  0 indicates no limit.
 *   SQL_MULT_RESULT_SETS
 *     Indicates support for multiple result sets.
 *   SQL_MULTIPLE_ACTIVE_TXN
 *     Indicates support for multiple active simultaneous transactions.
 *   SQL_NEED_LONG_DATA_LEN
 *     Indicates whether the length of a long data value must be provided when sending that data to the server.
 *   SQL_NON_NULLABLE_COLUMNS
 *     Indicates support for the NOT NULL constraint on columns.
 *   SQL_NULL_COLLATION
 *     Indicates how NULLs are sorted in sorted data.
 *   SQL_NUMERIC_FUNCTIONS
 *     Describes which numeric functions are supported.
 *   SQL_ODBC_INTERFACE_CONFORMANCE
 *     Indicates the level of compliance the driver has with the ODBC 3.x interface.
 *   SQL_ODBC_VER
 *     The version of ODBC used by the driver manager.
 *   SQL_OJ_CAPABILITIES
 *     Describes the level of support for OUTER JOINs.
 *   SQL_ORDER_BY_COLUMNS_IN_SELECT
 *     Indicates whether columns in the ORDER BY clause must be in the SELECT clause.
 *   SQL_PARAM_ARRAY_ROW_COUNTS
 *     Indicates how row counts from parameterized execution with multiple parameter sets are made available.
 *   SQL_PARAM_ARRAY_SELECTS
 *     Indicates how results from parameterized execution with multiple parameter sets are made available.
 *   SQL_PROCEDURE_TERM
 *     The term used by the server to refer to a procedure.
 *   SQL_PROCEDURES
 *     Indicates support for stored procedures and the ODBC invocation syntax.
 *   SQL_POS_OPERATIONS
 *     Describes support for SQLSetPos.
 *   SQL_QUOTED_IDENTIFIER_CASE
 *     Indicates the natural case of quoted identifiers on the server.
 *   SQL_ROW_UPDATES
 *     Indicates support for detecting whether rows in a result dataset were updated since the last time they were fetched.
 *   SQL_SCHEMA_TERM
 *     The term used by the server to refer to a schema.
 *   SQL_SCHEMA_USAGE
 *     Describes which statements can contain schema names.
 *   SQL_SCROLL_OPTIONS
 *     Describes the level of scrolling support for scrollable cursors.
 *   SQL_SEARCH_PATTERN_ESCAPE
 *     The string used by the server to escape search terms % and _ in catlog function arguments.
 *   SQL_SERVER_NAME
 *     The server name of the connected server.
 *   SQL_SPECIAL_CHARACTERS
 *     The special characters that can be used in a quoted identifier.
 *   SQL_SQL_CONFORMANCE
 *     Indicates the level of conformance with the SQL-92 standard.
 *   SQL_SQL92_DATETIME_FUNCTIONS
 *     Describes the level of support for date / time functions.
 *   SQL_SQL92_FOREIGN_KEY_DELETE_RULE
 *     Describes the level of support for cascading DELETE behavior.
 *   SQL_SQL92_FOREIGN_KEY_UPDATE_RULE
 *     Describes the level of support for cascading UPDATE behavior.
 *   SQL_SQL92_GRANT
 *     Describes the level of support for the GRANT statement.
 *   SQL_SQL92_NUMERIC_VALUE_FUNCTIONS
 *     Describes the level of support for numeric value functions.
 *   SQL_SQL92_PREDICATES
 *     Describes the level of support for predicates in the WHERE clause.
 *   SQL_SQL92_RELATIONAL_JOIN_OPERATORS
 *     Describes the level of support for SQL JOIN operators.
 *   SQL_SQL92_REVOKE
 *     Describes the level of support for the REVOKE statement.
 *   SQL_SQL92_ROW_VALUE_CONSTRUCTOR
 *     Describes the level of support for value expressions.
 *   SQL_SQL92_STRING_FUNCTIONS
 *     Describes the level of support for SQL 92 string functions.
 *   SQL_SQL92_VALUE_EXPRESSIONS
 *     Describes the level of support for conditional value expressions.
 *   SQL_STANDARD_CLI_CONFORMANCE
 *     Describes the driver's conformance to CLI standards.
 *   SQL_STATIC_CURSOR_ATTRIBUTES1
 *     Describes support for the first of 2 sets of cursor features for static cursors.
 *   SQL_STATIC_CURSOR_ATTRIBUTES2
 *     Describes support for the second of 2 sets of cursor features for static cursors.
 *   SQL_STRING_FUNCTIONS
 *     Describes the level of support for string functions.
 *   SQL_SUBQUERIES
 *     Describes the level of support for subqueries.
 *   SQL_SYSTEM_FUNCTIONS
 *     Describes the level of support for system functions.
 *   SQL_TABLE_TERM
 *     The term used by the server for tables.
 *   SQL_TIMEDATE_ADD_INTERVAL
 *     Describes the level of support for adding time intervals.
 *   SQL_TIMEDATE_DIFF_INTERVAL
 *     Describes the level of support for subtracting time intervals.
 *   SQL_TIMEDATE_FUNCTIONS
 *     Describes the level of support for date / time functions.
 *   SQL_TXN_CAPABLE
 *     Indicates the level of support for transactions.
 *   SQL_TXN_ISOLATION_OPTION
 *     Describes the level of support for transaction isolation levels.
 *   SQL_UNION
 *     Describes the level of support for UNION.
 *   SQL_USER_NAME
 *     The user name associated with the current databsae.
 *   SQL_XOPEN_CLI_YEAR
 *     The publication year of the Open Group spec this driver manager conforms to.
 * input - Some GetInfo types require data to be passed in through the value pointer.
 *         Those values can be passed in via this argument.
 *         If the input is an integer, it will also qualify as a length argument. To
 *         resolve this issue, supply a length argument when providing an integer as input
 *         to ensure control over the allocated size of the output.
 * length - The maximum length of returned text data.  Defaults to 255.
 *
 * Returns the value of the specified attribute.
 * Integer constants are translated into their string names.
 * Bitmask values are returned as a concatenated string of their names, separated by commas.
 * Possible return values for each attribute are:
 *   SQL_ACCESSIBLE_PROCEDURES (returns "Y" or "N")
 *   SQL_ACCESSIBLE_TABLES (returns "Y" or "N")
 *   SQL_ACTIVE_ENVIRONMENTS (returns an integer value, 0 indicates no limit)
 *   SQL_AGGREGATE_FUNCTIONS (comma separated flags)
 *     SQL_AF_ALL: All aggregate functions are supported.
 *     SQL_AF_AVG: AVG() is supported.
 *     SQL_AF_COUNT: COUNT() is supported.
 *     SQL_AF_DISTINCT: The DISTINCT modified is supported (eg COUNT(DISTINCT X))
 *     SQL_AF_MAX: MAX() is supported.
 *     SQL_AF_MIN: MIN() is supported.
 *     SQL_AF_SUM: SUM() is supported.
 *   SQL_ALTER_DOMAIN (comma separated flags)
 *     SQL_AD_ADD_DOMAIN_CONSTRAINT: The clause 'ADD DOMAIN CONSTRAINT' is supported.
 *     SQL_AD_ADD_DOMAIN_DEFAULT: The clause 'ALTER DOMAIN SET DOMAIN DEFAULT' is supported.
 *     SQL_AD_CONSTRAINT_NAME_DEFINITION: The clause 'CONSTRAINT NAME DEFINITION' is supported.
 *     SQL_AD_DROP_DOMAIN_CONSTRAINT: The clause 'DROP DOMAIN CONSTRAINT' is supported.
 *     SQL_AD_DROP_DOMAIN_DEFAULT: The clause 'ALTER DOMAIN DROP DOMAIN DEFAULT' is supported.
 *     SQL_AD_ADD_CONSTRAINT_DEFERRABLE: Domain constraints may be deferrable.
 *     SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE: Domain constraints may be non-deferrable.
 *     SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED: Domain constraints may be added in a deferred state.
 *     SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE: Domain constraints may be added in an immediate state.
 *   SQL_ALTER_TABLE (comma separated flags)
 *     SQL_AT_ADD COLUMN: The clause 'ADD COLUMN' is supported.
 *     SQL_AT_ADD_COLUMN_DEFAULT: Defaults may be specified on added columns.
 *     SQL_AT_ADD_CONSTRAINT: Constraints may be specified on added columns.
 *     SQL_AT_ADD_TABLE_CONSTRAINT: The clause 'ADD TABLE CONSTRAINT' is supported.
 *     SQL_AT_CONSTRAINT_NAME_DEFINITION: Column and table constraints may be named.
 *     SQL_AT_DROP_COLUMN_CASCADE: The clause 'DROP COLUMN CASCADE' is supported.
 *     SQL_AT_DROP_COLUMN_DEFAULT: The clause 'ALTER COLUMN DROP COLUMN DEFAULT' is supported.
 *     SQL_AT_DROP_COLUMN_RESTRICT: The clause 'DROP COLUMN RESTRICT' is supported.
 *     SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE: The clause 'DROP TABLE CONSTRAINT CASCADE' is supported.
 *     SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT: The clause 'DROP TABLE CONSTRAINT RESTRICT' is supported.
 *     SQL_AT_SET_COLUMN_DEFAULT: The clause 'ALTER COLUMN SET COLUMN DEFAULT' is supported.
 *     SQL_AT_CONSTRAINT_DEFERRABLE: Table and column constraints may be deferrable.
 *     SQL_AT_CONSTRAINT_NON_DEFERRABLE: Table and column constraints may be non-deferrable.
 *     SQL_AT_CONSTRAINT_INITIALLY_DEFERRED: Table and column constraints may be added in a deferred state.
 *     SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE: Table and column constraints may be added in an immediate state.
 *   SQL_ASYNC_MODE
 *     SQL_AM_CONNECTION: Statements on each connection must be either all synchronous or all asynchronous.
 *     SQL_AM_STATEMENT: Statements on each connection may mix between synchronous and asynchronous.
 *     SQL_AM_NONE: Asynchronous mode is not supported.
 *   SQL_ASYNC_NOTIFICATION
 *     SQL_ASYNC_NOTIFICATION_CAPABLE: Asynchronous notifications are supported.
 *     SQL_ASYNC_NOTIFICATION_NOT_CAPABLE: Asynchronous notifications are not supported.
 *   SQL_BATCH_ROW_COUNT (comma separated flags)
 *     SQL_BRC_ROLLED_UP: Row counts for consecutive DML statements are aggregated.
 *     SQL_BRC_PROCEDURES: Row counts are available (aggregated or not) from stored procedures.
 *     SQL_BRC_EXPLICIT: Row counts are available (aggregated or not) from executed SQL statements.
 *   SQL_BATCH_SUPPORT (comma separated flags)
 *     SQL_BS_SELECT_EXPLICIT: SQL statements that return result sets are supported.
 *     SQL_BS_ROW_COUNT_EXPLICIT: SQL statements that return row counts are supported.
 *     SQL_BS_SELECT_PROC: Stored procedures that return result sets are supported.
 *     SQL_BS_ROW_COUNT_PROC: Stored procedures that return row counts are supported.
 *   SQL_BOOKMARK_PERSISTENCE (comma separated flags)
 *     SQL_BP_CLOSE: Bookmarks are available after the statement or cursor handle is closed.
 *     SQL_BP_DELETE: Bookmerks are available after the bookmarked row is deleted.
 *     SQL_BP_DROP: Bookmarks are available the statement handle is dropped.
 *     SQL_BP_TRANSACTION: Bookmarks are available after the transaction is committed or rolled back.
 *     SQL_BP_UPDATE: Bookmarks are available after the bookmarked row has been updated.
 *     SQL_BP_OTHER_HSTMT: Bookmarks from one statement handle can be used on other statement handles.
 *   SQL_CATALOG_LOCATION
 *     SQL_CL_START: Catalog is at the beginning of qualified identifiers.
 *     SQL_CL_END: Catalog is at the end of qualified identifiers.
 *     SQL_CL_NOT_SUPPORTED: Catalogs are not supported.
 *   SQL_CATALOG_NAME (returns "Y" or "N")
 *   SQL_CATALOG_NAME_SEPARATOR (returns a string value, usually ".")
 *   SQL_CATALOG_TERM (returns a string value)
 *   SQL_CATALOG_USAGE (comma separated values)
 *     SQL_CU_DML_STATEMENTS: Catalogs can be specified in DML statements.
 *     SQL_CU_PROCEDURE_INVOCATION: Catalogs can be specified when executing procedures.
 *     SQL_CU_TABLE_DEFINITION: Catalogs can be specified in DDL statements.
 *     SQL_CU_INDEX_DEFINITION: Catalogs can be specified in index CREATE and DROP statements.
 *     SQL_CU_CATALOGS_NOT_SUPPORTED: Catalogs are not supported. (added in place of return value 0)
 *   SQL_COLLATION_SEQ (returns a string value)
 *   SQL_COLUMN_ALIAS (returns "Y" or "N")
 *   SQL_CONCAT_NULL_BEHAVIOR
 *     SQL_CB_NULL: Concatenation containing any NULL values returns NULL.
 *     SQL_CB_NON_NULL: Concatenation containing any NULL values treats them as zero-length strings
 *                      if there are any non-null values.
 *   SQL_CONVERT_BIGINT, SQL_CONVERT_BINARY, SQL_CONVERT_BIT, SQL_CONVERT_CHAR , SQL_CONVERT_GUID,
 *   SQL_CONVERT_DATE, SQL_CONVERT_DECIMAL, SQL_CONVERT_DOUBLE, SQL_CONVERT_FLOAT, SQL_CONVERT_INTEGER,
 *   SQL_CONVERT_INTERVAL_YEAR_MONTH, SQL_CONVERT_INTERVAL_DAY_TIME, SQL_CONVERT_LONGVARBINARY,
 *   SQL_CONVERT_LONGVARCHAR, SQL_CONVERT_NUMERIC, SQL_CONVERT_REAL, SQL_CONVERT_SMALLINT, SQL_CONVERT_TIME,
 *   SQL_CONVERT_TIMESTAMP, SQL_CONVERT_TINYINT, SQL_CONVERT_VARBINARY, SQL_CONVERT_VARCHAR
 *    (all have the same return format, which is comma separated values)
 *     SQL_CVT_BIGINT: Data type can be converted to BIGINT.
 *     SQL_CVT_BINARY: Data type can be converted to BINARY.
 *     SQL_CVT_BIT: Data type can be converted to BIT.
 *     SQL_CVT_GUID: Data type can be converted to GUID.
 *     SQL_CVT_CHAR: Data type can be converted to CHAR.
 *     SQL_CVT_DATE: Data type can be converted to DATE.
 *     SQL_CVT_DECIMAL: Data type can be converted to DECIMAL.
 *     SQL_CVT_DOUBLE: Data type can be converted to DOUBLE.
 *     SQL_CVT_FLOAT: Data type can be converted to FLOAT.
 *     SQL_CVT_INTEGER: Data type can be converted to INTEGER.
 *     SQL_CVT_INTERVAL_YEAR_MONTH: Data type can be converted to INTERVAL_YEAR_MONTH.
 *     SQL_CVT_INTERVAL_DAY_TIME: Data type can be converted to INTERVAL_DAY_TIME.
 *     SQL_CVT_LONGVARBINARY: Data type can be converted to LONGVARBINARY.
 *     SQL_CVT_LONGVARCHAR: Data type can be converted to LONGVARCHAR.
 *     SQL_CVT_NUMERIC: Data type can be converted to NUMERIC.
 *     SQL_CVT_REAL: Data type can be converted to REAL.
 *     SQL_CVT_SMALLINT: Data type can be converted to SMALLINT.
 *     SQL_CVT_TIME: Data type can be converted to TIME.
 *     SQL_CVT_TIMESTAMP: Data type can be converted to TIMESTAMP.
 *     SQL_CVT_TINYINT: Data type can be converted to TINYINT.
 *     SQL_CVT_VARBINARY: Data type can be converted to VARBINARY.
 *     SQL_CVT_VARCHAR: Data type can be converted to VARCHAR.
 *   SQL_CONVERT_FUNCTIONS (comma separated values)
 *     SQL_FN_CVT_CAST: The CAST function is supported.
 *     SQL_FN_CVT_CONVERT: The CONVERT function is supported.
 *   SQL_CORRELATION_NAME
 *     SQL_CN_NONE: Table aliases are not supported.
 *     SQL_CN_DIFFERENT: Table aliases are supported but the alias must be different from the table name.
 *     SQL_CN_ANY: Table aliases are supported even if the alias is the same as the table name.
 *   SQL_CREATE_ASSERTION (comma separated values)
 *     SQL_CA_CREATE_ASSERTION: The clause 'CREATE ASSERTION' is supported.
 *     SQL_CA_CONSTRAINT_DEFERRABLE: Assertion constraints may be deferrable.
 *     SQL_CA_CONSTRAINT_NON_DEFERRABLE: Assertion constraints may be non-deferrable.
 *     SQL_CA_CONSTRAINT_INITIALLY_DEFERRED: Assertion constraints may be added in a deferred state.
 *     SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE: Assertion constraints may be added in an immediate state.
 *     SQL_CA_ASSERTIONS_NOT_SUPPORTED: Assertions are not supported. (added in place of return value 0)
 *   SQL_CREATE_CHARACTER_SET (comma separated values)
 *     SQL_CCS_CREATE_CHARACTER_SET: The clause 'CREATE CHARACTER SET' is supported.
 *     SQL_CCS_COLLATE_CLAUSE: Created characters sets can have a defined collation mechanism.
 *     SQL_CCS_LIMITED_COLLATION: Limited collation is allowed when creating character sets.
 *     SQL_CCS_CHARACTER_SETS_NOT_SUPPORTED: Creation of character sets is not supported. (added in place of return value 0)
 *   SQL_CREATE_COLLATION (comma separated values)
 *     SQL_CCOL_CREATE_COLLATION: The clause 'CREATE COLLATION' is supported.
 *     SQL_CCOL_COLLATIONS_NOT_SUPPORTED: Creation of collations is not supported. (added in place of return value 0)
 *   SQL_CREATE_DOMAIN (comma separated values)
 *     SQL_CDO_CREATE_DOMAIN: The clause 'CREATE DOMAIN' is supported.
 *     SQL_CDO_CONSTRAINT_NAME_DEFINITION: Named domains are supported.
 *     SQL_CDO_DEFAULT: Domain defaults are supported.
 *     SQL_CDO_CONSTRAINT: Domain constraints are supported.
 *     SQL_CDO_COLLATION: Domain collation is supported.
 *     SQL_CDO_CONSTRAINT_DEFERRABLE: Domain constraints may be deferrable.
 *     SQL_CDO_CONSTRAINT_NON_DEFERRABLE: Domain constraints may be non-deferrable.
 *     SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED: Domain constraints may be added in a deferred state.
 *     SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE: Domain constraints may be added in an immediate state.
 *     SQL_CDO_DOMAINS_NOT_SUPPORTED: The clause 'CREATE DOMAIN' is not supported. (added in place of return value 0)
 *   SQL_CREATE_SCHEMA (comma separated values)
 *     SQL_CS_CREATE_SCHEMA: The clause 'CREATE SCHEMA' is supported.
 *     SQL_CS_AUTHORIZATION: This might indicate that different schemas can use different authorization, whatever that means.
 *     SQL_CS_DEFAULT_CHARACTER_SET: A default character set can be specified during schema creation.
 *   SQL_CREATE_TABLE (comma separated values)
 *     SQL_CT_CREATE_TABLE: The clause 'CREATE TABLE' is supported.
 *     SQL_CT_TABLE_CONSTRAINT: Table constraints may be specified.
 *     SQL_CT_CONSTRAINT_NAME_DEFINITION: Table and column constraints may be named.
 *     SQL_CT_COMMIT_PRESERVE: Temporary table rows may be preserved on a commit.
 *     SQL_CT_COMMIT_DELETE: Temporary table rows may be deleted on a commit. 
 *     SQL_CT_GLOBAL_TEMPORARY: Global temporary tables can be created.
 *     SQL_CT_LOCAL_TEMPORARY: Local temporary tables can be created.
 *     SQL_CT_COLUMN_CONSTRAINT: Column constraints may be specified during table creation.
 *     SQL_CT_COLUMN_DEFAULT: Column defaults may be specified during table creation.
 *     SQL_CT_COLUMN_COLLATION: Column collation may be specified during table creation.
 *     SQL_CT_CONSTRAINT_DEFERRABLE: Column or table constraints may be deferrable.
 *     SQL_CT_CONSTRAINT_NON_DEFERRABLE: Column or table constraints may be non-deferrable.
 *     SQL_CT_CONSTRAINT_INITIALLY_DEFERRED: Column or table constraints may be added in a deferred state.
 *     SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE: Column or table constraints may be added in an immediate state.
 *   SQL_CREATE_TRANSLATION (comma separated values)
 *     SQL_CTR_CREATE_TRANSLATION: The cause 'CREATE TRANSLATION' is supported.
 *     SQL_CTR_TRANSLATIONS_NOT_SUPPORTED: Translations are not supported. (added in place of return value 0)
 *   SQL_CREATE_VIEW
 *     SQL_CV_CREATE_VIEW: The clause 'CREATE VIEW' is supported.
 *     SQL_CV_CHECK_OPTION: SQL API doesn't say and I don't know what this is.
 *     SQL_CV_CASCADED: SQL API doesn't say and I don't know what this is.
 *     SQL_CV_LOCAL: SQL API doesn't say and I don't know what this is.
 *     SQL_CV_VIEWS_NOT_SUPPORTED: The clause 'CREATE VIEW' is not supported. (added in place of return value 0)
 *   SQL_CURSOR_COMMIT_BEHAVIOR
 *     SQL_CB_DELETE: Cursors and prepared statements are killed when a transaction is committed.
 *     SQL_CB_CLOSE: Cursors are closed when a transaction is committed.
 *     SQL_CB_PRESERVE: Cursors remain open when a transaction is committed.
 *   SQL_CURSOR_ROLLBACK_BEHAVIOR
 *     SQL_CB_DELETE: Cursors and prepared statements are killed when a transaction is rolled back.
 *     SQL_CB_CLOSE: Cursors are closed when a transaction is rolled back.
 *     SQL_CB_PRESERVE: Cursors remain open when a transaction is rolled back.
 *   SQL_CURSOR_SENSITIVITY
 *     SQL_INSENSITIVE: Cursors never reflect changes made by other cursors in the same transaction.
 *     SQL_UNSPECIFIED: Cursors may or may not reflect changes made by other cursors in the same transaction.
 *     SQL_SENSITIVE: Cursors always reflect changes made by other cursors in the same transaction.
 *   SQL_DATA_SOURCE_NAME (returns a string value)
 *   SQL_DATA_SOURCE_READ_ONLY (returns "Y" or "N")
 *   SQL_DATABASE_NAME (returns a string value)
 *   SQL_DATETIME_LITERALS (comma separated values)
 *     SQL_DL_SQL92_DATE: The datetime literal type DATE is supported.
 *     SQL_DL_SQL92_TIME: The datetime literal type TIME is supported.
 *     SQL_DL_SQL92_TIMESTAMP: The datetime literal type TIMESTAMP is supported.
 *     SQL_DL_SQL92_INTERVAL_YEAR: The datetime literal type INTERVAL_YEAR is supported.
 *     SQL_DL_SQL92_INTERVAL_MONTH: The datetime literal type INTERVAL_MONTH is supported.
 *     SQL_DL_SQL92_INTERVAL_DAY: The datetime literal type INTERVAL_DAY is supported.
 *     SQL_DL_SQL92_INTERVAL_HOUR: The datetime literal type INTERVAL_HOUR is supported.
 *     SQL_DL_SQL92_INTERVAL_MINUTE: The datetime literal type INTERVAL_MINUTE is supported.
 *     SQL_DL_SQL92_INTERVAL_SECOND: The datetime literal type INTERVAL_SECOND is supported.
 *     SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH: The datetime literal type INTERVAL_YEAR_TO_MONTH is supported.
 *     SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR: The datetime literal type INTERVAL_DAY_TO_HOUR is supported.
 *     SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE: The datetime literal type INTERVAL_DAY_TO_MINUTE is supported.
 *     SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND: The datetime literal type INTERVAL_DAY_TO_SECOND is supported.
 *     SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE: The datetime literal type INTERVAL_HOUR_TO_MINUTE is supported.
 *     SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND: The datetime literal type INTERVAL_HOUR_TO_SECOND is supported.
 *     SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND: The datetime literal type INTERVAL_MINUTE_TO_SECOND is supported.
 *   SQL_DBMS_NAME (returns a string value)
 *   SQL_DBMS_VER (returns a string value)
 *   SQL_DDL_INDEX (comma separated values)
 *     SQL_DI_CREATE_INDEX: The clause 'CREATE INDEX' is supported.
 *     SQL_DI_DROP_INDEX: The clause 'DROP INDEX' is supported.
 *   SQL_DEFAULT_TXN_ISOLATION
 *     SQL_TXN_READ_UNCOMMITTED: Dirty reads, nonrepeatable reads, and phantoms are possible by default.
 *     SQL_TXN_READ_COMMITTED: Nonrepeatable reads and phantoms are possible by default.
 *     SQL_TXN_REPEATABLE_READ: Phantoms are possible by default.
 *     SQL_TXN_SERIALIZABLE: Transactions are fully serializable by default.
 *   SQL_DESCRIBE_PARAMETER (returns "Y" or "N")
 *   SQL_DM_VER (returns a string value)
 *   SQL_DRIVER_HDBC (returns an external handle)
 *   SQL_DRIVER_HENV (returns an external handle)
 *   SQL_DRIVER_HDESC (returns an external handle)
 *   SQL_DRIVER_HLIB (returns an external handle)
 *   SQL_DRIVER_HSTMT (returns an external handle)
 *   SQL_DRIVER_NAME (returns a string value)
 *   SQL_DRIVER_ODBC_VER (returns a string value)
 *   SQL_DRIVER_VER (returns a string value)
 *   SQL_DROP_ASSERTION (comma separated values)
 *     SQL_DA_DROP_ASSERTION: The clause 'DROP ASSERTION' is supported.
 *   SQL_DROP_CHARACTER_SET (comma separated values)
 *     SQL_DCS_DROP_CHARACTER_SET: The clause 'DROP CHARACTER SET' is supported.
 *   SQL_DROP_COLLATION (comma separated values)
 *     SQL_DC_DROP_COLLATION: The clause 'DROP COLLATION' is supported.
 *   SQL_DROP_DOMAIN (comma separated values)
 *     SQL_DD_DROP_DOMAIN: The clause 'DROP DOMAIN' is supported.
 *     SQL_DD_CASCADE: The clause 'DROP DOMAIN CASCADE' is supported.
 *     SQL_DD_RESTRICT: The clause 'DROP DOMAIN RESTRICT' is supported.
 *   SQL_DROP_SCHEMA (comma separated values)
 *     SQL_DS_DROP_SCHEMA: The clause 'DROP SCHEMA' is supported.
 *     SQL_DS_CASCADE: The clause 'DROP SCHEMA CASCADE' is supported.
 *     SQL_DS_RESTRICT: The clause 'DROP SCHEMA RESTRICT' is supported.
 *   SQL_DROP_TABLE (comma separated values)
 *     SQL_DS_DROP_TABLE: The clause 'DROP TABLE' is supported.
 *     SQL_DS_CASCADE: The clause 'DROP TABLE CASCADE' is supported.
 *     SQL_DS_RESTRICT: The clause 'DROP TABLE RESTRICT' is supported.
 *   SQL_DROP_TRANSLATION (comma separated values)
 *     SQL_DS_DROP_TRANSLATION: The clause 'DROP TRANSLATION' is supported.
 *   SQL_DROP_VIEW (comma separated values)
 *     SQL_DS_DROP_VIEW: The clause 'DROP VIEW' is supported.
 *     SQL_DS_CASCADE: The clause 'DROP VIEW CASCADE' is supported.
 *     SQL_DS_RESTRICT: The clause 'DROP VIEW RESTRICT' is supported.
 *   SQL_DYNAMIC_CURSOR_ATTRIBUTES1 (comma separated values)
 *     SQL_CA1_NEXT: Dynamic cursors support fetching the next row.
 *     SQL_CA1_ABSOLUTE: Dynamic cursors support fetching a row from the first, last, or a specific position.
 *     SQL_CA1_RELATIVE: Dynamic cursors support fetching the prior row, or a row relative to the current position.
 *     SQL_CA1_BOOKMARK: Dynamic cursors support bookmarks.
 *     SQL_CA1_LOCK_EXCLUSIVE: Dynamic cursors support locking rows using SQLSetPos.
 *     SQL_CA1_LOCK_NO_CHANGE: Dynamic cursors support leaving row locks alone using SQLSetPos.
 *     SQL_CA1_LOCK_UNLOCK: Dynamic cursors support unolocking rows using SQLSetPos.
 *     SQL_CA1_POS_POSITION: Dynamic cursors support setting the position using SQLSetPos.
 *     SQL_CA1_POS_UPDATE: Dynamic cursors support updating using SQLSetPos.
 *     SQL_CA1_POS_DELETE: Dynamic cursors support deleting using SQLSetPos.
 *     SQL_CA1_POS_REFRESH: Dynamic cursors support refreshing data using SQLSetPos.
 *     SQL_CA1_POSITIONED_UPDATE: The clause 'UPDATE WHERE CURRENT OF' is supported on a dynamic cursor.
 *     SQL_CA1_POSITIONED_DELETE: The clause 'DELETE WHERE CURRENT OF' is supported on a dynamic cursor.
 *     SQL_CA1_SELECT_FOR_UPDATE: The clause 'SELECT FOR UPDATE' is supported on a dynamic cursor.
 *     SQL_CA1_BULK_ADD: Dynamic cursors support bulk add operations.
 *     SQL_CA1_BULK_UPDATE_BY_BOOKMARK: Dynamic cursors support bulk update by bookmark operations.
 *     SQL_CA1_BULK_DELETE_BY_BOOKMARK: Dynamic cursors support bulk delete by bookmark operations.
 *     SQL_CA1_BULK_FETCH_BY_BOOKMARK: Dynamic cursors support bulk fetch by bookmark operations.
 *   SQL_DYNAMIC_CURSOR_ATTRIBUTES2 (comma separated values)
 *     SQL_CA2_READ_ONLY_CONCURRENCY: Dynamic cursors can be read-only.
 *     SQL_CA2_LOCK_CONCURRENCY: Dynamic cursors can use locking to apply updates.
 *     SQL_CA2_OPT_ROWVER_CONCURRENCY: Dynamic cursors can compare row versions optimistically.
 *     SQL_CA2_OPT_VALUES_CONCURRENCY: Dynamic cursors can compare values optimistically.
 *     SQL_CA2_SENSITIVITY_ADDITIONS: Dynamic cursors can see added rows.
 *     SQL_CA2_SENSITIVITY_DELETIONS: Dynamic cursors are affected by row deletion.
 *     SQL_CA2_SENSITIVITY_UPDATES: Dynamic cursors can see row updates.
 *     SQL_CA2_MAX_ROWS_SELECT: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing SELECT.
 *     SQL_CA2_MAX_ROWS_INSERT: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing INSERT.
 *     SQL_CA2_MAX_ROWS_DELETE: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing DELETE.
 *     SQL_CA2_MAX_ROWS_UPDATE: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing UPDATE.
 *     SQL_CA2_MAX_ROWS_CATALOG: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing CATALOG.
 *     SQL_CA2_MAX_ROWS_AFFECTS_ALL: Dynamic cursors are subject to SQL_ATTR_MAX_ROWS when performing any operation.
 *     SQL_CA2_CRC_EXACT: Dynamic cursors have an exact row count available.
 *     SQL_CA2_CRC_APPROXIMATE: Dynamic cursors have an approximate row count available.
 *     SQL_CA2_SIMULATE_NON_UNIQUE: Dynamic cursor updates and deletes are not guaranteed to affect only one row.
 *     SQL_CA2_SIMULATE_TRY_UNIQUE: Dynamic cursor updates and deletes try to only affect one row.
 *     SQL_CA2_SIMULATE_UNIQUE: Dynamic cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_EXPRESSIONS_IN_ORDERBY (returns "Y" or "N")
 *   SQL_FILE_USAGE
 *     SQL_FILE_NOT_SUPPORTED: The driver cannot read directly from a file.
 *     SQL_FILE_TABLE: Files are treated as tables.
 *     SQL_FILE_CATALOG: Files are treated as catalogs.
 *   SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1 (comma separated values)
 *     SQL_CA1_NEXT: Forward-only cursors support fetching the next row.
 *     SQL_CA1_ABSOLUTE: Forward-only cursors support fetching a row from the first, last, or a specific position.
 *     SQL_CA1_RELATIVE: Forward-only cursors support fetching the prior row, or a row relative to the current position.
 *     SQL_CA1_BOOKMARK: Forward-only cursors support bookmarks.
 *     SQL_CA1_LOCK_EXCLUSIVE: Forward-only cursors support locking rows using SQLSetPos.
 *     SQL_CA1_LOCK_NO_CHANGE: Forward-only cursors support leaving row locks alone using SQLSetPos.
 *     SQL_CA1_LOCK_UNLOCK: Forward-only cursors support unolocking rows using SQLSetPos.
 *     SQL_CA1_POS_POSITION: Forward-only cursors support setting the position using SQLSetPos.
 *     SQL_CA1_POS_UPDATE: Forward-only cursors support updating using SQLSetPos.
 *     SQL_CA1_POS_DELETE: Forward-only cursors support deleting using SQLSetPos.
 *     SQL_CA1_POS_REFRESH: Forward-only cursors support refreshing data using SQLSetPos.
 *     SQL_CA1_POSITIONED_UPDATE: The clause 'UPDATE WHERE CURRENT OF' is supported on a forward-only cursor.
 *     SQL_CA1_POSITIONED_DELETE: The clause 'DELETE WHERE CURRENT OF' is supported on a forward-only cursor.
 *     SQL_CA1_SELECT_FOR_UPDATE: The clause 'SELECT FOR UPDATE' is supported on a forward-only cursor.
 *     SQL_CA1_BULK_ADD: Forward-only cursors support bulk add operations.
 *     SQL_CA1_BULK_UPDATE_BY_BOOKMARK: Forward-only cursors support bulk update by bookmark operations.
 *     SQL_CA1_BULK_DELETE_BY_BOOKMARK: Forward-only cursors support bulk delete by bookmark operations.
 *     SQL_CA1_BULK_FETCH_BY_BOOKMARK: Forward-only cursors support bulk fetch by bookmark operations.
 *   SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2 (comma separated values)
 *     SQL_CA2_READ_ONLY_CONCURRENCY: Forward-only cursors can be read-only.
 *     SQL_CA2_LOCK_CONCURRENCY: Forward-only cursors can use locking to apply updates.
 *     SQL_CA2_OPT_ROWVER_CONCURRENCY: Forward-only cursors can compare row versions optimistically.
 *     SQL_CA2_OPT_VALUES_CONCURRENCY: Forward-only cursors can compare values optimistically.
 *     SQL_CA2_SENSITIVITY_ADDITIONS: Forward-only cursors can see added rows.
 *     SQL_CA2_SENSITIVITY_DELETIONS: Forward-only cursors are affected by row deletion.
 *     SQL_CA2_SENSITIVITY_UPDATES: Forward-only cursors can see row updates.
 *     SQL_CA2_MAX_ROWS_SELECT: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing SELECT.
 *     SQL_CA2_MAX_ROWS_INSERT: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing INSERT.
 *     SQL_CA2_MAX_ROWS_DELETE: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing DELETE.
 *     SQL_CA2_MAX_ROWS_UPDATE: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing UPDATE.
 *     SQL_CA2_MAX_ROWS_CATALOG: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing CATALOG.
 *     SQL_CA2_MAX_ROWS_AFFECTS_ALL: Forward-only cursors are subject to SQL_ATTR_MAX_ROWS when performing any operation.
 *     SQL_CA2_CRC_EXACT: Forward-only cursors have an exact row count available.
 *     SQL_CA2_CRC_APPROXIMATE: Forward-only cursors have an approximate row count available.
 *     SQL_CA2_SIMULATE_NON_UNIQUE: Forward-only cursor updates and deletes are not guaranteed to affect only one row.
 *     SQL_CA2_SIMULATE_TRY_UNIQUE: Forward-only cursor updates and deletes try to only affect one row.
 *     SQL_CA2_SIMULATE_UNIQUE: Forward-only cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_GETDATA_EXTENSIONS (comma separated values)
 *     SQL_GD_ANY_COLUMN: SQLGetData can retrieve data for unbound columns, before or after the last bound column.
 *     SQL_GD_ANY_ORDER: SQLGetData can retrieve data for unbound columns in any order.
 *     SQL_GD_BLOCK: SQLGetData can retrieve data for other rows in the same block.
 *     SQL_GD_BOUND: SQLGetData can retrieve data for bound columns.
 *     SQL_GD_OUTPUT_PARAMS: SQLGetData can retrieve output parameter values.
 *   SQL_GROUP_BY (comma separated values)
 *     SQL_GB_COLLATE: Each grouping column may contain a COLLATE clause.
 *     SQL_GB_NOT_SUPPORTED: The clause 'GROUP BY' is not supported.
 *     SQL_GB_GROUP_BY_EQUALS_SELECT: The GROUP BY clause must match the non-aggregated columns in the SELECT clause.
 *     SQL_GB_GROUP_BY_CONTAINS_SELECT: The GROUP BY clause must contain the non-aggregated columns in the SELECT clause.
 *                                      It may also contain other columns.
 *     SQL_GB_NO_RELATION: The GROUP BY clause is not related to the SELECT clause.
 *   SQL_IDENTIFIER_CASE
 *     SQL_IC_UPPER: Idenfitiers are case insensitive and stored on the server in upper case.
 *     SQL_IC_LOWER: Idenfitiers are case insensitive and stored on the server in lower case.
 *     SQL_IC_SENSITIVE: Idenfitiers are case sensitive and stored on the server in mixed case.
 *     SQL_IC_MIXED: Idenfitiers are case insensitive and stored on the server in mixed case.
 *   SQL_IDENTIFIER_QUOTE_CHAR (returns a string value)
 *   SQL_INDEX_KEYWORDS (comma separated values)
 *     SQL_IK_NONE: No index keywords are supported.
 *     SQL_IK_ASC: The ASC keyword is supported.
 *     SQL_IK_DESC: The DESC keyword is supported.
 *     SQL_IK_ALL: All of the index keywords are supported.
 *   SQL_INFO_SCHEMA_VIEWS (comma separated values)
 *     SQL_ISV_ASSERTIONS: The INFORMATION_SCHEMA view ASSERTIONS is supported.
 *     SQL_ISV_CHARACTER_SETS: The INFORMATION_SCHEMA view CHARACTER_SETS is supported.
 *     SQL_ISV_CHECK_CONSTRAINTS: The INFORMATION_SCHEMA view CHECK_CONSTRAINTS is supported.
 *     SQL_ISV_COLLATIONS: The INFORMATION_SCHEMA view COLLATIONS is supported.
 *     SQL_ISV_COLUMN_DOMAIN_USAGE: The INFORMATION_SCHEMA view COLUMN_DOMAIN_USAGE is supported.
 *     SQL_ISV_COLUMN_PRIVILEGES: The INFORMATION_SCHEMA view COLUMN_PRIVILEGES is supported.
 *     SQL_ISV_COLUMNS: The INFORMATION_SCHEMA view COLUMNS is supported.
 *     SQL_ISV_CONSTRAINT_COLUMN_USAGE: The INFORMATION_SCHEMA view CONSTRAINT_COLUMN_USAGE is supported.
 *     SQL_ISV_CONSTRAINT_TABLE_USAGE: The INFORMATION_SCHEMA view CONSTRAINT_TABLE_USAGE is supported.
 *     SQL_ISV_DOMAIN_CONSTRAINTS: The INFORMATION_SCHEMA view DOMAIN_CONSTRAINTS is supported.
 *     SQL_ISV_DOMAINS: The INFORMATION_SCHEMA view DOMAINS is supported.
 *     SQL_ISV_KEY_COLUMN_USAGE: The INFORMATION_SCHEMA view KEY_COLUMN_USAGE is supported.
 *     SQL_ISV_REFERENTIAL_CONSTRAINTS: The INFORMATION_SCHEMA view REFERENTIAL_CONSTRAINTS is supported.
 *     SQL_ISV_SCHEMATA: The INFORMATION_SCHEMA view SCHEMATA is supported.
 *     SQL_ISV_SQL_LANGUAGES: The INFORMATION_SCHEMA view SQL_LANGUAGES is supported.
 *     SQL_ISV_TABLE_CONSTRAINTS: The INFORMATION_SCHEMA view TABLE_CONSTRAINTS is supported.
 *     SQL_ISV_TABLE_PRIVILEGES: The INFORMATION_SCHEMA view TABLE_PRIVILEGES is supported.
 *     SQL_ISV_TABLES: The INFORMATION_SCHEMA view TABLES is supported.
 *     SQL_ISV_TRANSLATIONS: The INFORMATION_SCHEMA view TRANSLATIONS is supported.
 *     SQL_ISV_USAGE_PRIVILEGES: The INFORMATION_SCHEMA view USAGE_PRIVILEGES is supported.
 *     SQL_ISV_VIEW_COLUMN_USAGE: The INFORMATION_SCHEMA view VIEW_COLUMN_USAGE is supported.
 *     SQL_ISV_VIEW_TABLE_USAGE: The INFORMATION_SCHEMA view VIEW_TABLE_USAGE is supported.
 *     SQL_ISV_VIEWS: The INFORMATION_SCHEMA view VIEWS is supported.
 *   SQL_INSERT_STATEMENT (comma separated values)
 *     SQL_IS_INSERT_LITERALS: INSERT statements can insert one row with literal values.
 *     SQL_IS_INSERT_SEARCHED: INSERT statements can insert the results of a SELECT statement.
 *     SQL_IS_SELECT_INTO: SELECT ... INTO can insert data into a table or value.
 *   SQL_INTEGRITY (returns "Y" or "N")
 *   SQL_KEYSET_CURSOR_ATTRIBUTES1 (comma separated values)
 *     SQL_CA1_NEXT: Keyset cursors support fetching the next row.
 *     SQL_CA1_ABSOLUTE: Keyset cursors support fetching a row from the first, last, or a specific position.
 *     SQL_CA1_RELATIVE: Keyset cursors support fetching the prior row, or a row relative to the current position.
 *     SQL_CA1_BOOKMARK: Keyset cursors support bookmarks.
 *     SQL_CA1_LOCK_EXCLUSIVE: Keyset cursors support locking rows using SQLSetPos.
 *     SQL_CA1_LOCK_NO_CHANGE: Keyset cursors support leaving row locks alone using SQLSetPos.
 *     SQL_CA1_LOCK_UNLOCK: Keyset cursors support unolocking rows using SQLSetPos.
 *     SQL_CA1_POS_POSITION: Keyset cursors support setting the position using SQLSetPos.
 *     SQL_CA1_POS_UPDATE: Keyset cursors support updating using SQLSetPos.
 *     SQL_CA1_POS_DELETE: Keyset cursors support deleting using SQLSetPos.
 *     SQL_CA1_POS_REFRESH: Keyset cursors support refreshing data using SQLSetPos.
 *     SQL_CA1_POSITIONED_UPDATE: The clause 'UPDATE WHERE CURRENT OF' is supported on a keyset cursor.
 *     SQL_CA1_POSITIONED_DELETE: The clause 'DELETE WHERE CURRENT OF' is supported on a keyset cursor.
 *     SQL_CA1_SELECT_FOR_UPDATE: The clause 'SELECT FOR UPDATE' is supported on a keyset cursor.
 *     SQL_CA1_BULK_ADD: Keyset cursors support bulk add operations.
 *     SQL_CA1_BULK_UPDATE_BY_BOOKMARK: Keyset cursors support bulk update by bookmark operations.
 *     SQL_CA1_BULK_DELETE_BY_BOOKMARK: Keyset cursors support bulk delete by bookmark operations.
 *     SQL_CA1_BULK_FETCH_BY_BOOKMARK: Keyset cursors support bulk fetch by bookmark operations.
 *   SQL_KEYSET_CURSOR_ATTRIBUTES2 (comma separated values)
 *     SQL_CA2_READ_ONLY_CONCURRENCY: Keyset cursors can be read-only.
 *     SQL_CA2_LOCK_CONCURRENCY: Keyset cursors can use locking to apply updates.
 *     SQL_CA2_OPT_ROWVER_CONCURRENCY: Keyset cursors can compare row versions optimistically.
 *     SQL_CA2_OPT_VALUES_CONCURRENCY: Keyset cursors can compare values optimistically.
 *     SQL_CA2_SENSITIVITY_ADDITIONS: Keyset cursors can see added rows.
 *     SQL_CA2_SENSITIVITY_DELETIONS: Keyset cursors are affected by row deletion.
 *     SQL_CA2_SENSITIVITY_UPDATES: Keyset cursors can see row updates.
 *     SQL_CA2_MAX_ROWS_SELECT: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing SELECT.
 *     SQL_CA2_MAX_ROWS_INSERT: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing INSERT.
 *     SQL_CA2_MAX_ROWS_DELETE: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing DELETE.
 *     SQL_CA2_MAX_ROWS_UPDATE: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing UPDATE.
 *     SQL_CA2_MAX_ROWS_CATALOG: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing CATALOG.
 *     SQL_CA2_MAX_ROWS_AFFECTS_ALL: Keyset cursors are subject to SQL_ATTR_MAX_ROWS when performing any operation.
 *     SQL_CA2_CRC_EXACT: Keyset cursors have an exact row count available.
 *     SQL_CA2_CRC_APPROXIMATE: Keyset cursors have an approximate row count available.
 *     SQL_CA2_SIMULATE_NON_UNIQUE: Keyset cursor updates and deletes are not guaranteed to affect only one row.
 *     SQL_CA2_SIMULATE_TRY_UNIQUE: Keyset cursor updates and deletes try to only affect one row.
 *     SQL_CA2_SIMULATE_UNIQUE: Keyset cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_KEYWORDS (comma separated values, varies by driver)
 *   SQL_LIKE_ESCAPE_CLAUSE (returns "Y" or "N")
 *   SQL_MAX_ASYNC_CONCURRENT_STATEMENTS (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_BINARY_LITERAL_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_CATALOG_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_CHAR_LITERAL_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMN_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMNS_IN_GROUP_BY (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMNS_IN_INDEX (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMNS_IN_ORDER_BY (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMNS_IN_SELECT (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_COLUMNS_IN_TABLE (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_CONCURRENT_ACTIVITIES (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_CURSOR_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_DRIVER_CONNECTIONS (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_IDENTIFIER_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_INDEX_SIZE (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_PROCEDURE_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_ROW_SIZE (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_ROW_SIZE_INCLUDES_LONG (returns "Y" or "N")
 *   SQL_MAX_SCHEMA_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_STATEMENT_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_TABLE_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_TABLES_IN_SELECT (returns an integer value, 0 indicates no limit)
 *   SQL_MAX_USER_NAME_LEN (returns an integer value, 0 indicates no limit)
 *   SQL_MULT_RESULT_SETS (returns "Y" or "N")
 *   SQL_MULTIPLE_ACTIVE_TXN (returns "Y" or "N")
 *   SQL_NEED_LONG_DATA_LEN (returns "Y" or "N")
 *   SQL_NON_NULLABLE_COLUMNS
 *     SQL_NNC_NULL: Does not support the NOT NULL constraint.
 *     SQL_NNC_NON_NULL: Supports the NOT NULL constraint.
 *   SQL_NULL_COLLATION
 *     SQL_NC_END: NULL values are always sorted at the end of an ordered set.
 *     SQL_NC_HIGH: NULL values are considered to be higher than other values in an ordered set.
 *     SQL_NC_LOW: NULL values are considered to be lower than other values in an ordered set.
 *     SQL_NC_START: NULL values are always sorted at the beginning of an ordered set.
 *   SQL_NUMERIC_FUNCTIONS (comma separated values)
 *     SQL_FN_NUM_ABS: The numeric function ABS is supported.
 *     SQL_FN_NUM_ACOS: The numeric function ACOS is supported.
 *     SQL_FN_NUM_ASIN: The numeric function ASIN is supported.
 *     SQL_FN_NUM_ATAN: The numeric function ATAN is supported.
 *     SQL_FN_NUM_ATAN2: The numeric function ATAN2 is supported.
 *     SQL_FN_NUM_CEILING: The numeric function CEILING is supported.
 *     SQL_FN_NUM_COS: The numeric function COS is supported.
 *     SQL_FN_NUM_COT: The numeric function COT is supported.
 *     SQL_FN_NUM_DEGREES: The numeric function DEGREES is supported.
 *     SQL_FN_NUM_EXP: The numeric function EXP is supported.
 *     SQL_FN_NUM_FLOOR: The numeric function FLOOR is supported.
 *     SQL_FN_NUM_LOG: The numeric function LOG is supported.
 *     SQL_FN_NUM_LOG10: The numeric function LOG10 is supported.
 *     SQL_FN_NUM_MOD: The numeric function MOD is supported.
 *     SQL_FN_NUM_PI: The numeric function PI is supported.
 *     SQL_FN_NUM_POWER: The numeric function POWER is supported.
 *     SQL_FN_NUM_RADIANS: The numeric function RADIANS is supported.
 *     SQL_FN_NUM_RAND: The numeric function RAND is supported.
 *     SQL_FN_NUM_ROUND: The numeric function ROUND is supported.
 *     SQL_FN_NUM_SIGN: The numeric function SIGN is supported.
 *     SQL_FN_NUM_SIN: The numeric function SIN is supported.
 *     SQL_FN_NUM_SQRT: The numeric function SQRT is supported.
 *     SQL_FN_NUM_TAN: The numeric function TAN is supported.
 *     SQL_FN_NUM_TRUNCATE: The numeric function TRUNCATE is supported.
 *   SQL_ODBC_INTERFACE_CONFORMANCE
 *     SQL_OIC_CORE: The driver conforms to the core ODBC standard.
 *     SQL_OIC_LEVEL1: The driver conforms to the Level 1 ODBC standard.
 *     SQL_OIC_LEVEL2: The driver conforms to the Level 2 ODBC standard.
 *   SQL_ODBC_VER (returns a string value)
 *   SQL_OJ_CAPABILITIES
 *     SQL_OJ_LEFT: Left outer joins are supported.
 *     SQL_OJ_RIGHT: Right outer joins are supported.
 *     SQL_OJ_FULL: Full outer joins are supported.
 *     SQL_OJ_NESTED: Nested outer joins are supported.
 *     SQL_OJ_NOT_ORDERED: The order of column names in the 'ON' clause does not matter.
 *     SQL_OJ_INNER = The inner table in the join can also be inner joined.
 *     SQL_OJ_ALL_COMPARISON_OPS = The join can use operators other than '=' in the ON clause.
 *   SQL_ORDER_BY_COLUMNS_IN_SELECT (returns "Y" or "N")
 *   SQL_PARAM_ARRAY_ROW_COUNTS
 *     SQL_PARC_BATCH: Individual row counts are available for parameter sets on a prepared statement.
 *     SQL_PARC_NO_BATCH: Only one row count is available for parameter sets on a prepared statement.
 *   SQL_PARAM_ARRAY_SELECTS
 *     SQL_PAS_BATCH: Statements with parameter arrays return one result set for each parameter set.
 *     SQL_PAS_NO_BATCH: Statements with parameter arrays return all results in one set.
 *     SQL_PAS_NO_SELECT: The driver does not support running a SELECT statement with multiple sets of parameters.
 *   SQL_PROCEDURE_TERM (returns a string value)
 *   SQL_PROCEDURES (returns "Y" or "N")
 *   SQL_POS_OPERATIONS (comma separated values)
 *     SQL_POS_POSITION: SQLSetPos supports setting the cursor position.
 *     SQL_POS_REFRESH: SQLSetPos supports refreshing data.
 *     SQL_POS_UPDATE: SQLSetPos supports updating data.
 *     SQL_POS_DELETE: SQLSetPos supports deleting data.
 *     SQL_POS_ADD: SQLSetPos supports inserting data.
 *   SQL_QUOTED_IDENTIFIER
 *     SQL_IC_UPPER: Quoted identifiers are case insensitive and stored as upper-case on the server.
 *     SQL_IC_LOWER: Quoted identifiers are case insensitive and stored as lower-case on the server.
 *     SQL_IC_SENSITIVE: Quoted identifiers are case sensitive and stored in mixed case on the server.
 *     SQL_IC_MIXED: Quoted identifiers are case insensitive and stored as mixed-case on the server.
 *   SQL_ROW_UPDATES (returns "Y" or "N")
 *   SQL_SCHEMA_TERM (returns a string value)
 *   SQL_SCHEMA_USAGE (comma separated values)
 *     SQL_SU_DML_STATEMENTS: Schema names can be specified in DML statements.
 *     SQL_SU_PROCEDURE_INVOCATION: Schema names can be specified when running procedures.
 *     SQL_SU_TABLE_DEFINITION: Schema names can be specified when creating tables.
 *     SQL_SU_INDEX_DEFINITION: Schema names can be specified when creating indices.
 *     SQL_SU_PRIVILEGE_DEFINITION: Schema names can be specified when granting or revoking privileges.
 *   SQL_SCROLL_OPTIONS (comma separated values)
 *     SQL_SO_FORWARD_ONLY: Forward-only scrollable cursors are supported.
 *     SQL_SO_STATIC: Static scrollable cursors are supported.
 *     SQL_SO_KEYSET_DRIVEN: Keyset driven scrollable cursors are supported.
 *     SQL_SO_DYNAMIC: Dynamic scrollable cursors are supported.
 *     SQL_SO_MIXED: Mixed-mode scrollable cursors are supported.
 *   SQL_SEARCH_PATTERN_ESCAPE (returns a string value)
 *   SQL_SERVER_NAME (returns a string value)
 *   SQL_SPECIAL_CHARACTERS (returns a string value)
 *   SQL_SQL_CONFORMANCE
 *     SQL_SC_SQL92_ENTRY: Complies with the entry-level SQL 92 standard.
 *     SQL_SC_FIPS127_2_TRANSITIONAL: Complies with the FIPS 127-2 transitional standard.
 *     SQL_SC_SQL92_FULL: Complies with the full-level SQL 92 standard.
 *     SQL_SC_SQL92_INTERMEDIATE: Complies with the intermediate-level SQL 92 standard.
 *   SQL_SQL92_DATETIME_FUNCTIONS (comma separated values)
 *     SQL_SDF_CURRENT_DATE: CURRENT_DATE is supported.
 *     SQL_SDF_CURRENT_TIME: CURRENT_TIME is supported.
 *     SQL_SDF_CURRENT_TIMESTAMP: CURRENT_TIMESTAMP is supported.
 *   SQL_SQL92_FOREIGN_KEY_DELETE_RULE (comma separated values)
 *     SQL_SFKD_CASCADE: CASCADE is supported when deleting foreign keys.
 *     SQL_SFKD_NO_ACTION: NO ACTION is supported when deleting foreign keys.
 *     SQL_SFKD_SET_DEFAULT: SET DEFAULT is supported when deleting foreign keys.
 *     SQL_SFKD_SET_NULL: SET NULL is supported when deleting foreign keys.
 *   SQL_SQL92_FOREIGN_KEY_UPDATE_RULE (comma separated values)
 *     SQL_SFKU_CASCADE: CASCADE is supported when updating foreign keys.
 *     SQL_SFKU_NO_ACTION: NO ACTION is supported when updating foreign keys.
 *     SQL_SFKU_SET_DEFAULT: SET DEFAULT is supported when updating foreign keys.
 *     SQL_SFKU_SET_NULL: SET NULL is supported when updating foreign keys.
 *   SQL_SQL92_GRANT (comma separated values)
 *     SQL_SG_DELETE_TABLE: GRANT DELETE is supported.
 *     SQL_SG_INSERT_COLUMN: GRANT INSERT is supported at the column level.
 *     SQL_SG_INSERT_TABLE: GRANT INSERT is supported at the table level.
 *     SQL_SG_REFERENCES_TABLE: GRANT REFERENCES is supported at the table level.
 *     SQL_SG_REFERENCES_COLUMN: GRANT REFERENCES is supported at the column level.
 *     SQL_SG_SELECT_TABLE: GRANT SELECT is supported at the table level.
 *     SQL_SG_UPDATE_COLUMN: GRANT UPDATE is supported at the column level.
 *     SQL_SG_UPDATE_TABLE: GRANT UPDATE is supported at the table level.
 *     SQL_SG_USAGE_ON_DOMAIN: Access to domains can be GRANTed.
 *     SQL_SG_USAGE_ON_CHARACTER_SET: Access to character sets can be GRANTed.
 *     SQL_SG_USAGE_ON_COLLATION: Access to collations can be GRANTed.
 *     SQL_SG_USAGE_ON_TRANSLATION: Access to translations can be GRANTed.
 *     SQL_SG_WITH_GRANT_OPTION: Permission to GRANT permissions can be GRANTed.
 *   SQL_SQL92_NUMERIC_VALUE_FUNCTIONS (comma separated values)
 *     SQL_SNVF_BIT_LENGTH: The BIT_LENGTH function is supported.
 *     SQL_SNVF_CHAR_LENGTH: The CHAR_LENGTH function is supported.
 *     SQL_SNVF_CHARACTER_LENGTH: The CHARACTER_LENGTH function is supported.
 *     SQL_SNVF_EXTRACT: The EXTRACT function is supported.
 *     SQL_SNVF_OCTET_LENGTH: The OCTET_LENGTH function is supported.
 *     SQL_SNVF_POSITION: The POSITION function is supported.
 *   SQL_SQL92_PREDICATES (comma separated values)
 *     SQL_SP_BETWEEN: BETWEEN is a supported boolean operator in SELECT statements.
 *     SQL_SP_COMPARISON: SELECT statements supports comparison operators.
 *     SQL_SP_EXISTS: EXISTS is a supported boolean operator in SELECT statements.
 *     SQL_SP_IN: IN is a supported boolean operator in SELECT statements.
 *     SQL_SP_ISNOTNULL: IS NOT NULL is a supported boolean operator in SELECT statements.
 *     SQL_SP_ISNULL: IS NULL is a supported boolean operator in SELECT statements.
 *     SQL_SP_LIKE: LIKE is a supported boolean operator in SELECT statements.
 *     SQL_SP_MATCH_FULL: MATCH_FULL is a supported boolean operator in SELECT statements.
 *     SQL_SP_MATCH_PARTIAL(Full: MATCH_PARTIAL(Full is a supported boolean operator in SELECT statements.
 *     SQL_SP_MATCH_UNIQUE_FULL: MATCH_UNIQUE_FULL is a supported boolean operator in SELECT statements.
 *     SQL_SP_MATCH_UNIQUE_PARTIAL: MATCH_UNIQUE_PARTIAL is a supported boolean operator in SELECT statements.
 *     SQL_SP_OVERLAPS: OVERLAPS is a supported boolean operator in SELECT statements.
 *     SQL_SP_QUANTIFIED_COMPARISON: SELECT statements support quantified comparisons.
 *     SQL_SP_UNIQUE: UNIQUE is a supported boolean operator in SELECT statements.
 *   SQL_SQL92_RELATIONAL_JOIN_OPERATORS
 *     SQL_SRJO_CORRESPONDING_CLAUSE: Joins support CORRESPONDING clauses.
 *     SQL_SRJO_CROSS_JOIN: CROSS JOIN is a supported type of join.
 *     SQL_SRJO_EXCEPT_JOIN: EXCEPT JOIN is a supported type of join.
 *     SQL_SRJO_FULL_OUTER_JOIN: FULL OUTER JOIN is a supported type of join.
 *     SQL_SRJO_INNER_JOIN: INNER JOIN is a supported type of join.
 *     SQL_SRJO_INTERSECT_JOIN: INTERSECT JOIN is a supported type of join.
 *     SQL_SRJO_LEFT_OUTER_JOIN: LEFT OUTER JOIN is a supported type of join.
 *     SQL_SRJO_NATURAL_JOIN: NATURAL JOIN is a supported type of join.
 *     SQL_SRJO_RIGHT_OUTER_JOIN: RIGHT OUTER JOIN is a supported type of join.
 *     SQL_SRJO_UNION_JOIN: UNION JOIN is a supported type of join.
 *   SQL_SQL92_REVOKE (comma separated values)
 *     SQL_SR_CASCADE: The 'CASCADE' clause is supported when revoking permissions.
 *     SQL_SR_DELETE_TABLE: REVOKE DELETE is supported.
 *     SQL_SR_GRANT_OPTION_FOR: Permission to GRANT permissions can be revoked.
 *     SQL_SR_INSERT_COLUMN: REVOKE INSERT is supported at the column level.
 *     SQL_SR_INSERT_TABLE: REVOKE INSERT is supported at the table level.
 *     SQL_SR_REFERENCES_COLUMN: REVOKE REFERENCES is supported at the column level.
 *     SQL_SR_REFERENCES_TABLE: REVOKE REFERENCES is supported at the table level.
 *     SQL_SR_RESTRICT: The 'RESTRICT' clause is supported. (?)
 *     SQL_SR_SELECT_TABLE: REVOKE SELECT is supported at the table level.
 *     SQL_SR_UPDATE_COLUMN: REVOKE UPDATE is supported at the column level.
 *     SQL_SR_UPDATE_TABLE: REVOKE UPDATE is supported at the table level.
 *     SQL_SR_USAGE_ON_DOMAIN: Access to domains can be revoked.
 *     SQL_SR_USAGE_ON_CHARACTER_SET: Access to character sets can be revoked.
 *     SQL_SR_USAGE_ON_COLLATION: Access to collations can be revoked.
 *     SQL_SR_USAGE_ON_TRANSLATION: Access to translations can be revoked.
 *   SQL_SQL92_ROW_VALUE_CONSTRUCTOR (comma separated values)
 *     SQL_SRVC_VALUE_EXPRESSION: Expressions are allowed in the SELECT clause.
 *     SQL_SRVC_NULL: NULL is supported in the SELECT clause.
 *     SQL_SRVC_DEFAULT: DEFAULT is supported in the SELECT clause.
 *     SQL_SRVC_ROW_SUBQUERY: Subqueries are allowed in the SELECT clause.
 *   SQL_SQL92_STRING_FUNCTIONS (comma separated values)
 *     SQL_SSF_CONVERT: CONVERT is a supported string function.
 *     SQL_SSF_LOWER: LOWER is a supported string function.
 *     SQL_SSF_UPPER: UPPER is a supported string function.
 *     SQL_SSF_SUBSTRING: SUBSTRING is a supported string function.
 *     SQL_SSF_TRANSLATE: TRANSLATE is a supported string function.
 *     SQL_SSF_TRIM_BOTH: TRIM BOTH is a supported string function.
 *     SQL_SSF_TRIM_LEADING: TRIM LEADING is a supported string function.
 *     SQL_SSF_TRIM_TRAILING: TRIM TRAILING is a supported string function.
 *   SQL_SQL92_VALUE_EXPRESSIONS (comma separated values)
 *     SQL_SVE_CASE: CASE is supported in expressions.
 *     SQL_SVE_CAST: CAST is supported in expressions.
 *     SQL_SVE_COALESCE: COALESCE is supported in expressions.
 *     SQL_SVE_NULLIF: NULLIF is supported in expressions.
 *   SQL_STANDARD_CLI_CONFORMANCE (comma separated values)
 *     SQL_SCC_XOPEN_CLI_VERSION1: The driver conforms to the Open Group CLI version 1.
 *     SQL_SCC_ISO92_CLI: The driver conforms to the ISO 92 CLI.
 *   SQL_STATIC_CURSOR_ATTRIBUTES1 (comma separated values)
 *     SQL_CA1_NEXT: Static cursors support fetching the next row.
 *     SQL_CA1_ABSOLUTE: Static cursors support fetching a row from the first, last, or a specific position.
 *     SQL_CA1_RELATIVE: Static cursors support fetching the prior row, or a row relative to the current position.
 *     SQL_CA1_BOOKMARK: Static cursors support bookmarks.
 *     SQL_CA1_LOCK_EXCLUSIVE: Static cursors support locking rows using SQLSetPos.
 *     SQL_CA1_LOCK_NO_CHANGE: Static cursors support leaving row locks alone using SQLSetPos.
 *     SQL_CA1_LOCK_UNLOCK: Static cursors support unolocking rows using SQLSetPos.
 *     SQL_CA1_POS_POSITION: Static cursors support setting the position using SQLSetPos.
 *     SQL_CA1_POS_UPDATE: Static cursors support updating using SQLSetPos.
 *     SQL_CA1_POS_DELETE: Static cursors support deleting using SQLSetPos.
 *     SQL_CA1_POS_REFRESH: Static cursors support refreshing data using SQLSetPos.
 *     SQL_CA1_POSITIONED_UPDATE: The clause 'UPDATE WHERE CURRENT OF' is supported on a static cursor.
 *     SQL_CA1_POSITIONED_DELETE: The clause 'DELETE WHERE CURRENT OF' is supported on a static cursor.
 *     SQL_CA1_SELECT_FOR_UPDATE: The clause 'SELECT FOR UPDATE' is supported on a static cursor.
 *     SQL_CA1_BULK_ADD: Static cursors support bulk add operations.
 *     SQL_CA1_BULK_UPDATE_BY_BOOKMARK: Static cursors support bulk update by bookmark operations.
 *     SQL_CA1_BULK_DELETE_BY_BOOKMARK: Static cursors support bulk delete by bookmark operations.
 *     SQL_CA1_BULK_FETCH_BY_BOOKMARK: Static cursors support bulk fetch by bookmark operations.
 *   SQL_STATIC_CURSOR_ATTRIBUTES2 (comma separated values)
 *     SQL_CA2_READ_ONLY_CONCURRENCY: Static cursors can be read-only.
 *     SQL_CA2_LOCK_CONCURRENCY: Static cursors can use locking to apply updates.
 *     SQL_CA2_OPT_ROWVER_CONCURRENCY: Static cursors can compare row versions optimistically.
 *     SQL_CA2_OPT_VALUES_CONCURRENCY: Static cursors can compare values optimistically.
 *     SQL_CA2_SENSITIVITY_ADDITIONS: Static cursors can see added rows.
 *     SQL_CA2_SENSITIVITY_DELETIONS: Static cursors are affected by row deletion.
 *     SQL_CA2_SENSITIVITY_UPDATES: Static cursors can see row updates.
 *     SQL_CA2_MAX_ROWS_SELECT: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing SELECT.
 *     SQL_CA2_MAX_ROWS_INSERT: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing INSERT.
 *     SQL_CA2_MAX_ROWS_DELETE: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing DELETE.
 *     SQL_CA2_MAX_ROWS_UPDATE: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing UPDATE.
 *     SQL_CA2_MAX_ROWS_CATALOG: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing CATALOG.
 *     SQL_CA2_MAX_ROWS_AFFECTS_ALL: Static cursors are subject to SQL_ATTR_MAX_ROWS when performing any operation.
 *     SQL_CA2_CRC_EXACT: Static cursors have an exact row count available.
 *     SQL_CA2_CRC_APPROXIMATE: Static cursors have an approximate row count available.
 *     SQL_CA2_SIMULATE_NON_UNIQUE: Static cursor updates and deletes are not guaranteed to affect only one row.
 *     SQL_CA2_SIMULATE_TRY_UNIQUE: Static cursor updates and deletes try to only affect one row.
 *     SQL_CA2_SIMULATE_UNIQUE: Static cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_STRING_FUNCTIONS (comma separated values)
 *     SQL_FN_STR_ASCII: The string function ASCII is supported.
 *     SQL_FN_STR_BIT_LENGTH: The string function BIT_LENGTH is supported.
 *     SQL_FN_STR_CHAR: The string function CHAR is supported.
 *     SQL_FN_STR_CHAR_LENGTH: The string function CHAR_LENGTH is supported.
 *     SQL_FN_STR_CHARACTER_LENGTH: The string function CHARACTER_LENGTH is supported.
 *     SQL_FN_STR_CONCAT: The string function CONCAT is supported.
 *     SQL_FN_STR_DIFFERENCE: The string function DIFFERENCE is supported.
 *     SQL_FN_STR_INSERT: The string function INSERT is supported.
 *     SQL_FN_STR_LCASE: The string function LCASE is supported.
 *     SQL_FN_STR_LEFT: The string function LEFT is supported.
 *     SQL_FN_STR_LENGTH: The string function LENGTH is supported.
 *     SQL_FN_STR_LOCATE: The string function LOCATE is supported.
 *     SQL_FN_STR_LTRIM: The string function LTRIM is supported.
 *     SQL_FN_STR_OCTET_LENGTH: The string function OCTET_LENGTH is supported.
 *     SQL_FN_STR_POSITION: The string function POSITION is supported.
 *     SQL_FN_STR_REPEAT: The string function REPEAT is supported.
 *     SQL_FN_STR_REPLACE: The string function REPLACE is supported.
 *     SQL_FN_STR_RIGHT: The string function RIGHT is supported.
 *     SQL_FN_STR_RTRIM: The string function RTRIM is supported.
 *     SQL_FN_STR_SOUNDEX: The string function SOUNDEX is supported.
 *     SQL_FN_STR_SPACE: The string function SPACE is supported.
 *     SQL_FN_STR_SUBSTRING: The string function SUBSTRING is supported.
 *     SQL_FN_STR_UCASE: The string function UCASE is supported.
 *   SQL_SUBQUERIES (comma separated values)
 *     SQL_SQ_CORRELATED_SUBQUERIES: Correlated subqueries are supported.
 *     SQL_SQ_COMPARISON: Comparison subqueries are supported.
 *     SQL_SQ_EXISTS: EXISTS subqueries are supported.
 *     SQL_SQ_IN: IN subqueries are supported.
 *     SQL_SQ_QUANTIFIED: Quantified subqueries are supported.
 *   SQL_SYSTEM_FUNCTIONS (comma separated values)
 *     SQL_FN_SYS_DBNAME: The system function DBNAME is supported.
 *     SQL_FN_SYS_IFNULL: The system function IFNULL is supported.
 *     SQL_FN_SYS_USERNAME: The system function USERNAME is supported.
 *   SQL_TABLE_TERM (returns a string value)
 *   SQL_TIMEDATE_ADD_INTERVAL (comma separated values)
 *     SQL_FN_TSI_FRAC_SECOND: Fractional SECOND intervals can be added.
 *     SQL_FN_TSI_SECOND: SECOND intervals can be added.
 *     SQL_FN_TSI_MINUTE: MINUTE intervals can be added.
 *     SQL_FN_TSI_HOUR: HOUR intervals can be added.
 *     SQL_FN_TSI_DAY: DAY intervals can be added.
 *     SQL_FN_TSI_WEEK: WEEK intervals can be added.
 *     SQL_FN_TSI_MONTH: MONTH intervals can be added.
 *     SQL_FN_TSI_QUARTER: QUARTER intervals can be added.
 *     SQL_FN_TSI_YEAR: YEAR intervals can be added.
 *   SQL_TIMEDATE_DIFF_INTERVAL (comma separated values)
 *     SQL_FN_TSI_FRAC_SECOND: Fractional SECOND intervals can be subtracted.
 *     SQL_FN_TSI_SECOND: SECOND intervals can be subtracted.
 *     SQL_FN_TSI_MINUTE: MINUTE intervals can be subtracted.
 *     SQL_FN_TSI_HOUR: HOUR intervals can be subtracted.
 *     SQL_FN_TSI_DAY: DAY intervals can be subtracted.
 *     SQL_FN_TSI_WEEK: WEEK intervals can be subtracted.
 *     SQL_FN_TSI_MONTH: MONTH intervals can be subtracted.
 *     SQL_FN_TSI_QUARTER: QUARTER intervals can be subtracted.
 *     SQL_FN_TSI_YEAR: YEAR intervals can be subtracted.
 *   SQL_FN_TIMEDATE_FUNCTIONS (comma separated values)
 *     SQL_FN_TD_CURRENT_DATE: The time / date function CURRENT_DATE is supported.
 *     SQL_FN_TD_CURRENT_TIME: The time / date function CURRENT_TIME is supported.
 *     SQL_FN_TD_CURRENT_TIMESTAMP: The time / date function CURRENT_TIMESTAMP is supported.
 *     SQL_FN_TD_CURDATE: The time / date function CURDATE is supported.
 *     SQL_FN_TD_CURTIME: The time / date function CURTIME is supported.
 *     SQL_FN_TD_DAYNAME: The time / date function DAYNAME is supported.
 *     SQL_FN_TD_DAYOFMONTH: The time / date function DAYOFMONTH is supported.
 *     SQL_FN_TD_DAYOFWEEK: The time / date function DAYOFWEEK is supported.
 *     SQL_FN_TD_DAYOFYEAR: The time / date function DAYOFYEAR is supported.
 *     SQL_FN_TD_EXTRACT: The time / date function EXTRACT is supported.
 *     SQL_FN_TD_HOUR: The time / date function HOUR is supported.
 *     SQL_FN_TD_MINUTE: The time / date function MINUTE is supported.
 *     SQL_FN_TD_MONTH: The time / date function MONTH is supported.
 *     SQL_FN_TD_MONTHNAME: The time / date function MONTHNAME is supported.
 *     SQL_FN_TD_NOW: The time / date function NOW is supported.
 *     SQL_FN_TD_QUARTER: The time / date function QUARTER is supported.
 *     SQL_FN_TD_SECOND: The time / date function SECOND is supported.
 *     SQL_FN_TD_TIMESTAMPADD: The time / date function TIMESTAMPADD is supported.
 *     SQL_FN_TD_TIMESTAMPDIFF: The time / date function TIMESTAMPDIFF is supported.
 *     SQL_FN_TD_WEEK: The time / date function WEEK is supported.
 *     SQL_FN_TD_YEAR: The time / date function YEAR is supported.
 *   SQL_TXN_CAPABLE
 *     SQL_TC_NONE: Transactions are not supported.
 *     SQL_TC_DML: Transactions may contain only DML statements.
 *     SQL_TC_DDL_COMMIT: Transactions will commit when encountering DDL statements.
 *     SQL_TC_DDL_IGNORE: Transactions will ignore DDL statements.
 *     SQL_TC_ALL: Transactions may contain DML and DDL statements.
 *   SQL_TXN_ISOLATION_OPTION (comma separated values)
 *     SQL_TXN_READ_UNCOMMITTED: The server supports transactions that allow dirty reads, nonrepeatable reads, and phantoms.
 *     SQL_TXN_READ_COMMITTED: The server supports transactions that allow nonrepeatable reads and phantoms.
 *     SQL_TXN_REPEATABLE_READ: The server supports transactions that allow only phantoms.
 *     SQL_TXN_SERIALIZABLE: The server supports fully serializable transactions.
 *   SQL_UNION (comma separated values)
 *     SQL_U_UNION: UNION is supported.
 *     SQL_U_UNION_ALL: UNION ALL is supported.
 *   SQL_USER_NAME (returns a string value)
 *   SLQ_XOPEN_CLI_YEAR (returns a string value)
 */
Handle<Value> ndbcSQLGetInfo(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLUSMALLINT attrType;
  SQLPOINTER attrVal;
  SQLSMALLINT valLen;
  SQLSMALLINT strLenPtr;
  char* listVal;
  bool ok1 = true;
  bool ok2 = true;

  /* Allocate return buffer.
   */
  if (args.Length() == 4) {
    valLen = (SQLSMALLINT) args[3]->Uint32Value();
  } else if (args.Length() == 3) {
    if (args[2]->IsNumber()) {
      valLen = (SQLSMALLINT) args[2]->Uint32Value();
    }
  } else {
    valLen = 255;
  }
  attrVal = (SQLPOINTER) malloc(valLen + 1);
  /* Translate string inputs into constant values.
   */
  if (args[1]->ToString() == ndbcSQL_ACCESSIBLE_PROCEDURES) {
    attrType = SQL_ACCESSIBLE_PROCEDURES;
  } else if (args[1]->ToString() == ndbcSQL_ACCESSIBLE_TABLES) {
    attrType = SQL_ACCESSIBLE_TABLES;
  } else if (args[1]->ToString() == ndbcSQL_ACTIVE_ENVIRONMENTS) {
    attrType = SQL_ACTIVE_ENVIRONMENTS;
  } else if (args[1]->ToString() == ndbcSQL_AGGREGATE_FUNCTIONS) {
    attrType = SQL_AGGREGATE_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_ALTER_DOMAIN) {
    attrType = SQL_ALTER_DOMAIN;
  } else if (args[1]->ToString() == ndbcSQL_ALTER_TABLE) {
    attrType = SQL_ALTER_TABLE;
  } else if (args[1]->ToString() == ndbcSQL_ASYNC_MODE) {
    attrType = SQL_ASYNC_MODE;
  } else if (args[1]->ToString() == ndbcSQL_BATCH_ROW_COUNT) {
    attrType = SQL_BATCH_ROW_COUNT;
  } else if (args[1]->ToString() == ndbcSQL_BATCH_SUPPORT) {
    attrType = SQL_BATCH_SUPPORT;
  } else if (args[1]->ToString() == ndbcSQL_BOOKMARK_PERSISTENCE) {
    attrType = SQL_BOOKMARK_PERSISTENCE;
  } else if (args[1]->ToString() == ndbcSQL_CATALOG_LOCATION) {
    attrType = SQL_CATALOG_LOCATION;
  } else if (args[1]->ToString() == ndbcSQL_CATALOG_NAME) {
    attrType = SQL_CATALOG_NAME;
  } else if (args[1]->ToString() == ndbcSQL_CATALOG_NAME_SEPARATOR) {
    attrType = SQL_CATALOG_NAME_SEPARATOR;
  } else if (args[1]->ToString() == ndbcSQL_CATALOG_TERM) {
    attrType = SQL_CATALOG_TERM;
  } else if (args[1]->ToString() == ndbcSQL_CATALOG_USAGE) {
    attrType = SQL_CATALOG_USAGE;
  } else if (args[1]->ToString() == ndbcSQL_COLLATION_SEQ) {
    attrType = SQL_COLLATION_SEQ;
  } else if (args[1]->ToString() == ndbcSQL_COLUMN_ALIAS) {
    attrType = SQL_COLUMN_ALIAS;
  } else if (args[1]->ToString() == ndbcSQL_CONCAT_NULL_BEHAVIOR) {
    attrType = SQL_CONCAT_NULL_BEHAVIOR;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_BIGINT) {
    attrType = SQL_CONVERT_BIGINT;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_BINARY) {
    attrType = SQL_CONVERT_BINARY;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_BIT) {
    attrType = SQL_CONVERT_BIT;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_CHAR) {
    attrType = SQL_CONVERT_CHAR;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_GUID) {
    attrType = SQL_CONVERT_GUID;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_DATE) {
    attrType = SQL_CONVERT_DATE;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_DECIMAL) {
    attrType = SQL_CONVERT_DECIMAL;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_DOUBLE) {
    attrType = SQL_CONVERT_DOUBLE;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_FLOAT) {
    attrType = SQL_CONVERT_FLOAT;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_INTEGER) {
    attrType = SQL_CONVERT_INTEGER;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_INTERVAL_YEAR_MONTH) {
    attrType = SQL_CONVERT_INTERVAL_YEAR_MONTH;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_INTERVAL_DAY_TIME) {
    attrType = SQL_CONVERT_INTERVAL_DAY_TIME;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_LONGVARBINARY) {
    attrType = SQL_CONVERT_LONGVARBINARY;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_LONGVARCHAR) {
    attrType = SQL_CONVERT_LONGVARCHAR;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_NUMERIC) {
    attrType = SQL_CONVERT_NUMERIC;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_REAL) {
    attrType = SQL_CONVERT_REAL;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_SMALLINT) {
    attrType = SQL_CONVERT_SMALLINT;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_TIME) {
    attrType = SQL_CONVERT_TIME;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_TIMESTAMP) {
    attrType = SQL_CONVERT_TIMESTAMP;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_TINYINT) {
    attrType = SQL_CONVERT_TINYINT;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_VARBINARY) {
    attrType = SQL_CONVERT_VARBINARY;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_VARCHAR) {
    attrType = SQL_CONVERT_VARCHAR;
  } else if (args[1]->ToString() == ndbcSQL_CONVERT_FUNCTIONS) {
    attrType = SQL_CONVERT_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_CORRELATION_NAME) {
    attrType = SQL_CORRELATION_NAME;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_ASSERTION) {
    attrType = SQL_CREATE_ASSERTION;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_CHARACTER_SET) {
    attrType = SQL_CREATE_CHARACTER_SET;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_COLLATION) {
    attrType = SQL_CREATE_COLLATION;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_DOMAIN) {
    attrType = SQL_CREATE_DOMAIN;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_SCHEMA) {
    attrType = SQL_CREATE_SCHEMA;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_TABLE) {
    attrType = SQL_CREATE_TABLE;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_TRANSLATION) {
    attrType = SQL_CREATE_TRANSLATION;
  } else if (args[1]->ToString() == ndbcSQL_CREATE_VIEW) {
    attrType = SQL_CREATE_VIEW;
  } else if (args[1]->ToString() == ndbcSQL_CURSOR_COMMIT_BEHAVIOR) {
    attrType = SQL_CURSOR_COMMIT_BEHAVIOR;
  } else if (args[1]->ToString() == ndbcSQL_CURSOR_ROLLBACK_BEHAVIOR) {
    attrType = SQL_CURSOR_ROLLBACK_BEHAVIOR;
  } else if (args[1]->ToString() == ndbcSQL_CURSOR_SENSITIVITY) {
    attrType = SQL_CURSOR_SENSITIVITY;
  } else if (args[1]->ToString() == ndbcSQL_DATA_SOURCE_NAME) {
    attrType = SQL_DATA_SOURCE_NAME;
  } else if (args[1]->ToString() == ndbcSQL_DATA_SOURCE_READ_ONLY) {
    attrType = SQL_DATA_SOURCE_READ_ONLY;
  } else if (args[1]->ToString() == ndbcSQL_DATABASE_NAME) {
    attrType = SQL_DATABASE_NAME;
  } else if (args[1]->ToString() == ndbcSQL_DATETIME_LITERALS) {
    attrType = SQL_DATETIME_LITERALS;
  } else if (args[1]->ToString() == ndbcSQL_DBMS_NAME) {
    attrType = SQL_DBMS_NAME;
  } else if (args[1]->ToString() == ndbcSQL_DBMS_VER) {
    attrType = SQL_DBMS_VER;
  } else if (args[1]->ToString() == ndbcSQL_DDL_INDEX) {
    attrType = SQL_DDL_INDEX;
  } else if (args[1]->ToString() == ndbcSQL_DEFAULT_TXN_ISOLATION) {
    attrType = SQL_DEFAULT_TXN_ISOLATION;
  } else if (args[1]->ToString() == ndbcSQL_DESCRIBE_PARAMETER) {
    attrType = SQL_DESCRIBE_PARAMETER;
  } else if (args[1]->ToString() == ndbcSQL_DM_VER) {
    attrType = SQL_DM_VER;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_HDBC) {
    attrType = SQL_DRIVER_HDBC;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_HENV) {
    attrType = SQL_DRIVER_HENV;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_HDESC) {
    attrType = SQL_DRIVER_HDESC;
    *(SQLHANDLE*)attrVal = (SQLHANDLE) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_HLIB) {
    attrType = SQL_DRIVER_HLIB;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_HSTMT) {
    attrType = SQL_DRIVER_HSTMT;
    *(SQLHANDLE*)attrVal = (SQLHANDLE) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_NAME) {
    attrType = SQL_DRIVER_NAME;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_ODBC_VER) {
    attrType = SQL_DRIVER_ODBC_VER;
  } else if (args[1]->ToString() == ndbcSQL_DRIVER_VER) {
    attrType = SQL_DRIVER_VER;
  } else if (args[1]->ToString() == ndbcSQL_DROP_ASSERTION) {
    attrType = SQL_DROP_ASSERTION;
  } else if (args[1]->ToString() == ndbcSQL_DROP_CHARACTER_SET) {
    attrType = SQL_DROP_CHARACTER_SET;
  } else if (args[1]->ToString() == ndbcSQL_DROP_COLLATION) {
    attrType = SQL_DROP_COLLATION;
  } else if (args[1]->ToString() == ndbcSQL_DROP_DOMAIN) {
    attrType = SQL_DROP_DOMAIN;
  } else if (args[1]->ToString() == ndbcSQL_DROP_SCHEMA) {
    attrType = SQL_DROP_SCHEMA;
  } else if (args[1]->ToString() == ndbcSQL_DROP_TABLE) {
    attrType = SQL_DROP_TABLE;
  } else if (args[1]->ToString() == ndbcSQL_DROP_TRANSLATION) {
    attrType = SQL_DROP_TRANSLATION;
  } else if (args[1]->ToString() == ndbcSQL_DROP_VIEW) {
    attrType = SQL_DROP_VIEW;
  } else if (args[1]->ToString() == ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES1) {
    attrType = SQL_DYNAMIC_CURSOR_ATTRIBUTES1;
  } else if (args[1]->ToString() == ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES2) {
    attrType = SQL_DYNAMIC_CURSOR_ATTRIBUTES2;
  } else if (args[1]->ToString() == ndbcSQL_EXPRESSIONS_IN_ORDERBY) {
    attrType = SQL_EXPRESSIONS_IN_ORDERBY;
  } else if (args[1]->ToString() == ndbcSQL_FILE_USAGE) {
    attrType = SQL_FILE_USAGE;
  } else if (args[1]->ToString() == ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1) {
    attrType = SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1;
  } else if (args[1]->ToString() == ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2) {
    attrType = SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2;
  } else if (args[1]->ToString() == ndbcSQL_GETDATA_EXTENSIONS) {
    attrType = SQL_GETDATA_EXTENSIONS;
  } else if (args[1]->ToString() == ndbcSQL_GROUP_BY) {
    attrType = SQL_GROUP_BY;
  } else if (args[1]->ToString() == ndbcSQL_IDENTIFIER_CASE) {
    attrType = SQL_IDENTIFIER_CASE;
  } else if (args[1]->ToString() == ndbcSQL_IDENTIFIER_QUOTE_CHAR) {
    attrType = SQL_IDENTIFIER_QUOTE_CHAR;
  } else if (args[1]->ToString() == ndbcSQL_INDEX_KEYWORDS) {
    attrType = SQL_INDEX_KEYWORDS;
  } else if (args[1]->ToString() == ndbcSQL_INFO_SCHEMA_VIEWS) {
    attrType = SQL_INFO_SCHEMA_VIEWS;
  } else if (args[1]->ToString() == ndbcSQL_INSERT_STATEMENT) {
    attrType = SQL_INSERT_STATEMENT;
  } else if (args[1]->ToString() == ndbcSQL_INTEGRITY) {
    attrType = SQL_INTEGRITY;
  } else if (args[1]->ToString() == ndbcSQL_KEYSET_CURSOR_ATTRIBUTES1) {
    attrType = SQL_KEYSET_CURSOR_ATTRIBUTES1;
  } else if (args[1]->ToString() == ndbcSQL_KEYSET_CURSOR_ATTRIBUTES2) {
    attrType = SQL_KEYSET_CURSOR_ATTRIBUTES2;
  } else if (args[1]->ToString() == ndbcSQL_KEYWORDS) {
    attrType = SQL_KEYWORDS;
  } else if (args[1]->ToString() == ndbcSQL_LIKE_ESCAPE_CLAUSE) {
    attrType = SQL_LIKE_ESCAPE_CLAUSE;
  } else if (args[1]->ToString() == ndbcSQL_MAX_ASYNC_CONCURRENT_STATEMENTS) {
    attrType = SQL_MAX_ASYNC_CONCURRENT_STATEMENTS;
  } else if (args[1]->ToString() == ndbcSQL_MAX_BINARY_LITERAL_LEN) {
    attrType = SQL_MAX_BINARY_LITERAL_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_CATALOG_NAME_LEN) {
    attrType = SQL_MAX_CATALOG_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_CHAR_LITERAL_LEN) {
    attrType = SQL_MAX_CHAR_LITERAL_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMN_NAME_LEN) {
    attrType = SQL_MAX_COLUMN_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMNS_IN_GROUP_BY) {
    attrType = SQL_MAX_COLUMNS_IN_GROUP_BY;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMNS_IN_INDEX) {
    attrType = SQL_MAX_COLUMNS_IN_INDEX;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMNS_IN_ORDER_BY) {
    attrType = SQL_MAX_COLUMNS_IN_ORDER_BY;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMNS_IN_SELECT) {
    attrType = SQL_MAX_COLUMNS_IN_SELECT;
  } else if (args[1]->ToString() == ndbcSQL_MAX_COLUMNS_IN_TABLE) {
    attrType = SQL_MAX_COLUMNS_IN_TABLE;
  } else if (args[1]->ToString() == ndbcSQL_MAX_CONCURRENT_ACTIVITIES) {
    attrType = SQL_MAX_CONCURRENT_ACTIVITIES;
  } else if (args[1]->ToString() == ndbcSQL_MAX_CURSOR_NAME_LEN) {
    attrType = SQL_MAX_CURSOR_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_DRIVER_CONNECTIONS) {
    attrType = SQL_MAX_DRIVER_CONNECTIONS;
  } else if (args[1]->ToString() == ndbcSQL_MAX_IDENTIFIER_LEN) {
    attrType = SQL_MAX_IDENTIFIER_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_INDEX_SIZE) {
    attrType = SQL_MAX_INDEX_SIZE;
  } else if (args[1]->ToString() == ndbcSQL_MAX_PROCEDURE_NAME_LEN) {
    attrType = SQL_MAX_PROCEDURE_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_ROW_SIZE) {
    attrType = SQL_MAX_ROW_SIZE;
  } else if (args[1]->ToString() == ndbcSQL_MAX_ROW_SIZE_INCLUDES_LONG) {
    attrType = SQL_MAX_ROW_SIZE_INCLUDES_LONG;
  } else if (args[1]->ToString() == ndbcSQL_MAX_SCHEMA_NAME_LEN) {
    attrType = SQL_MAX_SCHEMA_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_STATEMENT_LEN) {
    attrType = SQL_MAX_STATEMENT_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_TABLE_NAME_LEN) {
    attrType = SQL_MAX_TABLE_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MAX_TABLES_IN_SELECT) {
    attrType = SQL_MAX_TABLES_IN_SELECT;
  } else if (args[1]->ToString() == ndbcSQL_MAX_USER_NAME_LEN) {
    attrType = SQL_MAX_USER_NAME_LEN;
  } else if (args[1]->ToString() == ndbcSQL_MULT_RESULT_SETS) {
    attrType = SQL_MULT_RESULT_SETS;
  } else if (args[1]->ToString() == ndbcSQL_MULTIPLE_ACTIVE_TXN) {
    attrType = SQL_MULTIPLE_ACTIVE_TXN;
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok1 = false;
  }
  if (args[1]->ToString() == ndbcSQL_NEED_LONG_DATA_LEN) {
    attrType = SQL_NEED_LONG_DATA_LEN;
  } else if (args[1]->ToString() == ndbcSQL_NON_NULLABLE_COLUMNS) {
    attrType = SQL_NON_NULLABLE_COLUMNS;
  } else if (args[1]->ToString() == ndbcSQL_NULL_COLLATION) {
    attrType = SQL_NULL_COLLATION;
  } else if (args[1]->ToString() == ndbcSQL_NUMERIC_FUNCTIONS) {
    attrType = SQL_NUMERIC_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_ODBC_INTERFACE_CONFORMANCE) {
    attrType = SQL_ODBC_INTERFACE_CONFORMANCE;
  } else if (args[1]->ToString() == ndbcSQL_ODBC_VER) {
    attrType = SQL_ODBC_VER;
  } else if (args[1]->ToString() == ndbcSQL_OJ_CAPABILITIES) {
    attrType = SQL_OJ_CAPABILITIES;
  } else if (args[1]->ToString() == ndbcSQL_ORDER_BY_COLUMNS_IN_SELECT) {
    attrType = SQL_ORDER_BY_COLUMNS_IN_SELECT;
  } else if (args[1]->ToString() == ndbcSQL_PARAM_ARRAY_ROW_COUNTS) {
    attrType = SQL_PARAM_ARRAY_ROW_COUNTS;
  } else if (args[1]->ToString() == ndbcSQL_PARAM_ARRAY_SELECTS) {
    attrType = SQL_PARAM_ARRAY_SELECTS;
  } else if (args[1]->ToString() == ndbcSQL_PROCEDURE_TERM) {
    attrType = SQL_PROCEDURE_TERM;
  } else if (args[1]->ToString() == ndbcSQL_PROCEDURES) {
    attrType = SQL_PROCEDURES;
  } else if (args[1]->ToString() == ndbcSQL_POS_OPERATIONS) {
    attrType = SQL_POS_OPERATIONS;
  } else if (args[1]->ToString() == ndbcSQL_QUOTED_IDENTIFIER_CASE) {
    attrType = SQL_QUOTED_IDENTIFIER_CASE;
  } else if (args[1]->ToString() == ndbcSQL_ROW_UPDATES) {
    attrType = SQL_ROW_UPDATES;
  } else if (args[1]->ToString() == ndbcSQL_SCHEMA_TERM) {
    attrType = SQL_SCHEMA_TERM;
  } else if (args[1]->ToString() == ndbcSQL_SCHEMA_USAGE) {
    attrType = SQL_SCHEMA_USAGE;
  } else if (args[1]->ToString() == ndbcSQL_SCROLL_OPTIONS) {
    attrType = SQL_SCROLL_OPTIONS;
  } else if (args[1]->ToString() == ndbcSQL_SEARCH_PATTERN_ESCAPE) {
    attrType = SQL_SEARCH_PATTERN_ESCAPE;
  } else if (args[1]->ToString() == ndbcSQL_SERVER_NAME) {
    attrType = SQL_SERVER_NAME;
  } else if (args[1]->ToString() == ndbcSQL_SPECIAL_CHARACTERS) {
    attrType = SQL_SPECIAL_CHARACTERS;
  } else if (args[1]->ToString() == ndbcSQL_SQL_CONFORMANCE) {
    attrType = SQL_SQL_CONFORMANCE;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_DATETIME_FUNCTIONS) {
    attrType = SQL_SQL92_DATETIME_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_FOREIGN_KEY_DELETE_RULE) {
    attrType = SQL_SQL92_FOREIGN_KEY_DELETE_RULE;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_FOREIGN_KEY_UPDATE_RULE) {
    attrType = SQL_SQL92_FOREIGN_KEY_UPDATE_RULE;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_GRANT) {
    attrType = SQL_SQL92_GRANT;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_NUMERIC_VALUE_FUNCTIONS) {
    attrType = SQL_SQL92_NUMERIC_VALUE_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_PREDICATES) {
    attrType = SQL_SQL92_PREDICATES;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_RELATIONAL_JOIN_OPERATORS) {
    attrType = SQL_SQL92_RELATIONAL_JOIN_OPERATORS;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_REVOKE) {
    attrType = SQL_SQL92_REVOKE;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_ROW_VALUE_CONSTRUCTOR) {
    attrType = SQL_SQL92_ROW_VALUE_CONSTRUCTOR;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_STRING_FUNCTIONS) {
    attrType = SQL_SQL92_STRING_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_SQL92_VALUE_EXPRESSIONS) {
    attrType = SQL_SQL92_VALUE_EXPRESSIONS;
  } else if (args[1]->ToString() == ndbcSQL_STANDARD_CLI_CONFORMANCE) {
    attrType = SQL_STANDARD_CLI_CONFORMANCE;
  } else if (args[1]->ToString() == ndbcSQL_STATIC_CURSOR_ATTRIBUTES1) {
    attrType = SQL_STATIC_CURSOR_ATTRIBUTES1;
  } else if (args[1]->ToString() == ndbcSQL_STATIC_CURSOR_ATTRIBUTES2) {
    attrType = SQL_STATIC_CURSOR_ATTRIBUTES2;
  } else if (args[1]->ToString() == ndbcSQL_STRING_FUNCTIONS) {
    attrType = SQL_STRING_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_SUBQUERIES) {
    attrType = SQL_SUBQUERIES;
  } else if (args[1]->ToString() == ndbcSQL_SYSTEM_FUNCTIONS) {
    attrType = SQL_SYSTEM_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_TABLE_TERM) {
    attrType = SQL_TABLE_TERM;
  } else if (args[1]->ToString() == ndbcSQL_TIMEDATE_ADD_INTERVALS) {
    attrType = SQL_TIMEDATE_ADD_INTERVALS;
  } else if (args[1]->ToString() == ndbcSQL_TIMEDATE_DIFF_INTERVALS) {
    attrType = SQL_TIMEDATE_DIFF_INTERVALS;
  } else if (args[1]->ToString() == ndbcSQL_TIMEDATE_FUNCTIONS) {
    attrType = SQL_TIMEDATE_FUNCTIONS;
  } else if (args[1]->ToString() == ndbcSQL_TXN_CAPABLE) {
    attrType = SQL_TXN_CAPABLE;
  } else if (args[1]->ToString() == ndbcSQL_TXN_ISOLATION_OPTION) {
    attrType = SQL_TXN_ISOLATION_OPTION;
  } else if (args[1]->ToString() == ndbcSQL_UNION) {
    attrType = SQL_UNION;
  } else if (args[1]->ToString() == ndbcSQL_USER_NAME) {
    attrType = SQL_USER_NAME;
  } else if (args[1]->ToString() == ndbcSQL_XOPEN_CLI_YEAR) {
    attrType = SQL_XOPEN_CLI_YEAR;
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok2 = false;
  }
  if (ok1 || ok2) {
    switch (SQLGetInfo((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen, &strLenPtr)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      /* Translate constant values into string outputs.
       */
      switch (attrType) {
      case SQL_ACCESSIBLE_PROCEDURES:
      case SQL_ACCESSIBLE_TABLES:
      case SQL_CATALOG_NAME:
      case SQL_CATALOG_NAME_SEPARATOR:
      case SQL_CATALOG_TERM:
      case SQL_COLLATION_SEQ:
      case SQL_COLUMN_ALIAS:
      case SQL_DATA_SOURCE_NAME:
      case SQL_DATA_SOURCE_READ_ONLY:
      case SQL_DATABASE_NAME:
      case SQL_DBMS_NAME:
      case SQL_DBMS_VER:
      case SQL_DESCRIBE_PARAMETER:
      case SQL_DM_VER:
      case SQL_DRIVER_NAME:
      case SQL_DRIVER_ODBC_VER:
      case SQL_DRIVER_VER:
      case SQL_EXPRESSIONS_IN_ORDERBY:
      case SQL_IDENTIFIER_QUOTE_CHAR:
      case SQL_INTEGRITY:
      case SQL_KEYWORDS:
      case SQL_LIKE_ESCAPE_CLAUSE:
      case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
      case SQL_MULT_RESULT_SETS:
      case SQL_MULTIPLE_ACTIVE_TXN:
      case SQL_NEED_LONG_DATA_LEN:
      case SQL_ODBC_VER:
      case SQL_ORDER_BY_COLUMNS_IN_SELECT:
      case SQL_PROCEDURE_TERM:
      case SQL_PROCEDURES:
      case SQL_ROW_UPDATES:
      case SQL_SCHEMA_TERM:
      case SQL_SEARCH_PATTERN_ESCAPE:
      case SQL_SERVER_NAME:
      case SQL_SPECIAL_CHARACTERS:
      case SQL_TABLE_TERM:
      case SQL_USER_NAME:
      case SQL_XOPEN_CLI_YEAR:
        retVal = String::New((char*) attrVal);
        break;
      case SQL_DRIVER_HDBC:
      case SQL_DRIVER_HENV:
      case SQL_DRIVER_HDESC:
      case SQL_DRIVER_HLIB:
      case SQL_DRIVER_HSTMT:
        retVal = External::Wrap((void*) *(SQLHANDLE*)attrVal);
        break;
      case SQL_ACTIVE_ENVIRONMENTS:
      case SQL_MAX_CATALOG_NAME_LEN:
      case SQL_MAX_COLUMN_NAME_LEN:
      case SQL_MAX_COLUMNS_IN_GROUP_BY:
      case SQL_MAX_COLUMNS_IN_INDEX:
      case SQL_MAX_COLUMNS_IN_ORDER_BY:
      case SQL_MAX_COLUMNS_IN_SELECT:
      case SQL_MAX_COLUMNS_IN_TABLE:
      case SQL_MAX_CONCURRENT_ACTIVITIES:
      case SQL_MAX_CURSOR_NAME_LEN:
      case SQL_MAX_DRIVER_CONNECTIONS:
      case SQL_MAX_IDENTIFIER_LEN:
      case SQL_MAX_PROCEDURE_NAME_LEN:
      case SQL_MAX_SCHEMA_NAME_LEN:
      case SQL_MAX_TABLE_NAME_LEN:
      case SQL_MAX_TABLES_IN_SELECT:
      case SQL_MAX_USER_NAME_LEN:
        retVal = Integer::NewFromUnsigned(*(SQLUSMALLINT*)attrVal);
        break;
      case SQL_MAX_ASYNC_CONCURRENT_STATEMENTS:
      case SQL_MAX_BINARY_LITERAL_LEN:
      case SQL_MAX_CHAR_LITERAL_LEN:
      case SQL_MAX_INDEX_SIZE:
      case SQL_MAX_ROW_SIZE:
      case SQL_MAX_STATEMENT_LEN:
        retVal = Integer::NewFromUnsigned(*(SQLUINTEGER*)attrVal);
        break;
      case SQL_AGGREGATE_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_ALL) == SQL_AF_ALL) {
          strcat(listVal, "SQL_AF_ALL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_AVG) == SQL_AF_AVG) {
          strcat(listVal, "SQL_AF_AVG,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_COUNT) == SQL_AF_COUNT) {
          strcat(listVal, "SQL_AF_COUNT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_DISTINCT) == SQL_AF_DISTINCT) {
          strcat(listVal, "SQL_AF_DISTINCT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_MAX) == SQL_AF_MAX) {
          strcat(listVal, "SQL_AF_MAX,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_MIN) == SQL_AF_MIN) {
          strcat(listVal, "SQL_AF_MIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AF_SUM) == SQL_AF_SUM) {
          strcat(listVal, "SQL_AF_SUM,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_ALTER_DOMAIN:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_DOMAIN_CONSTRAINT) == SQL_AD_ADD_DOMAIN_CONSTRAINT) {
          strcat(listVal, "SQL_AD_ADD_DOMAIN_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_DOMAIN_DEFAULT) == SQL_AD_ADD_DOMAIN_DEFAULT) {
          strcat(listVal, "SQL_AD_ADD_DOMAIN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_CONSTRAINT_NAME_DEFINITION) == SQL_AD_CONSTRAINT_NAME_DEFINITION) {
          strcat(listVal, "SQL_AD_CONSTRAINT_NAME_DEFINITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_DROP_DOMAIN_CONSTRAINT) == SQL_AD_DROP_DOMAIN_CONSTRAINT) {
          strcat(listVal, "SQL_AD_DROP_DOMAIN_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_DROP_DOMAIN_DEFAULT) == SQL_AD_DROP_DOMAIN_DEFAULT) {
          strcat(listVal, "SQL_AD_DROP_DOMAIN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_CONSTRAINT_DEFERRABLE) == SQL_AD_ADD_CONSTRAINT_DEFERRABLE) {
          strcat(listVal, "SQL_AD_ADD_CONSTRAINT_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE) == SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE) {
          strcat(listVal, "SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED) == SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED) {
          strcat(listVal, "SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE) == SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE) {
          strcat(listVal, "SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_ALTER_TABLE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_ADD_COLUMN_COLLATION) == SQL_AT_ADD_COLUMN_COLLATION) {
          strcat(listVal, "SQL_AT_ADD_COLUMN_COLLATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_ADD_COLUMN_DEFAULT) == SQL_AT_ADD_COLUMN_DEFAULT) {
          strcat(listVal, "SQL_AT_ADD_COLUMN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_ADD_COLUMN_SINGLE) == SQL_AT_ADD_COLUMN_SINGLE) {
          strcat(listVal, "SQL_AT_ADD_COLUMN_SINGLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_ADD_CONSTRAINT) == SQL_AT_ADD_CONSTRAINT) {
          strcat(listVal, "SQL_AT_ADD_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_ADD_TABLE_CONSTRAINT) == SQL_AT_ADD_CONSTRAINT) {
          strcat(listVal, "SQL_AT_ADD_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_CONSTRAINT_NAME_DEFINITION) == SQL_AT_CONSTRAINT_NAME_DEFINITION) {
          strcat(listVal, "SQL_AT_CONSTRAINT_NAME_DEFINITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_DROP_COLUMN_CASCADE) == SQL_AT_DROP_COLUMN_CASCADE) {
          strcat(listVal, "SQL_AT_DROP_COLUMN_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_DROP_COLUMN_DEFAULT) == SQL_AT_DROP_COLUMN_DEFAULT) {
          strcat(listVal, "SQL_AT_DROP_COLUMN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_DROP_COLUMN_RESTRICT) == SQL_AT_DROP_COLUMN_RESTRICT) {
          strcat(listVal, "SQL_AT_DROP_COLUMN_RESTRICT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE) == SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE) {
          strcat(listVal, "SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT) == SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT) {
          strcat(listVal, "SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_SET_COLUMN_DEFAULT) == SQL_AT_SET_COLUMN_DEFAULT) {
          strcat(listVal, "SQL_AT_SET_COLUMN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_CONSTRAINT_DEFERRABLE) == SQL_AT_CONSTRAINT_DEFERRABLE) {
          strcat(listVal, "SQL_AT_CONSTRAINT_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_CONSTRAINT_NON_DEFERRABLE) == SQL_AT_CONSTRAINT_NON_DEFERRABLE) {
          strcat(listVal, "SQL_AT_CONSTRAINT_NON_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_CONSTRAINT_INITIALLY_DEFERRED) == SQL_AT_CONSTRAINT_INITIALLY_DEFERRED) {
          strcat(listVal, "SQL_AT_CONSTRAINT_INITIALLY_DEFERRED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE) == SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE) {
          strcat(listVal, "SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_ASYNC_MODE:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_AM_CONNECTION:
          retVal = ndbcSQL_AM_CONNECTION;
          break;
        case SQL_AM_STATEMENT:
          retVal = ndbcSQL_AM_STATEMENT;
          break;
        case SQL_AM_NONE:
          retVal = ndbcSQL_AM_NONE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_BATCH_ROW_COUNT:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_BRC_ROLLED_UP) == SQL_BRC_ROLLED_UP) {
          strcat(listVal, "SQL_BRC_ROLLED_UP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BRC_PROCEDURES) == SQL_BRC_PROCEDURES) {
          strcat(listVal, "SQL_BRC_PROCEDURES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BRC_EXPLICIT) == SQL_BRC_EXPLICIT) {
          strcat(listVal, "SQL_BRC_EXPLICIT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_BATCH_SUPPORT:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_BS_SELECT_EXPLICIT) == SQL_BS_SELECT_EXPLICIT) {
          strcat(listVal, "SQL_BS_SELECT_EXPLICIT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BS_ROW_COUNT_EXPLICIT) == SQL_BS_ROW_COUNT_EXPLICIT) {
          strcat(listVal, "SQL_BS_ROW_COUNT_EXPLICIT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BS_SELECT_PROC) == SQL_BS_SELECT_PROC) {
          strcat(listVal, "SQL_BS_SELECT_PROC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BS_ROW_COUNT_PROC) == SQL_BS_ROW_COUNT_PROC) {
          strcat(listVal, "SQL_BS_ROW_COUNT_PROC,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_BOOKMARK_PERSISTENCE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_CLOSE) == SQL_BP_CLOSE) {
          strcat(listVal, "SQL_BP_CLOSE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_DELETE) == SQL_BP_DELETE) {
          strcat(listVal, "SQL_BP_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_DROP) == SQL_BP_DROP) {
          strcat(listVal, "SQL_BP_DROP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_TRANSACTION) == SQL_BP_TRANSACTION) {
          strcat(listVal, "SQL_BP_TRANSACTION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_UPDATE) == SQL_BP_UPDATE) {
          strcat(listVal, "SQL_BP_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_BP_OTHER_HSTMT) == SQL_BP_OTHER_HSTMT) {
          strcat(listVal, "SQL_BP_OTHER_HSTMT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CATALOG_LOCATION:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_CL_START:
          retVal = ndbcSQL_CL_START;
          break;
        case SQL_CL_END:
          retVal = ndbcSQL_CL_END;
          break;
        case 0:
          retVal = ndbcSQL_CL_NOT_SUPPORTED;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_CATALOG_USAGE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CU_CATALOGS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CU_DML_STATEMENTS) == SQL_CU_DML_STATEMENTS) {
            strcat(listVal, "SQL_CU_DML_STATEMENTS,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CU_PROCEDURE_INVOCATION) == SQL_CU_PROCEDURE_INVOCATION) {
            strcat(listVal, "SQL_CU_PROCEDURE_INVOCATION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CU_TABLE_DEFINITION) == SQL_CU_TABLE_DEFINITION) {
            strcat(listVal, "SQL_CU_TABLE_DEFINITION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CU_INDEX_DEFINITION) == SQL_CU_INDEX_DEFINITION) {
            strcat(listVal, "SQL_CU_INDEX_DEFINITION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CU_PRIVILEGE_DEFINITION) == SQL_CU_PRIVILEGE_DEFINITION) {
            strcat(listVal, "SQL_CU_PRIVILEGE_DEFINITION,");
          }
          if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CONCAT_NULL_BEHAVIOR:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_CB_NULL:
          retVal = ndbcSQL_CB_NULL;
          break;
        case SQL_CB_NON_NULL:
          retVal = ndbcSQL_CB_NON_NULL;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_CONVERT_BIGINT:
      case SQL_CONVERT_BINARY:
      case SQL_CONVERT_BIT:
      case SQL_CONVERT_CHAR:
      case SQL_CONVERT_GUID:
      case SQL_CONVERT_DATE:
      case SQL_CONVERT_DECIMAL:
      case SQL_CONVERT_DOUBLE:
      case SQL_CONVERT_FLOAT:
      case SQL_CONVERT_INTEGER:
      case SQL_CONVERT_INTERVAL_YEAR_MONTH:
      case SQL_CONVERT_INTERVAL_DAY_TIME:
      case SQL_CONVERT_LONGVARBINARY:
      case SQL_CONVERT_LONGVARCHAR:
      case SQL_CONVERT_NUMERIC:
      case SQL_CONVERT_REAL:
      case SQL_CONVERT_SMALLINT:
      case SQL_CONVERT_TIME:
      case SQL_CONVERT_TIMESTAMP:
      case SQL_CONVERT_TINYINT:
      case SQL_CONVERT_VARBINARY:
      case SQL_CONVERT_VARCHAR:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_BIGINT) == SQL_CVT_BIGINT) {
          strcat(listVal, "SQL_CVT_BIGINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_BINARY) == SQL_CVT_BINARY) {
          strcat(listVal, "SQL_CVT_BINARY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_BIT) == SQL_CVT_BIT) {
          strcat(listVal, "SQL_CVT_BIT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_GUID) == SQL_CVT_GUID) {
          strcat(listVal, "SQL_CVT_GUID,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_CHAR) == SQL_CVT_CHAR) {
          strcat(listVal, "SQL_CVT_CHAR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_DATE) == SQL_CVT_DATE) {
          strcat(listVal, "SQL_CVT_DATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_DECIMAL) == SQL_CVT_DECIMAL) {
          strcat(listVal, "SQL_CVT_DECIMAL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_DOUBLE) == SQL_CVT_DOUBLE) {
          strcat(listVal, "SQL_CVT_DOUBLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_FLOAT) == SQL_CVT_FLOAT) {
          strcat(listVal, "SQL_CVT_FLOAT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_INTEGER) == SQL_CVT_INTEGER) {
          strcat(listVal, "SQL_CVT_INTEGER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_INTERVAL_YEAR_MONTH) == SQL_CVT_INTERVAL_YEAR_MONTH) {
          strcat(listVal, "SQL_CVT_INTERVAL_YEAR_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_INTERVAL_DAY_TIME) == SQL_CVT_INTERVAL_DAY_TIME) {
          strcat(listVal, "SQL_CVT_INTERVAL_DAY_TIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_LONGVARBINARY) == SQL_CVT_LONGVARBINARY) {
          strcat(listVal, "SQL_CVT_LONGVARBINARY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_LONGVARCHAR) == SQL_CVT_LONGVARCHAR) {
          strcat(listVal, "SQL_CVT_LONGVARCHAR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_NUMERIC) == SQL_CVT_NUMERIC) {
          strcat(listVal, "SQL_CVT_NUMERIC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_REAL) == SQL_CVT_REAL) {
          strcat(listVal, "SQL_CVT_REAL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_SMALLINT) == SQL_CVT_SMALLINT) {
          strcat(listVal, "SQL_CVT_SMALLINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_TIME) == SQL_CVT_TIME) {
          strcat(listVal, "SQL_CVT_TIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_TIMESTAMP) == SQL_CVT_TIMESTAMP) {
          strcat(listVal, "SQL_CVT_TIMESTAMP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_TINYINT) == SQL_CVT_TINYINT) {
          strcat(listVal, "SQL_CVT_TINYINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_VARBINARY) == SQL_CVT_VARBINARY) {
          strcat(listVal, "SQL_CVT_VARBINARY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CVT_VARCHAR) == SQL_CVT_VARCHAR) {
          strcat(listVal, "SQL_CVT_VARCHAR,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CONVERT_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_CVT_CAST) == SQL_FN_CVT_CAST) {
          strcat(listVal, "SQL_FN_CVT_CAST,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_CVT_CONVERT) == SQL_FN_CVT_CONVERT) {
          strcat(listVal, "SQL_FN_CVT_CONVERT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CORRELATION_NAME:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_CN_NONE:
          retVal = ndbcSQL_CN_NONE;
          break;
        case SQL_CN_DIFFERENT:
          retVal = ndbcSQL_CN_DIFFERENT;
          break;
        case SQL_CN_ANY:
          retVal = ndbcSQL_CN_ANY;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_CREATE_ASSERTION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CA_ASSERTIONS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CA_CREATE_ASSERTION) == SQL_CA_CREATE_ASSERTION) {
            strcat(listVal, "SQL_CA_CREATE_ASSERTION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CA_CONSTRAINT_DEFERRABLE) == SQL_CA_CONSTRAINT_DEFERRABLE) {
            strcat(listVal, "SQL_CA_CONSTRAINT_DEFERRABLE,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CA_CONSTRAINT_NON_DEFERRABLE) == SQL_CA_CONSTRAINT_NON_DEFERRABLE) {
            strcat(listVal, "SQL_CA_CONSTRAINT_NON_DEFERRABLE,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CA_CONSTRAINT_INITIALLY_DEFERRED) == SQL_CA_CONSTRAINT_INITIALLY_DEFERRED) {
            strcat(listVal, "SQL_CA_CONSTRAINT_INITIALLY_DEFERRED,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE) == SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE) {
            strcat(listVal, "SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE,");
          }
          if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_CHARACTER_SET:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CCS_CHARACTER_SETS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CCS_CREATE_CHARACTER_SET) == SQL_CCS_CREATE_CHARACTER_SET) {
            strcat(listVal, "SQL_CCS_CREATE_CHARACTER_SET,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CCS_COLLATE_CLAUSE) == SQL_CCS_COLLATE_CLAUSE) {
            strcat(listVal, "SQL_CCS_COLLATE_CLAUSE,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CCS_LIMITED_COLLATION) == SQL_CCS_LIMITED_COLLATION) {
            strcat(listVal, "SQL_CCS_LIMITED_COLLATION,");
          }
         if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_COLLATION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CCOL_COLLATIONS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CCOL_CREATE_COLLATION) == SQL_CCOL_CREATE_COLLATION) {
            strcat(listVal, "SQL_CCOL_CREATE_COLLATION,");
          }
         if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_DOMAIN:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CDO_DOMAINS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CREATE_DOMAIN) == SQL_CDO_CREATE_DOMAIN) {
            strcat(listVal, "SQL_CDO_CREATE_DOMAIN,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CONSTRAINT_NAME_DEFINITION) == SQL_CDO_CONSTRAINT_NAME_DEFINITION) {
            strcat(listVal, "SQL_CDO_CONSTRAINT_NAME_DEFINITION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CONSTRAINT_DEFERRABLE) == SQL_CDO_CONSTRAINT_DEFERRABLE) {
            strcat(listVal, "SQL_CDO_CONSTRAINT_DEFERRABLE,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CONSTRAINT_NON_DEFERRABLE) == SQL_CDO_CONSTRAINT_NON_DEFERRABLE) {
            strcat(listVal, "SQL_CDO_CONSTRAINT_NON_DEFERRABLE,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED) == SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED) {
            strcat(listVal, "SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE) == SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE) {
            strcat(listVal, "SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE,");
          }
         if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_SCHEMA:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_CS_CREATE_SCHEMA) == SQL_CS_CREATE_SCHEMA) {
          strcat(listVal, "SQL_CS_CREATE_SCHEMA,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CS_AUTHORIZATION) == SQL_CS_AUTHORIZATION) {
          strcat(listVal, "SQL_CS_AUTHORIZATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CS_DEFAULT_CHARACTER_SET) == SQL_CS_DEFAULT_CHARACTER_SET) {
          strcat(listVal, "SQL_CS_DEFAULT_CHARACTER_SET,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_TABLE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CREATE_TABLE) == SQL_CT_CREATE_TABLE) {
          strcat(listVal, "SQL_CT_CREATE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_TABLE_CONSTRAINT) == SQL_CT_TABLE_CONSTRAINT) {
          strcat(listVal, "SQL_CT_TABLE_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CONSTRAINT_NAME_DEFINITION) == SQL_CT_CONSTRAINT_NAME_DEFINITION) {
          strcat(listVal, "SQL_CT_CONSTRAINT_NAME_DEFINITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_COMMIT_PRESERVE) == SQL_CT_COMMIT_PRESERVE) {
          strcat(listVal, "SQL_CT_COMMIT_PRESERVE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_COMMIT_DELETE) == SQL_CT_COMMIT_DELETE) {
          strcat(listVal, "SQL_CT_COMMIT_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_GLOBAL_TEMPORARY) == SQL_CT_GLOBAL_TEMPORARY) {
          strcat(listVal, "SQL_CT_GLOBAL_TEMPORARY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_LOCAL_TEMPORARY) == SQL_CT_LOCAL_TEMPORARY) {
          strcat(listVal, "SQL_CT_LOCAL_TEMPORARY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_COLUMN_CONSTRAINT) == SQL_CT_COLUMN_CONSTRAINT) {
          strcat(listVal, "SQL_CT_COLUMN_CONSTRAINT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_COLUMN_DEFAULT) == SQL_CT_COLUMN_DEFAULT) {
          strcat(listVal, "SQL_CT_COLUMN_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_COLUMN_COLLATION) == SQL_CT_COLUMN_COLLATION) {
          strcat(listVal, "SQL_CT_COLUMN_COLLATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CONSTRAINT_DEFERRABLE) == SQL_CT_CONSTRAINT_DEFERRABLE) {
          strcat(listVal, "SQL_CT_CONSTRAINT_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CONSTRAINT_NON_DEFERRABLE) == SQL_CT_CONSTRAINT_NON_DEFERRABLE) {
          strcat(listVal, "SQL_CT_CONSTRAINT_NON_DEFERRABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CONSTRAINT_INITIALLY_DEFERRED) == SQL_CT_CONSTRAINT_INITIALLY_DEFERRED) {
          strcat(listVal, "SQL_CT_CONSTRAINT_INITIALLY_DEFERRED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE) == SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE) {
          strcat(listVal, "SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_TRANSLATION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CTR_TRANSLATIONS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CTR_CREATE_TRANSLATION) == SQL_CTR_CREATE_TRANSLATION) {
            strcat(listVal, "SQL_CTR_CREATE_TRANSLATION,");
          }
         if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CREATE_VIEW:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if (*(SQLUINTEGER*)attrVal == 0) {
          strcat(listVal, "SQL_CV_VIEWS_NOT_SUPPORTED");
        } else {
          if ((*(SQLUINTEGER*)attrVal & SQL_CV_CREATE_VIEW) == SQL_CV_CREATE_VIEW) {
            strcat(listVal, "SQL_CV_CREATE_VIEW,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CV_CHECK_OPTION) == SQL_CV_CHECK_OPTION) {
            strcat(listVal, "SQL_CV_CHECK_OPTION,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CV_CASCADED) == SQL_CV_CASCADED) {
            strcat(listVal, "SQL_CV_CASCADED,");
          }
          if ((*(SQLUINTEGER*)attrVal & SQL_CV_LOCAL) == SQL_CV_LOCAL) {
            strcat(listVal, "SQL_CV_LOCAL,");
          }
         if (strlen(listVal) > 0) {
            listVal[strlen(listVal) - 1] = NULL;
          }
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_CURSOR_COMMIT_BEHAVIOR:
      case SQL_CURSOR_ROLLBACK_BEHAVIOR:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_CB_DELETE:
          retVal = ndbcSQL_CB_DELETE;
          break;
        case SQL_CB_CLOSE:
          retVal = ndbcSQL_CB_CLOSE;
          break;
        case SQL_CB_PRESERVE:
          retVal = ndbcSQL_CB_PRESERVE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_CURSOR_SENSITIVITY:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_INSENSITIVE:
          retVal = ndbcSQL_INSENSITIVE;
          break;
        case SQL_UNSPECIFIED:
          retVal = ndbcSQL_UNSPECIFIED;
          break;
        case SQL_SENSITIVE:
          retVal = ndbcSQL_SENSITIVE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_DATETIME_LITERALS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_DATE) == SQL_DL_SQL92_DATE) {
          strcat(listVal, "SQL_DL_SQL92_DATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_TIME) == SQL_DL_SQL92_TIME) {
          strcat(listVal, "SQL_DL_SQL92_TIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_TIMESTAMP) == SQL_DL_SQL92_TIMESTAMP) {
          strcat(listVal, "SQL_DL_SQL92_TIMESTAMP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_YEAR) == SQL_DL_SQL92_INTERVAL_YEAR) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_YEAR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_MONTH) == SQL_DL_SQL92_INTERVAL_MONTH) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_DAY) == SQL_DL_SQL92_INTERVAL_DAY) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_DAY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_HOUR) == SQL_DL_SQL92_INTERVAL_HOUR) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_HOUR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_MINUTE) == SQL_DL_SQL92_INTERVAL_MINUTE) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_SECOND) == SQL_DL_SQL92_INTERVAL_SECOND) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH) == SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR) == SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE) == SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND) == SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE) == SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND) == SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND) == SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND) {
          strcat(listVal, "SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DDL_INDEX:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DI_CREATE_INDEX) == SQL_DI_CREATE_INDEX) {
          strcat(listVal, "SQL_DI_CREATE_INDEX,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DI_DROP_INDEX) == SQL_DI_DROP_INDEX) {
          strcat(listVal, "SQL_DI_DROP_INDEX,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DEFAULT_TXN_ISOLATION:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_TXN_READ_UNCOMMITTED:
          retVal = ndbcSQL_TXN_READ_UNCOMMITTED;
          break;
        case SQL_TXN_READ_COMMITTED:
          retVal = ndbcSQL_TXN_READ_COMMITTED;
          break;
        case SQL_TXN_REPEATABLE_READ:
          retVal = ndbcSQL_TXN_REPEATABLE_READ;
          break;
        case SQL_TXN_SERIALIZABLE:
          retVal = ndbcSQL_TXN_SERIALIZABLE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_DROP_ASSERTION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DA_DROP_ASSERTION) == SQL_DA_DROP_ASSERTION) {
          strcat(listVal, "SQL_DA_DROP_ASSERTION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_CHARACTER_SET:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DCS_DROP_CHARACTER_SET) == SQL_DCS_DROP_CHARACTER_SET) {
          strcat(listVal, "SQL_DCS_DROP_CHARACTER_SET,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_COLLATION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DC_DROP_COLLATION) == SQL_DC_DROP_COLLATION) {
          strcat(listVal, "SQL_DC_DROP_COLLATION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_DOMAIN:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DD_DROP_DOMAIN) == SQL_DD_DROP_DOMAIN) {
          strcat(listVal, "SQL_DD_DROP_DOMAIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DD_CASCADE) == SQL_DD_CASCADE) {
          strcat(listVal, "SQL_DD_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DD_RESTRICT) == SQL_DD_RESTRICT) {
          strcat(listVal, "SQL_DD_RESTRICT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_SCHEMA:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DS_DROP_SCHEMA) == SQL_DS_DROP_SCHEMA) {
          strcat(listVal, "SQL_DS_DROP_SCHEMA,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DS_CASCADE) == SQL_DS_CASCADE) {
          strcat(listVal, "SQL_DS_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DS_RESTRICT) == SQL_DS_RESTRICT) {
          strcat(listVal, "SQL_DS_RESTRICT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_TABLE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DT_DROP_TABLE) == SQL_DT_DROP_TABLE) {
          strcat(listVal, "SQL_DT_DROP_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DT_CASCADE) == SQL_DT_CASCADE) {
          strcat(listVal, "SQL_DT_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DT_RESTRICT) == SQL_DT_RESTRICT) {
          strcat(listVal, "SQL_DT_RESTRICT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_TRANSLATION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DTR_DROP_TRANSLATION) == SQL_DTR_DROP_TRANSLATION) {
          strcat(listVal, "SQL_DTR_DROP_TRANSLATION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DROP_VIEW:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_DV_DROP_VIEW) == SQL_DV_DROP_VIEW) {
          strcat(listVal, "SQL_DV_DROP_VIEW,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DV_CASCADE) == SQL_DV_CASCADE) {
          strcat(listVal, "SQL_DV_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_DV_RESTRICT) == SQL_DV_RESTRICT) {
          strcat(listVal, "SQL_DV_RESTRICT,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DYNAMIC_CURSOR_ATTRIBUTES1:
      case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1:
      case SQL_KEYSET_CURSOR_ATTRIBUTES1:
      case SQL_STATIC_CURSOR_ATTRIBUTES1:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_NEXT) == SQL_CA1_NEXT) {
          strcat(listVal, "SQL_CA1_NEXT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_ABSOLUTE) == SQL_CA1_ABSOLUTE) {
          strcat(listVal, "SQL_CA1_ABSOLUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_RELATIVE) == SQL_CA1_RELATIVE) {
          strcat(listVal, "SQL_CA1_RELATIVE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_BOOKMARK) == SQL_CA1_BOOKMARK) {
          strcat(listVal, "SQL_CA1_BOOKMARK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_LOCK_EXCLUSIVE) == SQL_CA1_LOCK_EXCLUSIVE) {
          strcat(listVal, "SQL_CA1_LOCK_EXCLUSIVE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_LOCK_NO_CHANGE) == SQL_CA1_LOCK_NO_CHANGE) {
          strcat(listVal, "SQL_CA1_LOCK_NO_CHANGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_LOCK_UNLOCK) == SQL_CA1_LOCK_UNLOCK) {
          strcat(listVal, "SQL_CA1_LOCK_UNLOCK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POS_POSITION) == SQL_CA1_POS_POSITION) {
          strcat(listVal, "SQL_CA1_POS_POSITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POS_UPDATE) == SQL_CA1_POS_UPDATE) {
          strcat(listVal, "SQL_CA1_POS_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POS_DELETE) == SQL_CA1_POS_DELETE) {
          strcat(listVal, "SQL_CA1_POS_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POS_REFRESH) == SQL_CA1_POS_REFRESH) {
          strcat(listVal, "SQL_CA1_POS_REFRESH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POSITIONED_UPDATE) == SQL_CA1_POSITIONED_UPDATE) {
          strcat(listVal, "SQL_CA1_POSITIONED_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_POSITIONED_DELETE) == SQL_CA1_POSITIONED_DELETE) {
          strcat(listVal, "SQL_CA1_POSITIONED_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_SELECT_FOR_UPDATE) == SQL_CA1_SELECT_FOR_UPDATE) {
          strcat(listVal, "SQL_CA1_SELECT_FOR_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_BULK_ADD) == SQL_CA1_BULK_ADD) {
          strcat(listVal, "SQL_CA1_BULK_ADD,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_BULK_UPDATE_BY_BOOKMARK) == SQL_CA1_BULK_UPDATE_BY_BOOKMARK) {
          strcat(listVal, "SQL_CA1_BULK_UPDATE_BY_BOOKMARK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_BULK_DELETE_BY_BOOKMARK) == SQL_CA1_BULK_DELETE_BY_BOOKMARK) {
          strcat(listVal, "SQL_CA1_BULK_DELETE_BY_BOOKMARK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA1_BULK_FETCH_BY_BOOKMARK) == SQL_CA1_BULK_FETCH_BY_BOOKMARK) {
          strcat(listVal, "SQL_CA1_BULK_FETCH_BY_BOOKMARK,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_DYNAMIC_CURSOR_ATTRIBUTES2:
      case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2:
      case SQL_KEYSET_CURSOR_ATTRIBUTES2:
      case SQL_STATIC_CURSOR_ATTRIBUTES2:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_READ_ONLY_CONCURRENCY) == SQL_CA2_READ_ONLY_CONCURRENCY) {
          strcat(listVal, "SQL_CA2_READ_ONLY_CONCURRENCY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_LOCK_CONCURRENCY) == SQL_CA2_LOCK_CONCURRENCY) {
          strcat(listVal, "SQL_CA2_LOCK_CONCURRENCY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_OPT_ROWVER_CONCURRENCY) == SQL_CA2_OPT_ROWVER_CONCURRENCY) {
          strcat(listVal, "SQL_CA2_OPT_ROWVER_CONCURRENCY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_OPT_VALUES_CONCURRENCY) == SQL_CA2_OPT_VALUES_CONCURRENCY) {
          strcat(listVal, "SQL_CA2_OPT_VALUES_CONCURRENCY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SENSITIVITY_ADDITIONS) == SQL_CA2_SENSITIVITY_ADDITIONS) {
          strcat(listVal, "SQL_CA2_SENSITIVITY_ADDITIONS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SENSITIVITY_DELETIONS) == SQL_CA2_SENSITIVITY_DELETIONS) {
          strcat(listVal, "SQL_CA2_SENSITIVITY_DELETIONS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SENSITIVITY_UPDATES) == SQL_CA2_SENSITIVITY_UPDATES) {
          strcat(listVal, "SQL_CA2_SENSITIVITY_UPDATES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_SELECT) == SQL_CA2_MAX_ROWS_SELECT) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_SELECT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_INSERT) == SQL_CA2_MAX_ROWS_INSERT) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_INSERT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_DELETE) == SQL_CA2_MAX_ROWS_DELETE) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_UPDATE) == SQL_CA2_MAX_ROWS_UPDATE) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_CATALOG) == SQL_CA2_MAX_ROWS_CATALOG) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_CATALOG,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_MAX_ROWS_AFFECTS_ALL) == SQL_CA2_MAX_ROWS_AFFECTS_ALL) {
          strcat(listVal, "SQL_CA2_MAX_ROWS_AFFECTS_ALL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_CRC_EXACT) == SQL_CA2_CRC_EXACT) {
          strcat(listVal, "SQL_CA2_CRC_EXACT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_CRC_APPROXIMATE) == SQL_CA2_CRC_APPROXIMATE) {
          strcat(listVal, "SQL_CA2_CRC_APPROXIMATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SIMULATE_NON_UNIQUE) == SQL_CA2_SIMULATE_NON_UNIQUE) {
          strcat(listVal, "SQL_CA2_SIMULATE_NON_UNIQUE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SIMULATE_TRY_UNIQUE) == SQL_CA2_SIMULATE_TRY_UNIQUE) {
          strcat(listVal, "SQL_CA2_SIMULATE_TRY_UNIQUE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_CA2_SIMULATE_UNIQUE) == SQL_CA2_SIMULATE_UNIQUE) {
          strcat(listVal, "SQL_CA2_SIMULATE_UNIQUE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_FILE_USAGE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FILE_NOT_SUPPORTED) == SQL_FILE_NOT_SUPPORTED) {
          strcat(listVal, "SQL_FILE_NOT_SUPPORTED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FILE_TABLE) == SQL_FILE_TABLE) {
          strcat(listVal, "SQL_FILE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FILE_CATALOG) == SQL_FILE_CATALOG) {
          strcat(listVal, "SQL_FILE_CATALOG,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_GETDATA_EXTENSIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_GD_ANY_COLUMN) == SQL_GD_ANY_COLUMN) {
          strcat(listVal, "SQL_GD_ANY_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_GD_ANY_ORDER) == SQL_GD_ANY_ORDER) {
          strcat(listVal, "SQL_GD_ANY_ORDER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_GD_BLOCK) == SQL_GD_BLOCK) {
          strcat(listVal, "SQL_GD_BLOCK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_GD_BOUND) == SQL_GD_BOUND) {
          strcat(listVal, "SQL_GD_BOUND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_GD_OUTPUT_PARAMS) == SQL_GD_OUTPUT_PARAMS) {
          strcat(listVal, "SQL_GD_OUTPUT_PARAMS,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_GROUP_BY:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUSMALLINT*)attrVal & SQL_GB_COLLATE) == SQL_GB_COLLATE) {
          strcat(listVal, "SQL_GB_COLLATE,");
        }
        if ((*(SQLUSMALLINT*)attrVal & SQL_GB_NOT_SUPPORTED) == SQL_GB_NOT_SUPPORTED) {
          strcat(listVal, "SQL_GB_NOT_SUPPORTED,");
        }
        if ((*(SQLUSMALLINT*)attrVal & SQL_GB_GROUP_BY_EQUALS_SELECT) == SQL_GB_GROUP_BY_EQUALS_SELECT) {
          strcat(listVal, "SQL_GB_GROUP_BY_EQUALS_SELECT,");
        }
        if ((*(SQLUSMALLINT*)attrVal & SQL_GB_GROUP_BY_CONTAINS_SELECT) == SQL_GB_GROUP_BY_CONTAINS_SELECT) {
          strcat(listVal, "SQL_GB_GROUP_BY_CONTAINS_SELECT,");
        }
        if ((*(SQLUSMALLINT*)attrVal & SQL_GB_NO_RELATION) == SQL_GB_NO_RELATION) {
          strcat(listVal, "SQL_GB_NO_RELATION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_IDENTIFIER_CASE:
      case SQL_QUOTED_IDENTIFIER_CASE:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_IC_UPPER:
          retVal = ndbcSQL_IC_UPPER;
          break;
        case SQL_IC_LOWER:
          retVal = ndbcSQL_IC_LOWER;
          break;
        case SQL_IC_SENSITIVE:
          retVal = ndbcSQL_IC_SENSITIVE;
          break;
        case SQL_IC_MIXED:
          retVal = ndbcSQL_IC_MIXED;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_INDEX_KEYWORDS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_IK_NONE) == SQL_IK_NONE) {
          strcat(listVal, "SQL_IK_NONE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_IK_ASC) == SQL_IK_ASC) {
          strcat(listVal, "SQL_IK_ASC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_IK_DESC) == SQL_IK_DESC) {
          strcat(listVal, "SQL_IK_DESC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_IK_ALL) == SQL_IK_ALL) {
          strcat(listVal, "SQL_IK_ALL,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_INFO_SCHEMA_VIEWS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_ASSERTIONS) == SQL_ISV_ASSERTIONS) {
          strcat(listVal, "SQL_ISV_ASSERTIONS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_CHARACTER_SETS) == SQL_ISV_CHARACTER_SETS) {
          strcat(listVal, "SQL_ISV_CHARACTER_SETS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_CHECK_CONSTRAINTS) == SQL_ISV_CHECK_CONSTRAINTS) {
          strcat(listVal, "SQL_ISV_CHECK_CONSTRAINTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_COLLATIONS) == SQL_ISV_COLLATIONS) {
          strcat(listVal, "SQL_ISV_COLLATIONS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_COLUMN_DOMAIN_USAGE) == SQL_ISV_COLUMN_DOMAIN_USAGE) {
          strcat(listVal, "SQL_ISV_COLUMN_DOMAIN_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_COLUMN_PRIVILEGES) == SQL_ISV_COLUMN_PRIVILEGES) {
          strcat(listVal, "SQL_ISV_COLUMN_PRIVILEGES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_COLUMNS) == SQL_ISV_COLUMNS) {
          strcat(listVal, "SQL_ISV_COLUMNS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_CONSTRAINT_COLUMN_USAGE) == SQL_ISV_CONSTRAINT_COLUMN_USAGE) {
          strcat(listVal, "SQL_ISV_CONSTRAINT_COLUMN_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_CONSTRAINT_TABLE_USAGE) == SQL_ISV_CONSTRAINT_TABLE_USAGE) {
          strcat(listVal, "SQL_ISV_CONSTRAINT_TABLE_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_DOMAIN_CONSTRAINTS) == SQL_ISV_DOMAIN_CONSTRAINTS) {
          strcat(listVal, "SQL_ISV_DOMAIN_CONSTRAINTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_DOMAINS) == SQL_ISV_DOMAINS) {
          strcat(listVal, "SQL_ISV_DOMAINS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_KEY_COLUMN_USAGE) == SQL_ISV_KEY_COLUMN_USAGE) {
          strcat(listVal, "SQL_ISV_KEY_COLUMN_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_REFERENTIAL_CONSTRAINTS) == SQL_ISV_REFERENTIAL_CONSTRAINTS) {
          strcat(listVal, "SQL_ISV_REFERENTIAL_CONSTRAINTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_SCHEMATA) == SQL_ISV_SCHEMATA) {
          strcat(listVal, "SQL_ISV_SCHEMATA,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_SQL_LANGUAGES) == SQL_ISV_SQL_LANGUAGES) {
          strcat(listVal, "SQL_ISV_SQL_LANGUAGES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_TABLE_CONSTRAINTS) == SQL_ISV_TABLE_CONSTRAINTS) {
          strcat(listVal, "SQL_ISV_TABLE_CONSTRAINTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_TABLE_PRIVILEGES) == SQL_ISV_TABLE_PRIVILEGES) {
          strcat(listVal, "SQL_ISV_TABLE_PRIVILEGES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_TABLES) == SQL_ISV_TABLES) {
          strcat(listVal, "SQL_ISV_TABLES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_TRANSLATIONS) == SQL_ISV_TRANSLATIONS) {
          strcat(listVal, "SQL_ISV_TRANSLATIONS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_USAGE_PRIVILEGES) == SQL_ISV_USAGE_PRIVILEGES) {
          strcat(listVal, "SQL_ISV_USAGE_PRIVILEGES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_VIEW_COLUMN_USAGE) == SQL_ISV_VIEW_COLUMN_USAGE) {
          strcat(listVal, "SQL_ISV_VIEW_COLUMN_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_VIEW_TABLE_USAGE) == SQL_ISV_VIEW_TABLE_USAGE) {
          strcat(listVal, "SQL_ISV_VIEW_TABLE_USAGE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_ISV_VIEWS) == SQL_ISV_VIEWS) {
          strcat(listVal, "SQL_ISV_VIEWS,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_INSERT_STATEMENT:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_IS_INSERT_LITERALS) == SQL_IS_INSERT_LITERALS) {
          strcat(listVal, "SQL_IS_INSERT_LITERALS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_IS_INSERT_SEARCHED) == SQL_IS_INSERT_SEARCHED) {
          strcat(listVal, "SQL_IS_INSERT_SEARCHED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_IS_SELECT_INTO) == SQL_IS_SELECT_INTO) {
          strcat(listVal, "SQL_IS_SELECT_INTO,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_NON_NULLABLE_COLUMNS:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_NNC_NULL:
          retVal = ndbcSQL_NNC_NULL;
          break;
        case SQL_NNC_NON_NULL:
          retVal = ndbcSQL_NNC_NON_NULL;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_NULL_COLLATION:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_NC_END:
          retVal = ndbcSQL_NC_END;
          break;
        case SQL_NC_HIGH:
          retVal = ndbcSQL_NC_HIGH;
          break;
        case SQL_NC_LOW:
          retVal = ndbcSQL_NC_LOW;
          break;
        case SQL_NC_START:
          retVal = ndbcSQL_NC_START;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_NUMERIC_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ABS) == SQL_FN_NUM_ABS) {
          strcat(listVal, "SQL_FN_NUM_ABS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ACOS) == SQL_FN_NUM_ACOS) {
          strcat(listVal, "SQL_FN_NUM_ACOS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ASIN) == SQL_FN_NUM_ASIN) {
          strcat(listVal, "SQL_FN_NUM_ASIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ATAN) == SQL_FN_NUM_ATAN) {
          strcat(listVal, "SQL_FN_NUM_ATAN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ATAN2) == SQL_FN_NUM_ATAN2) {
          strcat(listVal, "SQL_FN_NUM_ATAN2,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_CEILING) == SQL_FN_NUM_CEILING) {
          strcat(listVal, "SQL_FN_NUM_CEILING,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_COS) == SQL_FN_NUM_COS) {
          strcat(listVal, "SQL_FN_NUM_COS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_COT) == SQL_FN_NUM_COT) {
          strcat(listVal, "SQL_FN_NUM_COT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_DEGREES) == SQL_FN_NUM_DEGREES) {
          strcat(listVal, "SQL_FN_NUM_DEGREES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_EXP) == SQL_FN_NUM_EXP) {
          strcat(listVal, "SQL_FN_NUM_EXP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_FLOOR) == SQL_FN_NUM_FLOOR) {
          strcat(listVal, "SQL_FN_NUM_FLOOR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_LOG) == SQL_FN_NUM_LOG) {
          strcat(listVal, "SQL_FN_NUM_LOG,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_LOG10) == SQL_FN_NUM_LOG10) {
          strcat(listVal, "SQL_FN_NUM_LOG10,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_MOD) == SQL_FN_NUM_MOD) {
          strcat(listVal, "SQL_FN_NUM_MOD,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_PI) == SQL_FN_NUM_PI) {
          strcat(listVal, "SQL_FN_NUM_PI,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_POWER) == SQL_FN_NUM_POWER) {
          strcat(listVal, "SQL_FN_NUM_POWER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_RADIANS) == SQL_FN_NUM_RADIANS) {
          strcat(listVal, "SQL_FN_NUM_RADIANS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_RAND) == SQL_FN_NUM_RAND) {
          strcat(listVal, "SQL_FN_NUM_RAND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_ROUND) == SQL_FN_NUM_ROUND) {
          strcat(listVal, "SQL_FN_NUM_ROUND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_SIGN) == SQL_FN_NUM_SIGN) {
          strcat(listVal, "SQL_FN_NUM_SIGN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_SIN) == SQL_FN_NUM_SIN) {
          strcat(listVal, "SQL_FN_NUM_SIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_SQRT) == SQL_FN_NUM_SQRT) {
          strcat(listVal, "SQL_FN_NUM_SQRT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_TAN) == SQL_FN_NUM_TAN) {
          strcat(listVal, "SQL_FN_NUM_TAN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_NUM_TRUNCATE) == SQL_FN_NUM_TRUNCATE) {
          strcat(listVal, "SQL_FN_NUM_TRUNCATE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_ODBC_INTERFACE_CONFORMANCE:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_OIC_CORE:
          retVal = ndbcSQL_OIC_CORE;
          break;
        case SQL_OIC_LEVEL1:
          retVal = ndbcSQL_OIC_LEVEL1;
          break;
        case SQL_OIC_LEVEL2:
          retVal = ndbcSQL_OIC_LEVEL2;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_OJ_CAPABILITIES:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_LEFT) == SQL_OJ_LEFT) {
          strcat(listVal, "SQL_OJ_LEFT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_RIGHT) == SQL_OJ_RIGHT) {
          strcat(listVal, "SQL_OJ_RIGHT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_FULL) == SQL_OJ_FULL) {
          strcat(listVal, "SQL_OJ_FULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_NESTED) == SQL_OJ_NESTED) {
          strcat(listVal, "SQL_OJ_NESTED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_NOT_ORDERED) == SQL_OJ_NOT_ORDERED) {
          strcat(listVal, "SQL_OJ_NOT_ORDERED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_INNER) == SQL_OJ_INNER) {
          strcat(listVal, "SQL_OJ_INNER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_OJ_ALL_COMPARISON_OPS) == SQL_OJ_ALL_COMPARISON_OPS) {
          strcat(listVal, "SQL_OJ_ALL_COMPARISON_OPS,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_PARAM_ARRAY_ROW_COUNTS:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_PARC_BATCH:
          retVal = ndbcSQL_PARC_BATCH;
          break;
        case SQL_PARC_NO_BATCH:
          retVal = ndbcSQL_PARC_NO_BATCH;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_PARAM_ARRAY_SELECTS:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_PAS_BATCH:
          retVal = ndbcSQL_PAS_BATCH;
          break;
        case SQL_PAS_NO_BATCH:
          retVal = ndbcSQL_PAS_NO_BATCH;
          break;
        case SQL_PAS_NO_SELECT:
          retVal = ndbcSQL_PAS_NO_SELECT;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_POS_OPERATIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_POS_POSITION) == SQL_POS_POSITION) {
          strcat(listVal, "SQL_POS_POSITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_POS_REFRESH) == SQL_POS_REFRESH) {
          strcat(listVal, "SQL_POS_REFRESH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_POS_UPDATE) == SQL_POS_UPDATE) {
          strcat(listVal, "SQL_POS_UPDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_POS_DELETE) == SQL_POS_DELETE) {
          strcat(listVal, "SQL_POS_DELETE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_POS_ADD) == SQL_POS_ADD) {
          strcat(listVal, "SQL_POS_ADD,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SCHEMA_USAGE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SU_DML_STATEMENTS) == SQL_SU_DML_STATEMENTS) {
          strcat(listVal, "SQL_SU_DML_STATEMENTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SU_PROCEDURE_INVOCATION) == SQL_SU_PROCEDURE_INVOCATION) {
          strcat(listVal, "SQL_SU_PROCEDURE_INVOCATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SU_TABLE_DEFINITION) == SQL_SU_TABLE_DEFINITION) {
          strcat(listVal, "SQL_SU_TABLE_DEFINITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SU_INDEX_DEFINITION) == SQL_SU_INDEX_DEFINITION) {
          strcat(listVal, "SQL_SU_INDEX_DEFINITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SU_PRIVILEGE_DEFINITION) == SQL_SU_PRIVILEGE_DEFINITION) {
          strcat(listVal, "SQL_SU_PRIVILEGE_DEFINITION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SCROLL_OPTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SO_FORWARD_ONLY) == SQL_SO_FORWARD_ONLY) {
          strcat(listVal, "SQL_SO_FORWARD_ONLY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SO_STATIC) == SQL_SO_STATIC) {
          strcat(listVal, "SQL_SO_STATIC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SO_KEYSET_DRIVEN) == SQL_SO_KEYSET_DRIVEN) {
          strcat(listVal, "SQL_SO_KEYSET_DRIVEN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SO_DYNAMIC) == SQL_SO_DYNAMIC) {
          strcat(listVal, "SQL_SO_DYNAMIC,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SO_MIXED) == SQL_SO_MIXED) {
          strcat(listVal, "SQL_SO_MIXED,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL_CONFORMANCE:
        switch (*(SQLUINTEGER*)attrVal) {
        case SQL_SC_SQL92_ENTRY:
          retVal = ndbcSQL_SC_SQL92_ENTRY;
          break;
        case SQL_SC_FIPS127_2_TRANSITIONAL:
          retVal = ndbcSQL_SC_FIPS127_2_TRANSITIONAL;
          break;
        case SQL_SC_SQL92_FULL:
          retVal = ndbcSQL_SC_SQL92_FULL;
          break;
        case SQL_SC_SQL92_INTERMEDIATE:
          retVal = ndbcSQL_SC_SQL92_INTERMEDIATE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_SQL92_DATETIME_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SDF_CURRENT_DATE) == SQL_SDF_CURRENT_DATE) {
          strcat(listVal, "SQL_SDF_CURRENT_DATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SDF_CURRENT_TIME) == SQL_SDF_CURRENT_TIME) {
          strcat(listVal, "SQL_SDF_CURRENT_TIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SDF_CURRENT_TIMESTAMP) == SQL_SDF_CURRENT_TIMESTAMP) {
          strcat(listVal, "SQL_SDF_CURRENT_TIMESTAMP,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_FOREIGN_KEY_DELETE_RULE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKD_CASCADE) == SQL_SFKD_CASCADE) {
          strcat(listVal, "SQL_SFKD_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKD_NO_ACTION) == SQL_SFKD_NO_ACTION) {
          strcat(listVal, "SQL_SFKD_NO_ACTION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKD_SET_DEFAULT) == SQL_SFKD_SET_DEFAULT) {
          strcat(listVal, "SQL_SFKD_SET_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKD_SET_NULL) == SQL_SFKD_SET_NULL) {
          strcat(listVal, "SQL_SFKD_SET_NULL,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_FOREIGN_KEY_UPDATE_RULE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKU_CASCADE) == SQL_SFKD_CASCADE) {
          strcat(listVal, "SQL_SFKD_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKU_NO_ACTION) == SQL_SFKD_NO_ACTION) {
          strcat(listVal, "SQL_SFKD_NO_ACTION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKU_SET_DEFAULT) == SQL_SFKD_SET_DEFAULT) {
          strcat(listVal, "SQL_SFKD_SET_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SFKU_SET_NULL) == SQL_SFKD_SET_NULL) {
          strcat(listVal, "SQL_SFKD_SET_NULL,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_GRANT:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_DELETE_TABLE) == SQL_SG_DELETE_TABLE) {
          strcat(listVal, "SQL_SG_DELETE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_INSERT_COLUMN) == SQL_SG_INSERT_COLUMN) {
          strcat(listVal, "SQL_SG_INSERT_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_INSERT_TABLE) == SQL_SG_INSERT_TABLE) {
          strcat(listVal, "SQL_SG_INSERT_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_REFERENCES_TABLE) == SQL_SG_REFERENCES_TABLE) {
          strcat(listVal, "SQL_SG_REFERENCES_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_REFERENCES_COLUMN) == SQL_SG_REFERENCES_COLUMN) {
          strcat(listVal, "SQL_SG_REFERENCES_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_SELECT_TABLE) == SQL_SG_SELECT_TABLE) {
          strcat(listVal, "SQL_SG_SELECT_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_UPDATE_COLUMN) == SQL_SG_UPDATE_COLUMN) {
          strcat(listVal, "SQL_SG_UPDATE_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_UPDATE_TABLE) == SQL_SG_UPDATE_TABLE) {
          strcat(listVal, "SQL_SG_UPDATE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_USAGE_ON_DOMAIN) == SQL_SG_USAGE_ON_DOMAIN) {
          strcat(listVal, "SQL_SG_USAGE_ON_DOMAIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_USAGE_ON_CHARACTER_SET) == SQL_SG_USAGE_ON_CHARACTER_SET) {
          strcat(listVal, "SQL_SG_USAGE_ON_CHARACTER_SET,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_USAGE_ON_COLLATION) == SQL_SG_USAGE_ON_COLLATION) {
          strcat(listVal, "SQL_SG_USAGE_ON_COLLATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_USAGE_ON_TRANSLATION) == SQL_SG_USAGE_ON_TRANSLATION) {
          strcat(listVal, "SQL_SG_USAGE_ON_TRANSLATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SG_WITH_GRANT_OPTION) == SQL_SG_WITH_GRANT_OPTION) {
          strcat(listVal, "SQL_SG_WITH_GRANT_OPTION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_NUMERIC_VALUE_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_BIT_LENGTH) == SQL_SNVF_BIT_LENGTH) {
          strcat(listVal, "SQL_SNVF_BIT_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_CHAR_LENGTH) == SQL_SNVF_CHAR_LENGTH) {
          strcat(listVal, "SQL_SNVF_CHAR_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_CHARACTER_LENGTH) == SQL_SNVF_CHARACTER_LENGTH) {
          strcat(listVal, "SQL_SNVF_CHARACTER_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_EXTRACT) == SQL_SNVF_EXTRACT) {
          strcat(listVal, "SQL_SNVF_EXTRACT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_OCTET_LENGTH) == SQL_SNVF_OCTET_LENGTH) {
          strcat(listVal, "SQL_SNVF_OCTET_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SNVF_POSITION) == SQL_SNVF_POSITION) {
          strcat(listVal, "SQL_SNVF_POSITION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_PREDICATES:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_BETWEEN) == SQL_SP_BETWEEN) {
          strcat(listVal, "SQL_SP_BETWEEN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_COMPARISON) == SQL_SP_COMPARISON) {
          strcat(listVal, "SQL_SP_COMPARISON,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_EXISTS) == SQL_SP_EXISTS) {
          strcat(listVal, "SQL_SP_EXISTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_IN) == SQL_SP_IN) {
          strcat(listVal, "SQL_SP_IN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_ISNOTNULL) == SQL_SP_ISNOTNULL) {
          strcat(listVal, "SQL_SP_ISNOTNULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_ISNULL) == SQL_SP_ISNULL) {
          strcat(listVal, "SQL_SP_ISNULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_LIKE) == SQL_SP_LIKE) {
          strcat(listVal, "SQL_SP_LIKE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_MATCH_FULL) == SQL_SP_MATCH_FULL) {
          strcat(listVal, "SQL_SP_MATCH_FULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_MATCH_PARTIAL) == SQL_SP_MATCH_PARTIAL) {
          strcat(listVal, "SQL_SP_MATCH_PARTIAL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_MATCH_UNIQUE_FULL) == SQL_SP_MATCH_UNIQUE_FULL) {
          strcat(listVal, "SQL_SP_MATCH_UNIQUE_FULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_MATCH_UNIQUE_PARTIAL) == SQL_SP_MATCH_UNIQUE_PARTIAL) {
          strcat(listVal, "SQL_SP_MATCH_UNIQUE_PARTIAL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_OVERLAPS) == SQL_SP_OVERLAPS) {
          strcat(listVal, "SQL_SP_OVERLAPS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_QUANTIFIED_COMPARISON) == SQL_SP_QUANTIFIED_COMPARISON) {
          strcat(listVal, "SQL_SP_QUANTIFIED_COMPARISON,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SP_UNIQUE) == SQL_SP_UNIQUE) {
          strcat(listVal, "SQL_SP_UNIQUE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_RELATIONAL_JOIN_OPERATORS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_CORRESPONDING_CLAUSE) == SQL_SRJO_CORRESPONDING_CLAUSE) {
          strcat(listVal, "SQL_SRJO_CORRESPONDING_CLAUSE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_CROSS_JOIN) == SQL_SRJO_CROSS_JOIN) {
          strcat(listVal, "SQL_SRJO_CROSS_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_EXCEPT_JOIN) == SQL_SRJO_EXCEPT_JOIN) {
          strcat(listVal, "SQL_SRJO_EXCEPT_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_FULL_OUTER_JOIN) == SQL_SRJO_FULL_OUTER_JOIN) {
          strcat(listVal, "SQL_SRJO_FULL_OUTER_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_INNER_JOIN) == SQL_SRJO_INNER_JOIN) {
          strcat(listVal, "SQL_SRJO_INNER_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_INTERSECT_JOIN) == SQL_SRJO_INTERSECT_JOIN) {
          strcat(listVal, "SQL_SRJO_INTERSECT_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_LEFT_OUTER_JOIN) == SQL_SRJO_LEFT_OUTER_JOIN) {
          strcat(listVal, "SQL_SRJO_LEFT_OUTER_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_NATURAL_JOIN) == SQL_SRJO_NATURAL_JOIN) {
          strcat(listVal, "SQL_SRJO_NATURAL_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_RIGHT_OUTER_JOIN) == SQL_SRJO_RIGHT_OUTER_JOIN) {
          strcat(listVal, "SQL_SRJO_RIGHT_OUTER_JOIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRJO_UNION_JOIN) == SQL_SRJO_UNION_JOIN) {
          strcat(listVal, "SQL_SRJO_UNION_JOIN,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_REVOKE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_CASCADE) == SQL_SR_CASCADE) {
          strcat(listVal, "SQL_SR_CASCADE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_DELETE_TABLE) == SQL_SR_DELETE_TABLE) {
          strcat(listVal, "SQL_SR_DELETE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_GRANT_OPTION_FOR) == SQL_SR_GRANT_OPTION_FOR) {
          strcat(listVal, "SQL_SR_GRANT_OPTION_FOR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_INSERT_COLUMN) == SQL_SR_INSERT_COLUMN) {
          strcat(listVal, "SQL_SR_INSERT_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_INSERT_TABLE) == SQL_SR_INSERT_TABLE) {
          strcat(listVal, "SQL_SR_INSERT_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_REFERENCES_COLUMN) == SQL_SR_REFERENCES_COLUMN) {
          strcat(listVal, "SQL_SR_REFERENCES_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_REFERENCES_TABLE) == SQL_SR_REFERENCES_TABLE) {
          strcat(listVal, "SQL_SR_REFERENCES_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_RESTRICT) == SQL_SR_RESTRICT) {
          strcat(listVal, "SQL_SR_RESTRICT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_SELECT_TABLE) == SQL_SR_SELECT_TABLE) {
          strcat(listVal, "SQL_SR_SELECT_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_UPDATE_COLUMN) == SQL_SR_UPDATE_COLUMN) {
          strcat(listVal, "SQL_SR_UPDATE_COLUMN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_UPDATE_TABLE) == SQL_SR_UPDATE_TABLE) {
          strcat(listVal, "SQL_SR_UPDATE_TABLE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_USAGE_ON_DOMAIN) == SQL_SR_USAGE_ON_DOMAIN) {
          strcat(listVal, "SQL_SR_USAGE_ON_DOMAIN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_USAGE_ON_CHARACTER_SET) == SQL_SR_USAGE_ON_CHARACTER_SET) {
          strcat(listVal, "SQL_SR_USAGE_ON_CHARACTER_SET,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_USAGE_ON_COLLATION) == SQL_SR_USAGE_ON_COLLATION) {
          strcat(listVal, "SQL_SR_USAGE_ON_COLLATION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SR_USAGE_ON_TRANSLATION) == SQL_SR_USAGE_ON_TRANSLATION) {
          strcat(listVal, "SQL_SR_USAGE_ON_TRANSLATION,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_ROW_VALUE_CONSTRUCTOR:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SRVC_VALUE_EXPRESSION) == SQL_SRVC_VALUE_EXPRESSION) {
          strcat(listVal, "SQL_SRVC_VALUE_EXPRESSION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRVC_NULL) == SQL_SRVC_NULL) {
          strcat(listVal, "SQL_SRVC_NULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRVC_DEFAULT) == SQL_SRVC_DEFAULT) {
          strcat(listVal, "SQL_SRVC_DEFAULT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SRVC_ROW_SUBQUERY) == SQL_SRVC_ROW_SUBQUERY) {
          strcat(listVal, "SQL_SRVC_ROW_SUBQUERY,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_STRING_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_CONVERT) == SQL_SSF_CONVERT) {
          strcat(listVal, "SQL_SSF_CONVERT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_LOWER) == SQL_SSF_LOWER) {
          strcat(listVal, "SQL_SSF_LOWER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_UPPER) == SQL_SSF_UPPER) {
          strcat(listVal, "SQL_SSF_UPPER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_SUBSTRING) == SQL_SSF_SUBSTRING) {
          strcat(listVal, "SQL_SSF_SUBSTRING,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_TRANSLATE) == SQL_SSF_TRANSLATE) {
          strcat(listVal, "SQL_SSF_TRANSLATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_TRIM_BOTH) == SQL_SSF_TRIM_BOTH) {
          strcat(listVal, "SQL_SSF_TRIM_BOTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_TRIM_LEADING) == SQL_SSF_TRIM_LEADING) {
          strcat(listVal, "SQL_SSF_TRIM_LEADING,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SSF_TRIM_TRAILING) == SQL_SSF_TRIM_TRAILING) {
          strcat(listVal, "SQL_SSF_TRIM_TRAILING,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SQL92_VALUE_EXPRESSIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SVE_CASE) == SQL_SVE_CASE) {
          strcat(listVal, "SQL_SVE_CASE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SVE_CAST) == SQL_SVE_CAST) {
          strcat(listVal, "SQL_SVE_CAST,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SVE_COALESCE) == SQL_SVE_COALESCE) {
          strcat(listVal, "SQL_SVE_COALESCE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SVE_NULLIF) == SQL_SVE_NULLIF) {
          strcat(listVal, "SQL_SVE_NULLIF,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_STANDARD_CLI_CONFORMANCE:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SCC_XOPEN_CLI_VERSION1) == SQL_SCC_XOPEN_CLI_VERSION1) {
          strcat(listVal, "SQL_SCC_XOPEN_CLI_VERSION1,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SCC_ISO92_CLI) == SQL_SCC_ISO92_CLI) {
          strcat(listVal, "SQL_SCC_ISO92_CLI,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_STRING_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_ASCII) == SQL_FN_STR_ASCII) {
          strcat(listVal, "SQL_FN_STR_ASCII,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_BIT_LENGTH) == SQL_FN_STR_BIT_LENGTH) {
          strcat(listVal, "SQL_FN_STR_BIT_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_CHAR) == SQL_FN_STR_CHAR) {
          strcat(listVal, "SQL_FN_STR_CHAR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_CHAR_LENGTH) == SQL_FN_STR_CHAR_LENGTH) {
          strcat(listVal, "SQL_FN_STR_CHAR_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_CHARACTER_LENGTH) == SQL_FN_STR_CHARACTER_LENGTH) {
          strcat(listVal, "SQL_FN_STR_CHARACTER_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_CONCAT) == SQL_FN_STR_CONCAT) {
          strcat(listVal, "SQL_FN_STR_CONCAT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_DIFFERENCE) == SQL_FN_STR_DIFFERENCE) {
          strcat(listVal, "SQL_FN_STR_DIFFERENCE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_INSERT) == SQL_FN_STR_INSERT) {
          strcat(listVal, "SQL_FN_STR_INSERT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_LCASE) == SQL_FN_STR_LCASE) {
          strcat(listVal, "SQL_FN_STR_LCASE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_LEFT) == SQL_FN_STR_LEFT) {
          strcat(listVal, "SQL_FN_STR_LEFT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_LENGTH) == SQL_FN_STR_LENGTH) {
          strcat(listVal, "SQL_FN_STR_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_LOCATE) == SQL_FN_STR_LOCATE) {
          strcat(listVal, "SQL_FN_STR_LOCATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_LTRIM) == SQL_FN_STR_LTRIM) {
          strcat(listVal, "SQL_FN_STR_LTRIM,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_OCTET_LENGTH) == SQL_FN_STR_OCTET_LENGTH) {
          strcat(listVal, "SQL_FN_STR_OCTET_LENGTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_POSITION) == SQL_FN_STR_POSITION) {
          strcat(listVal, "SQL_FN_STR_POSITION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_REPEAT) == SQL_FN_STR_REPEAT) {
          strcat(listVal, "SQL_FN_STR_REPEAT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_REPLACE) == SQL_FN_STR_REPLACE) {
          strcat(listVal, "SQL_FN_STR_REPLACE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_RIGHT) == SQL_FN_STR_RIGHT) {
          strcat(listVal, "SQL_FN_STR_RIGHT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_RTRIM) == SQL_FN_STR_RTRIM) {
          strcat(listVal, "SQL_FN_STR_RTRIM,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_SOUNDEX) == SQL_FN_STR_SOUNDEX) {
          strcat(listVal, "SQL_FN_STR_SOUNDEX,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_SPACE) == SQL_FN_STR_SPACE) {
          strcat(listVal, "SQL_FN_STR_SPACE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_SUBSTRING) == SQL_FN_STR_SUBSTRING) {
          strcat(listVal, "SQL_FN_STR_SUBSTRING,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_STR_UCASE) == SQL_FN_STR_UCASE) {
          strcat(listVal, "SQL_FN_STR_UCASE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SUBQUERIES:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_SQ_CORRELATED_SUBQUERIES) == SQL_SQ_CORRELATED_SUBQUERIES) {
          strcat(listVal, "SQL_SQ_CORRELATED_SUBQUERIES,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SQ_COMPARISON) == SQL_SQ_COMPARISON) {
          strcat(listVal, "SQL_SQ_COMPARISON,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SQ_EXISTS) == SQL_SQ_EXISTS) {
          strcat(listVal, "SQL_SQ_EXISTS,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SQ_IN) == SQL_SQ_IN) {
          strcat(listVal, "SQL_SQ_IN,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_SQ_QUANTIFIED) == SQL_SQ_QUANTIFIED) {
          strcat(listVal, "SQL_SQ_QUANTIFIED,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_SYSTEM_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_SYS_DBNAME) == SQL_FN_SYS_DBNAME) {
          strcat(listVal, "SQL_FN_SYS_DBNAME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_SYS_IFNULL) == SQL_FN_SYS_IFNULL) {
          strcat(listVal, "SQL_FN_SYS_IFNULL,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_SYS_USERNAME) == SQL_FN_SYS_USERNAME) {
          strcat(listVal, "SQL_FN_SYS_USERNAME,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_TIMEDATE_ADD_INTERVALS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_FRAC_SECOND) == SQL_FN_TSI_FRAC_SECOND) {
          strcat(listVal, "SQL_FN_TSI_FRAC_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_SECOND) == SQL_FN_TSI_SECOND) {
          strcat(listVal, "SQL_FN_TSI_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_MINUTE) == SQL_FN_TSI_MINUTE) {
          strcat(listVal, "SQL_FN_TSI_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_HOUR) == SQL_FN_TSI_HOUR) {
          strcat(listVal, "SQL_FN_TSI_HOUR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_DAY) == SQL_FN_TSI_DAY) {
          strcat(listVal, "SQL_FN_TSI_DAY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_WEEK) == SQL_FN_TSI_WEEK) {
          strcat(listVal, "SQL_FN_TSI_WEEK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_MONTH) == SQL_FN_TSI_MONTH) {
          strcat(listVal, "SQL_FN_TSI_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_QUARTER) == SQL_FN_TSI_QUARTER) {
          strcat(listVal, "SQL_FN_TSI_QUARTER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_YEAR) == SQL_FN_TSI_YEAR) {
          strcat(listVal, "SQL_FN_TSI_YEAR,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_TIMEDATE_DIFF_INTERVALS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_FRAC_SECOND) == SQL_FN_TSI_FRAC_SECOND) {
          strcat(listVal, "SQL_FN_TSI_FRAC_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_SECOND) == SQL_FN_TSI_SECOND) {
          strcat(listVal, "SQL_FN_TSI_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_MINUTE) == SQL_FN_TSI_MINUTE) {
          strcat(listVal, "SQL_FN_TSI_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_HOUR) == SQL_FN_TSI_HOUR) {
          strcat(listVal, "SQL_FN_TSI_HOUR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_DAY) == SQL_FN_TSI_DAY) {
          strcat(listVal, "SQL_FN_TSI_DAY,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_WEEK) == SQL_FN_TSI_WEEK) {
          strcat(listVal, "SQL_FN_TSI_WEEK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_MONTH) == SQL_FN_TSI_MONTH) {
          strcat(listVal, "SQL_FN_TSI_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_QUARTER) == SQL_FN_TSI_QUARTER) {
          strcat(listVal, "SQL_FN_TSI_QUARTER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TSI_YEAR) == SQL_FN_TSI_YEAR) {
          strcat(listVal, "SQL_FN_TSI_YEAR,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_TIMEDATE_FUNCTIONS:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_CURRENT_DATE) == SQL_FN_TD_CURRENT_DATE) {
          strcat(listVal, "SQL_FN_TD_CURRENT_DATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_CURRENT_TIME) == SQL_FN_TD_CURRENT_TIME) {
          strcat(listVal, "SQL_FN_TD_CURRENT_TIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_CURRENT_TIMESTAMP) == SQL_FN_TD_CURRENT_TIMESTAMP) {
          strcat(listVal, "SQL_FN_TD_CURRENT_TIMESTAMP,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_CURDATE) == SQL_FN_TD_CURDATE) {
          strcat(listVal, "SQL_FN_TD_CURDATE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_CURTIME) == SQL_FN_TD_CURTIME) {
          strcat(listVal, "SQL_FN_TD_CURTIME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_DAYNAME) == SQL_FN_TD_DAYNAME) {
          strcat(listVal, "SQL_FN_TD_DAYNAME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_DAYOFMONTH) == SQL_FN_TD_DAYOFMONTH) {
          strcat(listVal, "SQL_FN_TD_DAYOFMONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_DAYOFWEEK) == SQL_FN_TD_DAYOFWEEK) {
          strcat(listVal, "SQL_FN_TD_DAYOFWEEK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_DAYOFYEAR) == SQL_FN_TD_DAYOFYEAR) {
          strcat(listVal, "SQL_FN_TD_DAYOFYEAR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_EXTRACT) == SQL_FN_TD_EXTRACT) {
          strcat(listVal, "SQL_FN_TD_EXTRACT,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_HOUR) == SQL_FN_TD_HOUR) {
          strcat(listVal, "SQL_FN_TD_HOUR,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_MINUTE) == SQL_FN_TD_MINUTE) {
          strcat(listVal, "SQL_FN_TD_MINUTE,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_MONTH) == SQL_FN_TD_MONTH) {
          strcat(listVal, "SQL_FN_TD_MONTH,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_MONTHNAME) == SQL_FN_TD_MONTHNAME) {
          strcat(listVal, "SQL_FN_TD_MONTHNAME,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_NOW) == SQL_FN_TD_NOW) {
          strcat(listVal, "SQL_FN_TD_NOW,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_QUARTER) == SQL_FN_TD_QUARTER) {
          strcat(listVal, "SQL_FN_TD_QUARTER,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_SECOND) == SQL_FN_TD_SECOND) {
          strcat(listVal, "SQL_FN_TD_SECOND,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_TIMESTAMPADD) == SQL_FN_TD_TIMESTAMPADD) {
          strcat(listVal, "SQL_FN_TD_TIMESTAMPADD,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_TIMESTAMPDIFF) == SQL_FN_TD_TIMESTAMPDIFF) {
          strcat(listVal, "SQL_FN_TD_TIMESTAMPDIFF,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_WEEK) == SQL_FN_TD_WEEK) {
          strcat(listVal, "SQL_FN_TD_WEEK,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_FN_TD_YEAR) == SQL_FN_TD_YEAR) {
          strcat(listVal, "SQL_FN_TD_YEAR,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_TXN_CAPABLE:
        switch (*(SQLUSMALLINT*)attrVal) {
        case SQL_TC_NONE:
          retVal = ndbcSQL_TC_NONE;
          break;
        case SQL_TC_DML:
          retVal = ndbcSQL_TC_DML;
          break;
        case SQL_TC_DDL_COMMIT:
          retVal = ndbcSQL_TC_DDL_COMMIT;
          break;
        case SQL_TC_DDL_IGNORE:
          retVal = ndbcSQL_TC_DDL_IGNORE;
          break;
        case SQL_TC_ALL:
          retVal = ndbcSQL_TC_ALL;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_TXN_ISOLATION_OPTION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_TXN_READ_UNCOMMITTED) == SQL_TXN_READ_UNCOMMITTED) {
          strcat(listVal, "SQL_TXN_READ_UNCOMMITTED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_TXN_READ_COMMITTED) == SQL_TXN_READ_COMMITTED) {
          strcat(listVal, "SQL_TXN_READ_COMMITTED,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_TXN_REPEATABLE_READ) == SQL_TXN_REPEATABLE_READ) {
          strcat(listVal, "SQL_TXN_REPEATABLE_READ,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_TXN_SERIALIZABLE) == SQL_TXN_SERIALIZABLE) {
          strcat(listVal, "SQL_TXN_SERIALIZABLE,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      case SQL_UNION:
        listVal = (char*) malloc(1024);
        listVal[0] = NULL;
        if ((*(SQLUINTEGER*)attrVal & SQL_U_UNION) == SQL_U_UNION) {
          strcat(listVal, "SQL_U_UNION,");
        }
        if ((*(SQLUINTEGER*)attrVal & SQL_U_UNION_ALL) == SQL_U_UNION_ALL) {
          strcat(listVal, "SQL_U_UNION_ALL,");
        }
        if (strlen(listVal) > 0) {
          listVal[strlen(listVal) - 1] = NULL;
        }
        retVal = String::New(listVal);
        free(listVal);
        break;
      default:
        retVal = ndbcINVALID_ARGUMENT;
      }
    }
  }

  free(attrVal);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLSetStmtAttr
 * SQLSetStmtAttr(statement, attribute, value)
 * statement - An statement handle created with SQLAllocHandle.
 * attribute - The attribute to set.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_APP_PARAM_DESC: The statement's parameter descriptor handle.
 *   SQL_ATTR_APP_ROW_DESC: The statement's row descriptor handle.
 *   SQL_ATTR_ASYNC_ENABLE: Indicates whether this statement runs asynchronously.
 *   SQL_ATTR_CONCURRENCY: Indicates the cursor concurrency associated with this statement.
 *   SQL_ATTR_CURSOR_SCROLLABLE: Indicates whether this statement has a scrollable cursor.
 *   SQL_ATTR_CURSOR_SENSITIVITY: Indicates whether this statement's cursor is sensitive to changes made by other cursors.
 *   SQL_ATTR_CURSOR_TYPE: Indicates the type of cursor associated with this statement.
 *   SQL_ATTR_ENABLE_AUTO_IPD: Indicates whether implementation parameter descriptors are automatically populated.
 *   SQL_ATTR_FETCH_BOOKMARK_PTR: The bookmark value that can be scrolled to using SQLFetchScroll.
 *   SQL_ATTR_IMP_PARAM_DESC: The implicit parameter descriptor that was set when this statement was allocated.  Read-only.
 *   SQL_ATTR_IMP_ROW_DESC: The implicit row descriptor that was set when this statement was allocated.  Read-only.
 *   SQL_ATTR_KEYSET_SIZE: The number of rows available in the keyset of a keyset-driven cursor.  Defaults to 0 (current row only).
 *   SQL_ATTR_MAX_LENGTH: The maximum length of data to return from a single column.  Defaults to 0 for no limit.
 *                        When set, this silently truncates data to the specified length.
 *   SQL_ATTR_MAX_ROWS: The maximum number of rows to return in a SELECT statement or catalog function.  Defaults to 0 for no limit.
 *                      When set, this silently truncates the result set to the specified number of rows.
 *   SQL_ATTR_METADATA_ID: Indicates whether the string arguments of catalog functions are treated as case-insensitive identifiers.
 *   SQL_ATTR_NOSCAN: Indicates whether to suppress scanning of query text for escape sequences.
 *   SQL_ATTR_PARAM_BIND_OFFSET_PTR: Indicates the offset to apply to binding dynamic parameters.
 *   SQL_ATTR_PARAM_BIND_TYPE: Indicates whether to bind parameter arrays by column or to bind by a specified length in a parameter buffer.
 *   SQL_ATTR_PARAM_OPERATION_PTR: Controls whether parameters are used or ignored when executing a statement.
 *   SQL_ATTR_PARAM_STATUS_PTR: Points to an array of return values for executed parameter sets.
 *   SQL_ATTR_PARAMS_PROCESSED_PTR: Points to a buffer that holds the number of parameter sets processed after executing.
 *   SQL_ATTR_PARAMSET_SIZE: Indicates the size of the parameter array being used.
 *   SQL_ATTR_QUERY_TIMEOUT: The timeout (in seconds) of this statement.
 *   SQL_ATTR_RETRIEVE_DATA: Indicates whether to automatically retrieve row data when the cursor position changes.
 *   SQL_ATTR_ROW_ARRAY_SIZE: The number of rows retrieved each time row data is fetched.
 *   SQL_ATTR_ROW_BIND_OFFSET_PTR: Indicates the offset to apply to binding column data.
 *   SQL_ATTR_ROW_BIND_TYPE: Indicates whether to bind row data by column or to bind by a specified length in a row data buffer.
 *   SQL_ATTR_ROW_NUMBER: The current row in the statement's result set.  Read-only.
 *   SQL_ATTR_ROW_STATUS_PTR: Points to an array holding row status values for fetched rows.
 *   SQL_ATTR_ROWS_FETCHED_PTR: Points to a buffer that holds the number of rows affected by the statement.
 *   SQL_ATTR_SIMULATE_CURSOR: Indicates whether cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_ATTR_USE_BOOKMARKS: Indicates whether the statement's cursor will use bookmarks.
 * value - The string or numeric value to set.
 *         Strings representing API constants will be translated into their constant values.
 *         Different attribute types have different available values.
 *   SQL_ATTR_APP_PARAM_DESC (supply a descriptor handle)
 *   SQL_ATTR_APP_ROW_DESC (supply a descriptor handle)
 *   SQL_ATTR_APP_ASYNC_ENABLE
 *     SQL_ASYNC_ENABLE_OFF: Statement will execute synchronously.
 *     SQL_ASYNC_ENABLE_ON: Statement will execute asynchronously.
 *   SQL_ATTR_CONCURRENCY
 *     SQL_CONCUR_READ_ONLY: Cursor is read-only.
 *     SQL_CONCUR_LOCK: Cursor locks when updating.
 *     SQL_CONCUR_ROWVER: Cursor handles concurrency optimistically using the row version.
 *     SQL_CONCUR_VALUES: Cursor handles concurrency optimistically using the row values.
 *   SQL_ATTR_CURSOR_SCROLLABLE
 *     SQL_NONSCROLLABLE: The cursor does not need to be able to scroll, it can only return the next row.
 *     SQL_SCROLLABLE: The cursor needs to be able to scroll in any direction.
 *   SQL_ATTR_CURSOR_SENSITIVITY
 *     SQL_UNSPECIFIED: Cursors may or may not reflect changes made by other cursors.
 *     SQL_INSENSITIVE: Cursors must not reflect changes made by other cursors.
 *     SQL_SENSITIVE: Cursors must reflect changes made by other cursors.
 *   SQL_ATTR_CURSOR_TYPE
 *     SQL_CURSOR_FORWARD_ONLY: The cursor can only scroll forward.
 *     SQL_CURSOR_STATIC: The cursor results are static.
 *     SQL_CURSOR_KEYSET_DRIVEN: Key values for the specified keyset size are saved and used by the driver.
 *     SQL_CURSOR_DYNAMIC: Key values for the specified rowset size are saved and used by the driver.
 *   SQL_ATTR_ENABLE_AUTO_IPD
 *     SQL_TRUE: Automatic implementation parameter descriptor population is enabled.
 *     SQL_FALSE: Automatic implementation parameter descriptor population is disabled.
 *   SQL_ATTR_FETCH_BOOKMARK_PTR (supply a pointer to a bookmark value)
 *   SQL_ATTR_IMP_PARAM_DESC (supply a descriptor handle, read-only)
 *   SQL_ATTR_IMP_ROW_DESC (supply a descriptor handle, read-only)
 *   SQL_ATTR_KEYSET_SIZE (supply an integer value)
 *   SQL_ATTR_MAX_LENGTH (supply an integer value)
 *   SQL_ATTR_MAX_ROWS (supply an integer value)
 *   SQL_ATTR_METADATA_ID
 *     SQL_TRUE: String arguments of catalog functions are treated as case-insensitive identifiers.
 *     SQL_FALSE: String arguments of catalog functions are treated as case-sensitive and allow search patterns.
 *   SQL_ATTR_NOSCAN
 *     SQL_NOSCAN_OFF: The driver parses escape sequences in query text.
 *     SQL_NOSCAN_ON: The driver sends the query text to the server without checking for escape sequences.
 *   SQL_ATTR_PARAM_BIND_OFFSET_PTR (supply an pointer to an integer value)
 *   SQL_ATTR_PARAM_BIND_TYPE (supply SQL_PARAM_BIND_BY_COLUMN or an integer value)
 *     SQL_PARAM_BIND_BY_COLUMN: Use column-wise binding for parameter sets.
 *   SQL_ATTR_PARAM_OPERATION_PTR (supply a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_PARAM_STATUS_PTR (supply a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_PARAMS_PROCESSED_PTR (supply a pointer to a buffer)
 *   SQL_ATTR_PARAMSET_SIZE (supply an integer value)
 *   SQL_ATTR_QUERY_TIMEOUT (supply an integer value)
 *   SQL_ATTR_RETRIEVE_DATA
 *     SQL_RD_ON: Retrieve data automatically when changing the cursor position.
 *     SQL_RD_OFF: Do not retrieve data when changing the cursor position.
 *   SQL_ATTR_ROW_ARRAY_SIZE (supply an integer value)
 *   SQL_ATTR_ROW_BIND_OFFSET_PTR (supply a pointer to an integer value)
 *   SQL_ATTR_ROW_BIND_TYPE (supply SQL_BIND_BY_COLUMN or an integer value)
 *     SQL_BIND_BY_COLUMN: Use column-wise binding for row data.
 *   SQL_ATTR_ROW_NUMBER (supply an integer value, read-only)
 *   SQL_ATTR_ROW_STATUS_PTR (supply a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_ROWS_FETCHED_PTR (supply a pointer to an integer value)
 *   SQL_ATTR_SIMULATE_CURSOR
 *     SQL_SC_NON_UNIQUE: The driver does not guarantee unique deletes and updates on a cursor.
 *     SQL_SC_TRY_UNIQUE: The driver tries to guarantee unique deletes and updates on a cursor.
 *     SQL_SC_UNIQUE: The driver guarantees unique deletes and updates on a cursor.
 *   SQL_ATTR_USE_BOOKMARKS
 *     SQL_UB_OFF: The cursor will not use bookmarks.
 *     SQL_UB_ON: The cursor will use bookmarks.
 *
 * Attempts to set the chosen attribute type to the value provided.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * Any other return value indicates failure.
 */
Handle<Value> ndbcSQLSetStmtAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen = 0;
  bool ok = true;

  // Translate string inputs into constant values.
  if (args[1]->ToString() == ndbcSQL_ATTR_APP_PARAM_DESC) {
    attrType = SQL_ATTR_APP_PARAM_DESC;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_APP_ROW_DESC) {
    attrType = SQL_ATTR_APP_ROW_DESC;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ASYNC_ENABLE) {
    attrType = SQL_ATTR_ASYNC_ENABLE;
    if (args[2]->ToString() == ndbcSQL_ASYNC_ENABLE_OFF) {
      attrVal = (SQLPOINTER) SQL_ASYNC_ENABLE_OFF;
    } else if (args[2]->ToString() == ndbcSQL_ASYNC_ENABLE_ON) {
      attrVal = (SQLPOINTER) SQL_ASYNC_ENABLE_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_SCROLLABLE) {
    attrType = SQL_ATTR_CURSOR_SCROLLABLE;
    if (args[2]->ToString() == ndbcSQL_NONSCROLLABLE) {
      attrVal = (SQLPOINTER) SQL_NONSCROLLABLE;
    } else if (args[2]->ToString() == ndbcSQL_SCROLLABLE) {
      attrVal = (SQLPOINTER) SQL_SCROLLABLE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_SENSITIVITY) {
    attrType = SQL_ATTR_CURSOR_SENSITIVITY;
    if (args[2]->ToString() == ndbcSQL_UNSPECIFIED) {
      attrVal = (SQLPOINTER) SQL_UNSPECIFIED;
    } else if (args[2]->ToString() == ndbcSQL_INSENSITIVE) {
      attrVal = (SQLPOINTER) SQL_INSENSITIVE;
    } else if (args[2]->ToString() == ndbcSQL_SENSITIVE) {
      attrVal = (SQLPOINTER) SQL_SENSITIVE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_TYPE) {
    attrType = SQL_ATTR_CURSOR_TYPE;
    if (args[2]->ToString() == ndbcSQL_CURSOR_FORWARD_ONLY) {
      attrVal = (SQLPOINTER) SQL_CURSOR_FORWARD_ONLY;
    } else if (args[2]->ToString() == ndbcSQL_CURSOR_STATIC) {
      attrVal = (SQLPOINTER) SQL_CURSOR_STATIC;
    } else if (args[2]->ToString() == ndbcSQL_CURSOR_KEYSET_DRIVEN) {
      attrVal = (SQLPOINTER) SQL_CURSOR_KEYSET_DRIVEN;
    } else if (args[2]->ToString() == ndbcSQL_CURSOR_DYNAMIC) {
      attrVal = (SQLPOINTER) SQL_CURSOR_DYNAMIC;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ENABLE_AUTO_IPD) {
    attrType = SQL_ATTR_ENABLE_AUTO_IPD;
    if (args[2]->ToString() == ndbcSQL_TRUE) {
      attrVal = (SQLPOINTER) SQL_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_FALSE) {
      attrVal = (SQLPOINTER) SQL_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_FETCH_BOOKMARK_PTR) {
    attrType = SQL_ATTR_FETCH_BOOKMARK_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_IMP_PARAM_DESC) {
    attrType = SQL_ATTR_IMP_PARAM_DESC;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_IMP_ROW_DESC) {
    attrType = SQL_ATTR_IMP_ROW_DESC;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_MAX_LENGTH) {
    attrType = SQL_ATTR_MAX_LENGTH;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_MAX_ROWS) {
    attrType = SQL_ATTR_MAX_ROWS;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_METADATA_ID) {
    attrType = SQL_ATTR_METADATA_ID;
    if (args[2]->ToString() == ndbcSQL_TRUE) {
      attrVal = (SQLPOINTER) SQL_TRUE;
    } else if (args[2]->ToString() == ndbcSQL_FALSE) {
      attrVal = (SQLPOINTER) SQL_FALSE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_NOSCAN) {
    attrType = SQL_ATTR_NOSCAN;
    if (args[2]->ToString() == ndbcSQL_NOSCAN_OFF) {
      attrVal = (SQLPOINTER) SQL_NOSCAN_OFF;
    } else if (args[2]->ToString() == ndbcSQL_NOSCAN_ON) {
      attrVal = (SQLPOINTER) SQL_NOSCAN_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_BIND_OFFSET_PTR) {
    attrType = SQL_ATTR_PARAM_BIND_OFFSET_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_BIND_TYPE) {
    attrType = SQL_ATTR_PARAM_BIND_TYPE;
    if (args[2]->IsNumber()) {
      attrVal = (SQLPOINTER) args[2]->Uint32Value();
    } else {
      if (args[2]->ToString() == ndbcSQL_PARAM_BIND_BY_COLUMN) {
        attrVal = (SQLPOINTER) SQL_PARAM_BIND_BY_COLUMN;
      } else {
        retVal = ndbcINVALID_ARGUMENT;
        ok = false;
      }
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_OPERATION_PTR) {
    attrType = SQL_ATTR_PARAM_OPERATION_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_STATUS_PTR) {
    attrType = SQL_ATTR_PARAM_STATUS_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAMS_PROCESSED_PTR) {
    attrType = SQL_ATTR_PARAMS_PROCESSED_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAMSET_SIZE) {
    attrType = SQL_ATTR_PARAMSET_SIZE;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_QUERY_TIMEOUT) {
    attrType = SQL_ATTR_QUERY_TIMEOUT;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_RETRIEVE_DATA) {
    attrType = SQL_ATTR_RETRIEVE_DATA;
    if (args[2]->ToString() == ndbcSQL_RD_OFF) {
      attrVal = (SQLPOINTER) SQL_RD_OFF;
    } else if (args[2]->ToString() == ndbcSQL_RD_ON) {
      attrVal = (SQLPOINTER) SQL_RD_ON;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_ARRAY_SIZE) {
    attrType = SQL_ATTR_ROW_ARRAY_SIZE;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_BIND_OFFSET_PTR) {
    attrType = SQL_ATTR_ROW_BIND_OFFSET_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_BIND_TYPE) {
    attrType = SQL_ATTR_ROW_BIND_TYPE;
    if (args[2]->IsNumber()) {
      attrVal = (SQLPOINTER) args[2]->Uint32Value();
    } else {
      if (args[2]->ToString() == ndbcSQL_BIND_BY_COLUMN) {
        attrVal = (SQLPOINTER) SQL_BIND_BY_COLUMN;
      } else {
        retVal = ndbcINVALID_ARGUMENT;
        ok = false;
      }
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_NUMBER) {
    attrType = SQL_ATTR_ROW_NUMBER;
    attrVal = (SQLPOINTER) args[2]->Uint32Value();
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_OPERATION_PTR) {
    attrType = SQL_ATTR_PARAM_OPERATION_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_STATUS_PTR) {
    attrType = SQL_ATTR_PARAM_STATUS_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROWS_FETCHED_PTR) {
    attrType = SQL_ATTR_ROWS_FETCHED_PTR;
    attrVal = (SQLPOINTER) External::Unwrap(args[2]);
  } else if (args[1]->ToString() == ndbcSQL_ATTR_SIMULATE_CURSOR) {
    attrType = SQL_ATTR_SIMULATE_CURSOR;
    if (args[2]->ToString() == ndbcSQL_SC_NON_UNIQUE) {
      attrVal = (SQLPOINTER) SQL_SC_NON_UNIQUE;
    } else if (args[2]->ToString() == ndbcSQL_SC_TRY_UNIQUE) {
      attrVal = (SQLPOINTER) SQL_SC_TRY_UNIQUE;
    } else if (args[2]->ToString() == ndbcSQL_SC_UNIQUE) {
      attrVal = (SQLPOINTER) SQL_SC_UNIQUE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else if (args[1]->ToString() == ndbcSQL_ATTR_USE_BOOKMARKS) {
    attrType = SQL_ATTR_USE_BOOKMARKS;
    if (args[2]->ToString() == ndbcSQL_UB_OFF) {
      attrVal = (SQLPOINTER) SQL_UB_OFF;
    } else if (args[2]->ToString() == ndbcSQL_UB_VARIABLE) {
      attrVal = (SQLPOINTER) SQL_UB_VARIABLE;
    } else {
      retVal = ndbcINVALID_ARGUMENT;
      ok = false;
    }
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLSetStmtAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      retVal = ndbcSQL_SUCCESS;
    }
  }
  
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLGetStmtAttr
 * SQLGetStmtAttr(statement, attribute, [length])
 * statement - An statement handle created with SQLAllocHandle.
 * attribute - The attribute to set.
 *             Strings representing API constants will be translated into their constant values.
 *   SQL_ATTR_APP_PARAM_DESC: The statement's parameter descriptor handle.
 *   SQL_ATTR_APP_ROW_DESC: The statement's row descriptor handle.
 *   SQL_ATTR_ASYNC_ENABLE: Indicates whether this statement runs asynchronously.
 *   SQL_ATTR_CONCURRENCY: Indicates the cursor concurrency associated with this statement.
 *   SQL_ATTR_CURSOR_SCROLLABLE: Indicates whether this statement has a scrollable cursor.
 *   SQL_ATTR_CURSOR_SENSITIVITY: Indicates whether this statement's cursor is sensitive to changes made by other cursors.
 *   SQL_ATTR_CURSOR_TYPE: Indicates the type of cursor associated with this statement.
 *   SQL_ATTR_ENABLE_AUTO_IPD: Indicates whether implementation parameter descriptors are automatically populated.
 *   SQL_ATTR_FETCH_BOOKMARK_PTR: The bookmark value that can be scrolled to using SQLFetchScroll.
 *   SQL_ATTR_IMP_PARAM_DESC: The implicit parameter descriptor that was set when this statement was allocated.  Read-only.
 *   SQL_ATTR_IMP_ROW_DESC: The implicit row descriptor that was set when this statement was allocated.  Read-only.
 *   SQL_ATTR_KEYSET_SIZE: The number of rows available in the keyset of a keyset-driven cursor.  Defaults to 0 (current row only).
 *   SQL_ATTR_MAX_LENGTH: The maximum length of data to return from a single column.  Defaults to 0 for no limit.
 *                        When set, this silently truncates data to the specified length.
 *   SQL_ATTR_MAX_ROWS: The maximum number of rows to return in a SELECT statement or catalog function.  Defaults to 0 for no limit.
 *                      When set, this silently truncates the result set to the specified number of rows.
 *   SQL_ATTR_METADATA_ID: Indicates whether the string arguments of catalog functions are treated as case-insensitive identifiers.
 *   SQL_ATTR_NOSCAN: Indicates whether to suppress scanning of query text for escape sequences.
 *   SQL_ATTR_PARAM_BIND_OFFSET_PTR: Indicates the offset to apply to binding dynamic parameters.
 *   SQL_ATTR_PARAM_BIND_TYPE: Indicates whether to bind parameter arrays by column or to bind by a specified length in a parameter buffer.
 *   SQL_ATTR_PARAM_OPERATION_PTR: Controls whether parameters are used or ignored when executing a statement.
 *   SQL_ATTR_PARAM_STATUS_PTR: Points to an array of return values for executed parameter sets.
 *   SQL_ATTR_PARAMS_PROCESSED_PTR: Points to a buffer that holds the number of parameter sets processed after executing.
 *   SQL_ATTR_PARAMSET_SIZE: Indicates the size of the parameter array being used.
 *   SQL_ATTR_QUERY_TIMEOUT: The timeout (in seconds) of this statement.
 *   SQL_ATTR_RETRIEVE_DATA: Indicates whether to automatically retrieve row data when the cursor position changes.
 *   SQL_ATTR_ROW_ARRAY_SIZE: The number of rows retrieved each time row data is fetched.
 *   SQL_ATTR_ROW_BIND_OFFSET_PTR: Indicates the offset to apply to binding column data.
 *   SQL_ATTR_ROW_BIND_TYPE: Indicates whether to bind row data by column or to bind by a specified length in a row data buffer.
 *   SQL_ATTR_ROW_NUMBER: The current row in the statement's result set.  Read-only.
 *   SQL_ATTR_ROW_STATUS_PTR: Points to an array holding row status values for fetched rows.
 *   SQL_ATTR_ROWS_FETCHED_PTR: Points to a buffer that holds the number of rows affected by the statement.
 *   SQL_ATTR_SIMULATE_CURSOR: Indicates whether cursor updates and deletes are guaranteed to affect only one row.
 *   SQL_ATTR_USE_BOOKMARKS: Indicates whether the statement's cursor will use bookmarks.
 * length - The maximum length of returned text data.  Defaults to 255.
 *
 * Returns the value of the specified attribute.  Possible return values for each attribute are:
 *   SQL_ATTR_APP_PARAM_DESC (returns a descriptor handle)
 *   SQL_ATTR_APP_ROW_DESC (returns a descriptor handle)
 *   SQL_ATTR_APP_ASYNC_ENABLE
 *     SQL_ASYNC_ENABLE_OFF: Statement will execute synchronously.
 *     SQL_ASYNC_ENABLE_ON: Statement will execute asynchronously.
 *   SQL_ATTR_CONCURRENCY
 *     SQL_CONCUR_READ_ONLY: Cursor is read-only.
 *     SQL_CONCUR_LOCK: Cursor locks when updating.
 *     SQL_CONCUR_ROWVER: Cursor handles concurrency optimistically using the row version.
 *     SQL_CONCUR_VALUES: Cursor handles concurrency optimistically using the row values.
 *   SQL_ATTR_CURSOR_SCROLLABLE
 *     SQL_NONSCROLLABLE: The cursor does not need to be able to scroll, it can only return the next row.
 *     SQL_SCROLLABLE: The cursor needs to be able to scroll in any direction.
 *   SQL_ATTR_CURSOR_SENSITIVITY
 *     SQL_UNSPECIFIED: Cursors may or may not reflect changes made by other cursors.
 *     SQL_INSENSITIVE: Cursors must not reflect changes made by other cursors.
 *     SQL_SENSITIVE: Cursors must reflect changes made by other cursors.
 *   SQL_ATTR_CURSOR_TYPE
 *     SQL_CURSOR_FORWARD_ONLY: The cursor can only scroll forward.
 *     SQL_CURSOR_STATIC: The cursor results are static.
 *     SQL_CURSOR_KEYSET_DRIVEN: Key values for the specified keyset size are saved and used by the driver.
 *     SQL_CURSOR_DYNAMIC: Key values for the specified rowset size are saved and used by the driver.
 *   SQL_ATTR_ENABLE_AUTO_IPD
 *     SQL_TRUE: Automatic implementation parameter descriptor population is enabled.
 *     SQL_FALSE: Automatic implementation parameter descriptor population is disabled.
 *   SQL_ATTR_FETCH_BOOKMARK_PTR (returns a pointer to a bookmark value)
 *   SQL_ATTR_IMP_PARAM_DESC (returns a descriptor handle, read-only)
 *   SQL_ATTR_IMP_ROW_DESC (returns a descriptor handle, read-only)
 *   SQL_ATTR_KEYSET_SIZE (returns an integer value)
 *   SQL_ATTR_MAX_LENGTH (returns an integer value)
 *   SQL_ATTR_MAX_ROWS (returns an integer value)
 *   SQL_ATTR_METADATA_ID
 *     SQL_TRUE: String arguments of catalog functions are treated as case-insensitive identifiers.
 *     SQL_FALSE: String arguments of catalog functions are treated as case-sensitive and allow search patterns.
 *   SQL_ATTR_NOSCAN
 *     SQL_NOSCAN_OFF: The driver parses escape sequences in query text.
 *     SQL_NOSCAN_ON: The driver sends the query text to the server without checking for escape sequences.
 *   SQL_ATTR_PARAM_BIND_OFFSET_PTR (returns an pointer to an integer value)
 *   SQL_ATTR_PARAM_BIND_TYPE (returns SQL_PARAM_BIND_BY_COLUMN or an integer value)
 *     SQL_PARAM_BIND_BY_COLUMN: Use column-wise binding for parameter sets.
 *   SQL_ATTR_PARAM_OPERATION_PTR (returns a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_PARAM_STATUS_PTR (returns a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_PARAMS_PROCESSED_PTR (returns a pointer to a buffer)
 *   SQL_ATTR_PARAMSET_SIZE (returns an integer value)
 *   SQL_ATTR_QUERY_TIMEOUT (returns an integer value)
 *   SQL_ATTR_RETRIEVE_DATA
 *     SQL_RD_ON: Retrieve data automatically when changing the cursor position.
 *     SQL_RD_OFF: Do not retrieve data when changing the cursor position.
 *   SQL_ATTR_ROW_ARRAY_SIZE (returns an integer value)
 *   SQL_ATTR_ROW_BIND_OFFSET_PTR (returns a pointer to an integer value)
 *   SQL_ATTR_ROW_BIND_TYPE (returns SQL_BIND_BY_COLUMN or an integer value)
 *     SQL_BIND_BY_COLUMN: Use column-wise binding for row data.
 *   SQL_ATTR_ROW_NUMBER (returns an integer value, read-only)
 *   SQL_ATTR_ROW_STATUS_PTR (returns a pointer to an array of SQLUSMALLINT values)
 *   SQL_ATTR_ROWS_FETCHED_PTR (returns a pointer to an integer value)
 *   SQL_ATTR_SIMULATE_CURSOR
 *     SQL_SC_NON_UNIQUE: The driver does not guarantee unique deletes and updates on a cursor.
 *     SQL_SC_TRY_UNIQUE: The driver tries to guarantee unique deletes and updates on a cursor.
 *     SQL_SC_UNIQUE: The driver guarantees unique deletes and updates on a cursor.
 *   SQL_ATTR_USE_BOOKMARKS
 *     SQL_UB_OFF: The cursor will not use bookmarks.
 *     SQL_UB_ON: The cursor will use bookmarks.
 *
 * Other returned values should be considered errors.
 */
Handle<Value> ndbcSQLGetStmtAttr(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLINTEGER attrType;
  SQLPOINTER attrVal;
  SQLINTEGER valLen;
  SQLINTEGER strLenPtr;
  bool ok = true;

  // Allocate return buffer.
  if (args.Length() == 3) {
    valLen = (SQLINTEGER) args[2]->Uint32Value();
  } else {
    valLen = 255;
  }
  attrVal = (SQLPOINTER) malloc(valLen + 1);

  // Translate string inputs into constant values.
  if (args[1]->ToString() == ndbcSQL_ATTR_APP_PARAM_DESC) {
    attrType = SQL_ATTR_APP_PARAM_DESC;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_APP_ROW_DESC) {
    attrType = SQL_ATTR_APP_ROW_DESC;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ASYNC_ENABLE) {
    attrType = SQL_ATTR_ASYNC_ENABLE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_SCROLLABLE) {
    attrType = SQL_ATTR_CURSOR_SCROLLABLE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_SENSITIVITY) {
    attrType = SQL_ATTR_CURSOR_SENSITIVITY;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_CURSOR_TYPE) {
    attrType = SQL_ATTR_CURSOR_TYPE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ENABLE_AUTO_IPD) {
    attrType = SQL_ATTR_ENABLE_AUTO_IPD;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_FETCH_BOOKMARK_PTR) {
    attrType = SQL_ATTR_FETCH_BOOKMARK_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_IMP_PARAM_DESC) {
    attrType = SQL_ATTR_IMP_PARAM_DESC;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_IMP_ROW_DESC) {
    attrType = SQL_ATTR_IMP_ROW_DESC;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_MAX_LENGTH) {
    attrType = SQL_ATTR_MAX_LENGTH;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_MAX_ROWS) {
    attrType = SQL_ATTR_MAX_ROWS;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_METADATA_ID) {
    attrType = SQL_ATTR_METADATA_ID;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_NOSCAN) {
    attrType = SQL_ATTR_NOSCAN;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_BIND_OFFSET_PTR) {
    attrType = SQL_ATTR_PARAM_BIND_OFFSET_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_BIND_TYPE) {
    attrType = SQL_ATTR_PARAM_BIND_TYPE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_OPERATION_PTR) {
    attrType = SQL_ATTR_PARAM_OPERATION_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAM_STATUS_PTR) {
    attrType = SQL_ATTR_PARAM_STATUS_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAMS_PROCESSED_PTR) {
    attrType = SQL_ATTR_PARAMS_PROCESSED_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_PARAMSET_SIZE) {
    attrType = SQL_ATTR_PARAMSET_SIZE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_QUERY_TIMEOUT) {
    attrType = SQL_ATTR_QUERY_TIMEOUT;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_RETRIEVE_DATA) {
    attrType = SQL_ATTR_RETRIEVE_DATA;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_ARRAY_SIZE) {
    attrType = SQL_ATTR_ROW_ARRAY_SIZE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_BIND_OFFSET_PTR) {
    attrType = SQL_ATTR_ROW_BIND_OFFSET_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_BIND_TYPE) {
    attrType = SQL_ATTR_ROW_BIND_TYPE;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_NUMBER) {
    attrType = SQL_ATTR_ROW_NUMBER;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_OPERATION_PTR) {
    attrType = SQL_ATTR_PARAM_OPERATION_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROW_STATUS_PTR) {
    attrType = SQL_ATTR_PARAM_STATUS_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_ROWS_FETCHED_PTR) {
    attrType = SQL_ATTR_ROWS_FETCHED_PTR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_SIMULATE_CURSOR) {
    attrType = SQL_ATTR_SIMULATE_CURSOR;
  } else if (args[1]->ToString() == ndbcSQL_ATTR_USE_BOOKMARKS) {
    attrType = SQL_ATTR_USE_BOOKMARKS;
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    switch (SQLGetStmtAttr((SQLHANDLE) External::Unwrap(args[0]), attrType, attrVal, valLen, &strLenPtr)) {
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      break;
    default:
      // Translate constant values into string outputs.
      switch (attrType) {
      case SQL_ATTR_APP_PARAM_DESC:
      case SQL_ATTR_APP_ROW_DESC:
      case SQL_ATTR_IMP_PARAM_DESC:
      case SQL_ATTR_IMP_ROW_DESC:
        retVal = External::Wrap((void*) *(SQLHANDLE*)attrVal);
        break;
      case SQL_ATTR_FETCH_BOOKMARK_PTR:
        retVal = External::Wrap((void*) *(SQLLEN**)attrVal);
        break;
      case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
      case SQL_ATTR_PARAMS_PROCESSED_PTR:
      case SQL_ATTR_ROW_BIND_OFFSET_PTR:
      case SQL_ATTR_ROWS_FETCHED_PTR:
        retVal = External::Wrap((void*) *(SQLULEN**)attrVal);
        break;
      case SQL_ATTR_PARAM_OPERATION_PTR:
      case SQL_ATTR_PARAM_STATUS_PTR:
      case SQL_ATTR_ROW_OPERATION_PTR:
      case SQL_ATTR_ROW_STATUS_PTR:
        retVal = External::Wrap((void*) *(SQLUSMALLINT**)attrVal);
        break;
      case SQL_ATTR_KEYSET_SIZE:
      case SQL_ATTR_MAX_LENGTH:
      case SQL_ATTR_MAX_ROWS:
      case SQL_ATTR_PARAMSET_SIZE:
      case SQL_ATTR_QUERY_TIMEOUT:
      case SQL_ATTR_ROW_ARRAY_SIZE:
      case SQL_ATTR_ROW_NUMBER:
        retVal = Integer::NewFromUnsigned(*(SQLULEN*)attrVal);
        break;
      case SQL_ATTR_ASYNC_ENABLE:
        switch (*(SQLULEN*) attrVal) {
        case SQL_ASYNC_ENABLE_OFF:
          retVal = ndbcSQL_ASYNC_ENABLE_OFF;
          break;
        case SQL_ASYNC_ENABLE_ON:
          retVal = ndbcSQL_ASYNC_ENABLE_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CONCURRENCY:
        switch (*(SQLULEN*) attrVal) {
        case SQL_CONCUR_READ_ONLY:
          retVal = ndbcSQL_CONCUR_READ_ONLY;
          break;
        case SQL_CONCUR_LOCK:
          retVal = ndbcSQL_CONCUR_LOCK;
          break;
        case SQL_CONCUR_ROWVER:
          retVal = ndbcSQL_CONCUR_ROWVER;
          break;
        case SQL_CONCUR_VALUES:
          retVal = ndbcSQL_CONCUR_VALUES;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CURSOR_SCROLLABLE:
        switch (*(SQLULEN*) attrVal) {
        case SQL_NONSCROLLABLE:
          retVal = ndbcSQL_NONSCROLLABLE;
          break;
        case SQL_SCROLLABLE:
          retVal = ndbcSQL_SCROLLABLE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CURSOR_SENSITIVITY:
        switch (*(SQLULEN*) attrVal) {
        case SQL_UNSPECIFIED:
          retVal = ndbcSQL_UNSPECIFIED;
          break;
        case SQL_INSENSITIVE:
          retVal = ndbcSQL_INSENSITIVE;
          break;
        case SQL_SENSITIVE:
          retVal = ndbcSQL_SENSITIVE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_CURSOR_TYPE:
        switch (*(SQLULEN*) attrVal) {
        case SQL_CURSOR_FORWARD_ONLY:
          retVal = ndbcSQL_CURSOR_FORWARD_ONLY;
          break;
        case SQL_CURSOR_STATIC:
          retVal = ndbcSQL_CURSOR_STATIC;
          break;
        case SQL_CURSOR_KEYSET_DRIVEN:
          retVal = ndbcSQL_CURSOR_KEYSET_DRIVEN;
          break;
        case SQL_CURSOR_DYNAMIC:
          retVal = ndbcSQL_CURSOR_DYNAMIC;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_ENABLE_AUTO_IPD:
      case SQL_ATTR_METADATA_ID:
        switch (*(SQLULEN*) attrVal) {
        case SQL_TRUE:
          retVal = ndbcSQL_TRUE;
          break;
        case SQL_FALSE:
          retVal = ndbcSQL_FALSE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_NOSCAN:
        switch (*(SQLULEN*) attrVal) {
        case SQL_NOSCAN_OFF:
          retVal = ndbcSQL_NOSCAN_OFF;
          break;
        case SQL_NOSCAN_ON:
          retVal = ndbcSQL_NOSCAN_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_PARAM_BIND_TYPE:
        switch (*(SQLULEN*) attrVal) {
        case SQL_PARAM_BIND_BY_COLUMN:
          retVal = ndbcSQL_PARAM_BIND_BY_COLUMN;
          break;
        default:
          retVal = Integer::NewFromUnsigned(*(SQLULEN*)attrVal);
        }
        break;
      case SQL_ATTR_RETRIEVE_DATA:
        switch (*(SQLULEN*) attrVal) {
        case SQL_RD_OFF:
          retVal = ndbcSQL_RD_OFF;
          break;
        case SQL_RD_ON:
          retVal = ndbcSQL_RD_ON;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_ROW_BIND_TYPE:
        switch (*(SQLULEN*) attrVal) {
        case SQL_BIND_BY_COLUMN:
          retVal = ndbcSQL_BIND_BY_COLUMN;
          break;
        default:
          retVal = Integer::NewFromUnsigned(*(SQLULEN*)attrVal);
        }
        break;
      case SQL_ATTR_SIMULATE_CURSOR:
        switch (*(SQLULEN*) attrVal) {
        case SQL_SC_NON_UNIQUE:
          retVal = ndbcSQL_SC_NON_UNIQUE;
          break;
        case SQL_SC_TRY_UNIQUE:
          retVal = ndbcSQL_SC_TRY_UNIQUE;
          break;
        case SQL_SC_UNIQUE:
          retVal = ndbcSQL_SC_UNIQUE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      case SQL_ATTR_USE_BOOKMARKS:
        switch (*(SQLULEN*) attrVal) {
        case SQL_UB_OFF:
          retVal = ndbcSQL_UB_OFF;
          break;
        case SQL_UB_VARIABLE:
          retVal = ndbcSQL_UB_VARIABLE;
          break;
        default:
          retVal = ndbcINVALID_RETURN;
        }
        break;
      default:
        retVal = ndbcINVALID_ARGUMENT;
      }
    }
  }

  free(attrVal);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLExecDirect
 * SQLExecDirect(statement, query)
 * statement - An statement handle created with SQLAllocHandle.
 * query - The query text to execute on the server.
 *
 * Attempts to run the specified query on the server.
 * Returns the string 'SQL_SUCCESS' if it succeeds.
 * If the query ran but affected nothing, 'SQL_NO_DATA' is returned.
 * If run asynchronously, it may return 'SQL_STILL_EXECUTING'.
 * If data needs to be supplied to the query while it is running, it may return 'SQL_NEED_DATA'.
 * Any other return value indicates failure.
 */
Handle<Value> ndbcSQLExecDirect(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLCHAR* query;
  SQLINTEGER queryLen;

  String::AsciiValue rawVal(args[1]->ToString());
  query = (SQLCHAR*) *rawVal;
  queryLen = rawVal.length();
  
  switch (SQLExecDirect((SQLHANDLE) External::Unwrap(args[0]), query, queryLen)) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  case SQL_NEED_DATA:
    retVal = ndbcSQL_NEED_DATA;
    break;
  case SQL_STILL_EXECUTING:
    retVal = ndbcSQL_STILL_EXECUTING;
    break;
  case SQL_NO_DATA:
    retVal = ndbcSQL_NO_DATA;
    break;
  case SQL_PARAM_DATA_AVAILABLE:
    retVal = ndbcSQL_PARAM_DATA_AVAILABLE;
    break;
  default:
    retVal = ndbcSQL_SUCCESS;
  }
  
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* Mapping for SQLRowCount
 * SQLRowCount(statement)
 * statement - An statement handle created with SQLAllocHandle.
 *
 * Retrieves the number of rows affected by the completed statement on supplied statement handle.
 * Returns the row count if it succeeds.
 * Any other return value indicates failure (SQL_ERROR, SQL_INVALID_HANDLE).
 */
Handle<Value> ndbcSQLRowCount(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {
  SQLLEN* rowCount = (SQLLEN*) malloc(sizeof(SQLLEN));

  switch (SQLRowCount((SQLHANDLE) External::Unwrap(args[0]), rowCount)) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  default:
    retVal = Integer::New(*rowCount);
  }
  
  free(rowCount);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* ndbc custom function ndbcJsonDescribe
 * ndbcJsonDescribe(statement)
 * statement - An statement handle that has an available result set.
 * 
 * Returns a string value describing how to bind result set data when returning rows via ndbcJsonData.
 * Designed to run synchronously.
 * Returns SQL_ERROR if there are any problems.
 * The returned string contains the following header information:
 *   columns: The character 'c' followed by a number representing the number of output columns.
 *   length: The character 'l' followed by the number of bytes required to represent a single row of data in Json format.
 *           This includes extra space for escape sequences, literal delimiters, and enclosing braces.
 * The returned string contains text for each column in the following format:
 *   serialize: A character representing how to serialize the column's data.
 *     q: Quoted, the data will be enclosed by double quotes and formatted into a string literal.
 *        Control characters 0x00-0x1f are replaced by spaces.
 *        Special character escape sequences \ to \\ and " to \" are applied.
 *     b: Quoted, the data will be enclosed by double quotes and encoded in base64 format.
 *     n: Not quoted, the data will not be enclosed by anything and will not be formatted.
 *   length: A number representing the maximum byte length of this field's output.
 *           This does not include extra space for escape sequences or null termination.
 */
Handle<Value> ndbcJsonDescribe(const Arguments& args) {
  HandleScope scope;
  Local<String> retVal;
try {
  Local<String> columnDesc = String::New("");
  SQLSMALLINT* columns = (SQLSMALLINT*) malloc(sizeof(SQLSMALLINT*));
  SQLUSMALLINT i;
  SQLULEN recLen = 2;
  bool ok = true;

  SQLSMALLINT* dataType = (SQLSMALLINT*) malloc(sizeof(SQLSMALLINT*));
  SQLULEN* dataLen = (SQLULEN*) malloc(sizeof(SQLULEN*));

  // Call SQLNumResultCols to get the number of columns in the result set
  switch (SQLNumResultCols((SQLHANDLE) External::Unwrap(args[0]), columns)) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  case SQL_STILL_EXECUTING:
    retVal = ndbcSQL_STILL_EXECUTING;
    break;
  default:
    // Encode the column count to the output
    retVal = String::New("c");
    retVal = String::Concat(retVal, Integer::New(*columns)->ToString());
    // Call SQLDescribeCol for each column in the result set
    for (i = 1; i <= *columns && ok; i++) {
      // Encode the column description into a placeholder V8 string and increment the total record length
      switch (SQLDescribeCol((SQLHANDLE) External::Unwrap(args[0]), i, NULL, 0, NULL, dataType, dataLen, NULL, NULL)) {
      case SQL_ERROR:
        retVal = ndbcSQL_ERROR;
        ok = false;
        break;
      case SQL_INVALID_HANDLE:
        retVal = ndbcSQL_INVALID_HANDLE;
        ok = false;
        break;
      case SQL_STILL_EXECUTING:
        retVal = ndbcSQL_STILL_EXECUTING;
        ok = false;
        break;
      default:
        // Minimum output length is 5 for NULL data represented as 'null' plus a comma.
        switch (*dataType) {
        case SQL_DECIMAL:
        case SQL_NUMERIC:
          // Add in 2 for a decimal point and a sign.
          *dataLen += 2;
          // Add 1 for a comma, and make sure the resulting length is at least 5.
          recLen += ((*dataLen > 4) ? *dataLen : 4) + 1;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_BIT:
          *dataLen = 1;
          // Minimum output length is 5.
          recLen += 5;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_TINYINT:
          *dataLen = 4;
          // Add 1 for a comma.
          recLen += 5;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_SMALLINT:
          *dataLen = 6;
          recLen += 7;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_INTEGER:
          *dataLen = 11;
          recLen += 12;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_BIGINT:
          *dataLen = 20;
          recLen += 21;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_REAL:
          *dataLen = 14;
          recLen += 15;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_FLOAT:
        case SQL_DOUBLE:
          *dataLen = 24;
          recLen += 25;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("n"));
          break;
        case SQL_CHAR:
        case SQL_VARCHAR:
        case SQL_LONGVARCHAR:
        case SQL_WCHAR:
        case SQL_WVARCHAR:
        case SQL_WLONGVARCHAR:
          // Double length for escape sequences, add 3 for quotes and a comma.
          recLen += (*dataLen * 2) + 3;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("q"));
          break;
        case SQL_TYPE_DATE:
        case SQL_TYPE_TIME:
        case SQL_TYPE_TIMESTAMP:
        case SQL_INTERVAL_MONTH:
        case SQL_INTERVAL_YEAR:
        case SQL_INTERVAL_YEAR_TO_MONTH:
        case SQL_INTERVAL_DAY:
        case SQL_INTERVAL_HOUR:
        case SQL_INTERVAL_MINUTE:
        case SQL_INTERVAL_SECOND:
        case SQL_INTERVAL_DAY_TO_HOUR:
        case SQL_INTERVAL_DAY_TO_MINUTE:
        case SQL_INTERVAL_DAY_TO_SECOND:
        case SQL_INTERVAL_HOUR_TO_MINUTE:
        case SQL_INTERVAL_HOUR_TO_SECOND:
        case SQL_INTERVAL_MINUTE_TO_SECOND:
        case SQL_GUID:
          // Add 1 to data length to fix date length bug
          *dataLen += 1;
          // Add 3 for quotes and a comma
          recLen += *dataLen + 3;
          columnDesc = String::Concat(columnDesc, String::NewSymbol("q"));
          break;
        case SQL_BINARY:
        case SQL_VARBINARY:
        case SQL_LONGVARBINARY:
        case SQL_UNKNOWN_TYPE:
        default:
          // Multiply length by 4/3 for base64 encoding, add 3 for quotes and a comma.
          recLen += ((*dataLen / 3) * 4) + 3;
          // Remaindered source data will add another 4 bytes to the base64 encoded length.
          if (*dataLen % 3 != 0) {
            recLen += 4;
          }
          columnDesc = String::Concat(columnDesc, String::NewSymbol("q"));
        }
        columnDesc = String::Concat(columnDesc, Integer::New(*dataLen)->ToString());
      }
    }
    if (ok) {
      // Encode the total record length to the output
      retVal = String::Concat(retVal, String::New("l"));
      retVal = String::Concat(retVal, Integer::New(recLen)->ToString());
      // Concatenate the column description string to the output
      retVal = String::Concat(retVal, columnDesc);
    }
  }
  
  free(columns);
  free(dataType);
  free(dataLen);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* ndbc custom function ndbcJsonHeader
 * ndbcJsonHeader(statement)
 * statement - An statement handle that has an available result set.
 *
 * Returns a Json-formatted array with column names for the result set.
 * Returns a leading [ character to begin the array of records.
 */
Handle<Value> ndbcJsonHeader(const Arguments& args) {
  HandleScope scope;
  Local<String> retVal;
try {
  SQLSMALLINT* columns = (SQLSMALLINT*) malloc(sizeof(SQLSMALLINT*));
  SQLCHAR* colName;

  colName = (SQLCHAR*) malloc(256);

  SQLUSMALLINT i;
  bool ok = true;

  // Call SQLNumResultCols to get the number of columns in the result set
  switch (SQLNumResultCols((SQLHANDLE) External::Unwrap(args[0]), columns)) {
  case SQL_ERROR:
    retVal = ndbcSQL_ERROR;
    break;
  case SQL_INVALID_HANDLE:
    retVal = ndbcSQL_INVALID_HANDLE;
    break;
  case SQL_STILL_EXECUTING:
    retVal = ndbcSQL_STILL_EXECUTING;
    break;
  default:
    // Write the header array to the output
    retVal = String::New("[[");
    // Call SQLDescribeCol for each column in the result set
    switch (SQLDescribeCol((SQLHANDLE) External::Unwrap(args[0]), 1, colName, 255, NULL, NULL, NULL, NULL, NULL)){
    case SQL_ERROR:
      retVal = ndbcSQL_ERROR;
      ok = false;
      break;
    case SQL_INVALID_HANDLE:
      retVal = ndbcSQL_INVALID_HANDLE;
      ok = false;
      break;
    case SQL_STILL_EXECUTING:
      retVal = ndbcSQL_STILL_EXECUTING;
      ok = false;
      break;
    default:
      retVal = String::Concat(retVal, String::New("\""));
      retVal = String::Concat(retVal, String::New((char*) colName));
      retVal = String::Concat(retVal, String::New("\""));
    }
    for (i = 2; i <= *columns && ok; i++) {
      switch (SQLDescribeCol((SQLHANDLE) External::Unwrap(args[0]), i, colName, 255, NULL, NULL, NULL, NULL, NULL)) {
      case SQL_ERROR:
        retVal = ndbcSQL_ERROR;
        ok = false;
        break;
      case SQL_INVALID_HANDLE:
        retVal = ndbcSQL_INVALID_HANDLE;
        ok = false;
        break;
      case SQL_STILL_EXECUTING:
        retVal = ndbcSQL_STILL_EXECUTING;
        ok = false;
        break;
      default:
        retVal = String::Concat(retVal, String::New(",\""));
        retVal = String::Concat(retVal, String::New((char*) colName));
        retVal = String::Concat(retVal, String::New("\""));
      }
    }
    if (ok) {
      // Add the closing brace
      retVal = String::Concat(retVal, String::New("]"));
    }
  }
  
  free(columns);
  free(colName);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* ndbc custom function ndbcJsonData
 * ndbcJsonData(statement, rowdesc, [rows])
 * statement - An statement handle that has an available result set.
 * rowdesc - A string describing the row format produced by ndbcJsonDescribe.
 * rows - The number of rows to output.  Defaults to 1.
 *
 * Returns a Json-formatted array with column data for the next <length> rows in the result set.
 * Returns truncated results if the number of remaining rows is less than the number of requested rows.
 * Returns a leading comma before each row of data to continue the array of records (assumes this will be
 * concatenated with the results of ndbcJsonHeader).
 * Returns a trailing ']' if there is no more result set data, to close the array of records.
 * Returns SQL_NO_DATA if the end of the result set has already been reached.
 */
Handle<Value> ndbcJsonData(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {

  SQLSMALLINT columns = 0;
  SQLINTEGER recLen = 0;
  SQLUSMALLINT i;
  SQLUSMALLINT j;
  SQLINTEGER k;
  SQLUSMALLINT l;
  SQLUINTEGER r = 0;
  SQLUINTEGER rows;
  SQLUINTEGER colLen;
  char* recData;
  char* serialize;
  SQLCHAR** rowData;
  SQLLEN** rowInd;
  SQLUINTEGER* rowLen;
  bool ok = true;
  bool data = true;
  char* rowDesc;

  if (args.Length() == 3) {
    rows = (SQLUINTEGER) args[2]->Uint32Value();
  } else {
    rows = 1;
  }

  // Parse the row description string.
  String::AsciiValue rawVal(args[1]->ToString());
  rowDesc = (char*) *rawVal;
  if (rowDesc[0] == 'c') {
    // Extract the column count from rowDesc.
    for (i = 1; rowDesc[i] > 47 && rowDesc[i] < 58; i++) {
      columns *= 10;
      columns += rowDesc[i] - 48;
    }
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok && rowDesc[i] == 'l') {
    // Extract the record length from rowDesc.
    for (i++; rowDesc[i] > 47 && rowDesc[i] < 58; i++) {
      recLen *= 10;
      recLen += rowDesc[i] - 48;
    }
    retVal = Integer::New(recLen);
  } else {
    retVal = ndbcINVALID_ARGUMENT;
    ok = false;
  }
  if (ok) {
    // Allocate output and serialization buffers.
    recData = (char*) malloc((recLen * rows) + 1); // Add 1 for null termination
    serialize = (char*) malloc(columns + 1); // Add 1 for null termination
    serialize[columns] = 0; // Manually supply null terminating character for serialize string.
    rowData = (SQLCHAR**) malloc(sizeof(SQLCHAR*) * columns);
    rowInd = (SQLLEN**) malloc(sizeof(SQLLEN*) * columns);
    rowLen = (SQLUINTEGER*) malloc(sizeof(SQLUINTEGER) * columns);
    j = 0;
    while (ok && rowDesc[i] != 0 && j < columns) {
      if (rowDesc[i] == 'q' || rowDesc[i] == 'b' || rowDesc[i] == 'n') {
        // Copy serialization type to the serialization buffer.
        serialize[j] = rowDesc[i];
        // Extract the field's buffer size from rowDesc.
        colLen= 0;
        for (i++; rowDesc[i] > 47 && rowDesc[i] < 58; i++) {
          colLen *= 10;
          colLen += rowDesc[i] - 48;
        }
        // Allocate the column output buffer.
        rowData[j] = (SQLCHAR*) malloc(colLen + 1); // Add 1 for null termination.
        rowInd[j] = (SQLLEN*) malloc(sizeof(SQLLEN));
        rowLen[j] = colLen;
        // Bind the column output buffer to the result set column.
        switch (SQLBindCol((SQLHANDLE) External::Unwrap(args[0]), j + 1, SQL_C_CHAR, (SQLPOINTER) rowData[j], colLen, rowInd[j])) {
        case SQL_ERROR:
          retVal = ndbcSQL_ERROR;
          ok = false;
          break;
        case SQL_INVALID_HANDLE:
          retVal = ndbcSQL_INVALID_HANDLE;
          ok = false;
          break;
        }
        j++;
      } else {
        retVal = ndbcINVALID_ARGUMENT;
        ok = false;
        break;
      }
    }
  }
  retVal = Integer::NewFromUnsigned(j);
  
  if (ok) {
    recData[0] = 0;
    k = 0;
    // Fetch the specified number of rows.
    for (i = 0; ok && data && i < rows; i++) {
      switch (SQLFetch((SQLHANDLE) External::Unwrap(args[0]))) {
      case SQL_ERROR:
        retVal = ndbcSQL_ERROR;
        ok = false;
        break;
      case SQL_INVALID_HANDLE:
        retVal = ndbcSQL_INVALID_HANDLE;
        ok = false;
        break;
      case SQL_STILL_EXECUTING:
        retVal = ndbcSQL_STILL_EXECUTING;
        ok = false;
        break;
      case SQL_NO_DATA:
        // Return SQL_NO_DATA if no data has been read so far.
        if (k > 0) {
          data = false;
        } else {
          retVal = ndbcSQL_NO_DATA;
          ok = false;
        }
        break;
      default:
        // Write a preceding comma and begin the row array.
        recData[k] = ',';
        k++;
        recData[k] = '[';
        k++;
        // Write the data array to the output.
        for (j = 0; j < columns; j++) {
          // Check for nulls.
          if (*rowInd[j] == SQL_NULL_DATA) {
            recData[k] = 'n';
            k++;
            recData[k] = 'u';
            k++;
            recData[k] = 'l';
            k++;
            recData[k] = 'l';
            k++;
          } else {
            switch (serialize[j]) {
            case 'q':
              recData[k] = '\"';
              k++;
              // Transcribe text data using escape sequences and discarding control characters.
              for (l = 0; l < *rowInd[j]; l++) {
                if (rowData[j][l] < 32 || (rowData[j][l] > 126 && rowData[j][l] < 160)) {
                  // Map non-printable characters to spaces.
                  recData[k] = ' ';
                  k++;
                } else if (rowData[j][l] == '\"' || rowData[j][l] == '\\') {
                  // Apply escape sequence to " and \ characters.
                  recData[k] = '\\';
                  k++;
                  recData[k] = rowData[j][l];
                  k++;
                } else {
                  // Copy other data over verbatim.
                  recData[k] = rowData[j][l];
                  k++;
                }
              }
              recData[k] = '\"';
              k++;
              break;
            case 'b':
              recData[k] = '\"';
              k++;
              // Transcribe binary data using base64 encoding.
              recData[k] = '\"';
              k++;
              break;
            case 'n':
              // Transcribe numeric data verbatim.
              for (l = 0; l < *rowInd[j]; l++) {
                recData[k] = rowData[j][l];
                k++;
              }
            }
          }
          recData[k] = ',';
          k++;
        }
        // Overwrite the last comma and terminate the row array.
        recData[k-1] = ']';
      }
    }
    // Add null terminating character to output Json data.
    recData[k] = 0;
  }
  
  if (ok) {
    // Copy the formatted data to the output.
    retVal = String::New(recData, k);
  }
  // Free allocated memory resources.
  for (j = 0; j < columns; j++) {
    free(rowData[j]);
    free(rowInd[j]);
  }
  free(rowData);
  free(rowInd);
  free(serialize);
  free(recData);
  free(rowLen);
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

/* ndbc custom function ndbcJsonTrailer
 * ndbcJsonTrailer(statement)
 * statement - An statement handle that has an available result set.
 *
 * Returns a trailing ] character to begin the array of records.
 * Technically, the argument isn't required, but you can supply it if you like.
 */
Handle<Value> ndbcJsonTrailer(const Arguments& args) {
  HandleScope scope;
  Local<Value> retVal;
try {

  retVal = String::New("]");
  
}
catch (...) {
  retVal = ndbcINTERNAL_ERROR;
}
  return scope.Close(retVal);
}

void init(Handle<Object> target) {
  target->Set(String::NewSymbol("SQLAllocHandle"),
              FunctionTemplate::New(ndbcSQLAllocHandle)->GetFunction());
  target->Set(String::NewSymbol("SQLFreeHandle"),
              FunctionTemplate::New(ndbcSQLFreeHandle)->GetFunction());
  target->Set(String::NewSymbol("SQLSetEnvAttr"),
              FunctionTemplate::New(ndbcSQLSetEnvAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLGetEnvAttr"),
              FunctionTemplate::New(ndbcSQLGetEnvAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLConnect"),
              FunctionTemplate::New(ndbcSQLConnect)->GetFunction());
  target->Set(String::NewSymbol("SQLDisconnect"),
              FunctionTemplate::New(ndbcSQLDisconnect)->GetFunction());
  target->Set(String::NewSymbol("SQLSetConnectAttr"),
              FunctionTemplate::New(ndbcSQLSetConnectAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLGetConnectAttr"),
              FunctionTemplate::New(ndbcSQLGetConnectAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLGetInfo"),
              FunctionTemplate::New(ndbcSQLGetInfo)->GetFunction());
  target->Set(String::NewSymbol("SQLSetStmtAttr"),
              FunctionTemplate::New(ndbcSQLSetStmtAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLGetStmtAttr"),
              FunctionTemplate::New(ndbcSQLGetStmtAttr)->GetFunction());
  target->Set(String::NewSymbol("SQLExecDirect"),
              FunctionTemplate::New(ndbcSQLExecDirect)->GetFunction());
  target->Set(String::NewSymbol("SQLRowCount"),
              FunctionTemplate::New(ndbcSQLRowCount)->GetFunction());
  target->Set(String::NewSymbol("JsonDescribe"),
              FunctionTemplate::New(ndbcJsonDescribe)->GetFunction());
  target->Set(String::NewSymbol("JsonHeader"),
              FunctionTemplate::New(ndbcJsonHeader)->GetFunction());
  target->Set(String::NewSymbol("JsonData"),
              FunctionTemplate::New(ndbcJsonData)->GetFunction());
  target->Set(String::NewSymbol("JsonTrailer"),
              FunctionTemplate::New(ndbcJsonTrailer)->GetFunction());
}
NODE_MODULE(ndbc, init)

/* Undefine string representations of ODBC constants.
 */
#undef ndbcSQL_ACCESSIBLE_PROCEDURES
#undef ndbcSQL_ACCESSIBLE_TABLES
#undef ndbcSQL_ACTIVE_ENVIRONMENTS
#undef ndbcSQL_AD_ADD_CONSTRAINT_DEFERRABLE
#undef ndbcSQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED
#undef ndbcSQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE
#undef ndbcSQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE
#undef ndbcSQL_AD_ADD_DOMAIN_CONSTRAINT
#undef ndbcSQL_AD_ADD_DOMAIN_DEFAULT
#undef ndbcSQL_AD_CONSTRAINT_NAME_DEFINITION
#undef ndbcSQL_AD_DROP_DOMAIN_CONSTRAINT
#undef ndbcSQL_AD_DROP_DOMAIN_DEFAULT
#undef ndbcSQL_AF_ALL
#undef ndbcSQL_AF_AVG
#undef ndbcSQL_AF_COUNT
#undef ndbcSQL_AF_DISTINCT
#undef ndbcSQL_AF_MAX
#undef ndbcSQL_AF_MIN
#undef ndbcSQL_AF_SUM
#undef ndbcSQL_AGGREGATE_FUNCTIONS
#undef ndbcSQL_ALTER_DOMAIN
#undef ndbcSQL_ALTER_TABLE
#undef ndbcSQL_AM_CONNECTION
#undef ndbcSQL_AM_NONE
#undef ndbcSQL_AM_STATEMENT
#undef ndbcSQL_ASYNC_DBC_CAPABLE
#undef ndbcSQL_ASYNC_DBC_FUNCTIONS
#undef ndbcSQL_ASYNC_DBC_NOT_CAPABLE
#undef ndbcSQL_ASYNC_ENABLE_OFF
#undef ndbcSQL_ASYNC_ENABLE_ON
#undef ndbcSQL_ASYNC_MODE
#undef ndbcSQL_AT_ADD_COLUMN_COLLATION
#undef ndbcSQL_AT_ADD_COLUMN_DEFAULT
#undef ndbcSQL_AT_ADD_COLUMN_SINGLE
#undef ndbcSQL_AT_ADD_CONSTRAINT
#undef ndbcSQL_AT_ADD_TABLE_CONSTRAINT
#undef ndbcSQL_AT_CONSTRAINT_DEFERRABLE
#undef ndbcSQL_AT_CONSTRAINT_INITIALLY_DEFERRED
#undef ndbcSQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE
#undef ndbcSQL_AT_CONSTRAINT_NAME_DEFINITION
#undef ndbcSQL_AT_CONSTRAINT_NON_DEFERRABLE
#undef ndbcSQL_AT_DROP_COLUMN_CASCADE
#undef ndbcSQL_AT_DROP_COLUMN_DEFAULT
#undef ndbcSQL_AT_DROP_COLUMN_RESTRICT
#undef ndbcSQL_AT_DROP_TABLE_CONSTRAINT_CASCADE
#undef ndbcSQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT
#undef ndbcSQL_AT_SET_COLUMN_DEFAULT
#undef ndbcSQL_ATTR_ACCESS_MODE
#undef ndbcSQL_ATTR_APP_PARAM_DESC
#undef ndbcSQL_ATTR_APP_ROW_DESC
#undef ndbcSQL_ATTR_ASYNC_ENABLE
#undef ndbcSQL_ATTR_AUTO_IPD
#undef ndbcSQL_ATTR_AUTOCOMMIT
#undef ndbcSQL_ATTR_CONCURRENCY
#undef ndbcSQL_ATTR_CONNECTION_DEAD
#undef ndbcSQL_ATTR_CONNECTION_POOLING
#undef ndbcSQL_ATTR_CONNECTION_TIMEOUT
#undef ndbcSQL_ATTR_CP_MATCH
#undef ndbcSQL_ATTR_CURRENT_CATALOG
#undef ndbcSQL_ATTR_CURSOR_SCROLLABLE
#undef ndbcSQL_ATTR_CURSOR_SENSITIVITY
#undef ndbcSQL_ATTR_CURSOR_TYPE
#undef ndbcSQL_ATTR_ENABLE_AUTO_IPD
#undef ndbcSQL_ATTR_ENLIST_IN_DTC
#undef ndbcSQL_ATTR_FETCH_BOOKMARK_PTR
#undef ndbcSQL_ATTR_IMP_PARAM_DESC
#undef ndbcSQL_ATTR_IMP_ROW_DESC
#undef ndbcSQL_ATTR_KEYSET_SIZE
#undef ndbcSQL_ATTR_LOGIN_TIMEOUT
#undef ndbcSQL_ATTR_MAX_LENGTH
#undef ndbcSQL_ATTR_MAX_ROWS
#undef ndbcSQL_ATTR_METADATA_ID
#undef ndbcSQL_ATTR_NOSCAN
#undef ndbcSQL_ATTR_ODBC_CURSORS
#undef ndbcSQL_ATTR_ODBC_VERSION
#undef ndbcSQL_ATTR_OUTPUT_NTS
#undef ndbcSQL_ATTR_PACKET_SIZE
#undef ndbcSQL_ATTR_PARAM_BIND_OFFSET_PTR
#undef ndbcSQL_ATTR_PARAM_BIND_TYPE
#undef ndbcSQL_ATTR_PARAM_OPERATION_PTR
#undef ndbcSQL_ATTR_PARAM_STATUS_PTR
#undef ndbcSQL_ATTR_PARAMS_PROCESSED_PTR
#undef ndbcSQL_ATTR_PARAMSET_SIZE
#undef ndbcSQL_ATTR_QUERY_TIMEOUT
#undef ndbcSQL_ATTR_QUIET_MODE
#undef ndbcSQL_ATTR_RETRIEVE_DATA
#undef ndbcSQL_ATTR_ROW_ARRAY_SIZE
#undef ndbcSQL_ATTR_ROW_BIND_OFFSET_PTR
#undef ndbcSQL_ATTR_ROW_BIND_TYPE
#undef ndbcSQL_ATTR_ROW_NUMBER
#undef ndbcSQL_ATTR_ROW_OPERATION_PTR
#undef ndbcSQL_ATTR_ROW_STATUS_PTR
#undef ndbcSQL_ATTR_ROWS_FETCHED_PTR
#undef ndbcSQL_ATTR_SIMULATE_CURSOR
#undef ndbcSQL_ATTR_TRACE
#undef ndbcSQL_ATTR_TRACEFILE
#undef ndbcSQL_ATTR_TRANSLATE_LIB
#undef ndbcSQL_ATTR_TRANSLATE_OPTION
#undef ndbcSQL_ATTR_TXN_ISOLATION
#undef ndbcSQL_ATTR_USE_BOOKMARKS
#undef ndbcSQL_AUTOCOMMIT_OFF
#undef ndbcSQL_AUTOCOMMIT_ON
#undef ndbcSQL_BATCH_ROW_COUNT
#undef ndbcSQL_BATCH_SUPPORT
#undef ndbcSQL_BIND_BY_COLUMN
#undef ndbcSQL_BOOKMARK_PERSISTENCE
#undef ndbcSQL_BP_CLOSE
#undef ndbcSQL_BP_DELETE
#undef ndbcSQL_BP_DROP
#undef ndbcSQL_BP_OTHER_HSTMT
#undef ndbcSQL_BP_TRANSACTION
#undef ndbcSQL_BP_UPDATE
#undef ndbcSQL_BRC_EXPLICIT
#undef ndbcSQL_BRC_PROCEDURES
#undef ndbcSQL_BRC_ROLLED_UP
#undef ndbcSQL_BS_ROW_COUNT_EXPLICIT
#undef ndbcSQL_BS_ROW_COUNT_PROC
#undef ndbcSQL_BS_SELECT_EXPLICIT
#undef ndbcSQL_BS_SELECT_PROC
#undef ndbcSQL_CA_CONSTRAINT_DEFERRABLE
#undef ndbcSQL_CA_CONSTRAINT_INITIALLY_DEFERRED
#undef ndbcSQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE
#undef ndbcSQL_CA_CONSTRAINT_NON_DEFERRABLE
#undef ndbcSQL_CA_CREATE_ASSERTION
#undef ndbcSQL_CA1_ABSOLUTE
#undef ndbcSQL_CA1_BOOKMARK
#undef ndbcSQL_CA1_BULK_ADD
#undef ndbcSQL_CA1_BULK_DELETE_BY_BOOKMARK
#undef ndbcSQL_CA1_BULK_FETCH_BY_BOOKMARK
#undef ndbcSQL_CA1_BULK_UPDATE_BY_BOOKMARK
#undef ndbcSQL_CA1_LOCK_EXCLUSIVE
#undef ndbcSQL_CA1_LOCK_NO_CHANGE
#undef ndbcSQL_CA1_LOCK_UNLOCK
#undef ndbcSQL_CA1_NEXT
#undef ndbcSQL_CA1_POS_DELETE
#undef ndbcSQL_CA1_POS_POSITION
#undef ndbcSQL_CA1_POS_REFRESH
#undef ndbcSQL_CA1_POS_UPDATE
#undef ndbcSQL_CA1_POSITIONED_DELETE
#undef ndbcSQL_CA1_POSITIONED_UPDATE
#undef ndbcSQL_CA1_RELATIVE
#undef ndbcSQL_CA1_SELECT_FOR_UPDATE
#undef ndbcSQL_CA2_CRC_APPROXIMATE
#undef ndbcSQL_CA2_CRC_EXACT
#undef ndbcSQL_CA2_LOCK_CONCURRENCY
#undef ndbcSQL_CA2_MAX_ROWS_AFFECTS_ALL
#undef ndbcSQL_CA2_MAX_ROWS_CATALOG
#undef ndbcSQL_CA2_MAX_ROWS_DELETE
#undef ndbcSQL_CA2_MAX_ROWS_INSERT
#undef ndbcSQL_CA2_MAX_ROWS_SELECT
#undef ndbcSQL_CA2_MAX_ROWS_UPDATE
#undef ndbcSQL_CA2_OPT_ROWVER_CONCURRENCY
#undef ndbcSQL_CA2_OPT_VALUES_CONCURRENCY
#undef ndbcSQL_CA2_READ_ONLY_CONCURRENCY
#undef ndbcSQL_CA2_SENSITIVITY_ADDITIONS
#undef ndbcSQL_CA2_SENSITIVITY_DELETIONS
#undef ndbcSQL_CA2_SENSITIVITY_UPDATES
#undef ndbcSQL_CA2_SIMULATE_NON_UNIQUE
#undef ndbcSQL_CA2_SIMULATE_TRY_UNIQUE
#undef ndbcSQL_CA2_SIMULATE_UNIQUE
#undef ndbcSQL_CATALOG_LOCATION
#undef ndbcSQL_CATALOG_NAME
#undef ndbcSQL_CATALOG_NAME_SEPARATOR
#undef ndbcSQL_CATALOG_TERM
#undef ndbcSQL_CATALOG_USAGE
#undef ndbcSQL_CB_CLOSE
#undef ndbcSQL_CB_DELETE
#undef ndbcSQL_CB_NON_NULL
#undef ndbcSQL_CB_NULL
#undef ndbcSQL_CB_PRESERVE
#undef ndbcSQL_CCOL_CREATE_COLLATION
#undef ndbcSQL_CCS_COLLATE_CLAUSE
#undef ndbcSQL_CCS_CREATE_CHARACTER_SET
#undef ndbcSQL_CCS_LIMITED_COLLATION
#undef ndbcSQL_CD_FALSE
#undef ndbcSQL_CD_TRUE
#undef ndbcSQL_CDO_COLLATION
#undef ndbcSQL_CDO_CONSTRAINT
#undef ndbcSQL_CDO_CONSTRAINT_DEFERRABLE
#undef ndbcSQL_CDO_CONSTRAINT_INITIALLY_DEFERRED
#undef ndbcSQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE
#undef ndbcSQL_CDO_CONSTRAINT_NAME_DEFINITION
#undef ndbcSQL_CDO_CONSTRAINT_NON_DEFERRABLE
#undef ndbcSQL_CDO_CREATE_DOMAIN
#undef ndbcSQL_CDO_DEFAULT
#undef ndbcSQL_CL_END
#undef ndbcSQL_CL_NOT_SUPPORTED
#undef ndbcSQL_CL_START
#undef ndbcSQL_CN_ANY
#undef ndbcSQL_CN_DIFFERENT
#undef ndbcSQL_CN_NONE
#undef ndbcSQL_COLLATION_SEQ
#undef ndbcSQL_COLUMN_ALIAS
#undef ndbcSQL_CONCAT_NULL_BEHAVIOR
#undef ndbcSQL_CONCUR_LOCK
#undef ndbcSQL_CONCUR_READ_ONLY
#undef ndbcSQL_CONCUR_ROWVER
#undef ndbcSQL_CONCUR_VALUES
#undef ndbcSQL_CONVERT_BIGINT
#undef ndbcSQL_CONVERT_BINARY
#undef ndbcSQL_CONVERT_BIT
#undef ndbcSQL_CONVERT_CHAR
#undef ndbcSQL_CONVERT_DATE
#undef ndbcSQL_CONVERT_DECIMAL
#undef ndbcSQL_CONVERT_DOUBLE
#undef ndbcSQL_CONVERT_FLOAT
#undef ndbcSQL_CONVERT_FUNCTIONS
#undef ndbcSQL_CONVERT_GUID
#undef ndbcSQL_CONVERT_INTEGER
#undef ndbcSQL_CONVERT_INTERVAL_DAY_TIME
#undef ndbcSQL_CONVERT_INTERVAL_YEAR_MONTH
#undef ndbcSQL_CONVERT_LONGVARBINARY
#undef ndbcSQL_CONVERT_LONGVARCHAR
#undef ndbcSQL_CONVERT_NUMERIC
#undef ndbcSQL_CONVERT_REAL
#undef ndbcSQL_CONVERT_SMALLINT
#undef ndbcSQL_CONVERT_TIME
#undef ndbcSQL_CONVERT_TIMESTAMP
#undef ndbcSQL_CONVERT_TINYINT
#undef ndbcSQL_CONVERT_VARBINARY
#undef ndbcSQL_CONVERT_VARCHAR
#undef ndbcSQL_CORRELATION_NAME
#undef ndbcSQL_CP_OFF
#undef ndbcSQL_CP_ONE_PER_DRIVER
#undef ndbcSQL_CP_ONE_PER_HENV
#undef ndbcSQL_CP_RELAXED_MATCH
#undef ndbcSQL_CP_STRICT_MATCH
#undef ndbcSQL_CREATE_ASSERTION
#undef ndbcSQL_CREATE_CHARACTER_SET
#undef ndbcSQL_CREATE_COLLATION
#undef ndbcSQL_CREATE_DOMAIN
#undef ndbcSQL_CREATE_SCHEMA
#undef ndbcSQL_CREATE_TABLE
#undef ndbcSQL_CREATE_TRANSLATION
#undef ndbcSQL_CREATE_VIEW
#undef ndbcSQL_CS_AUTHORIZATION
#undef ndbcSQL_CS_CREATE_SCHEMA
#undef ndbcSQL_CS_DEFAULT_CHARACTER_SET
#undef ndbcSQL_CT_COLUMN_COLLATION
#undef ndbcSQL_CT_COLUMN_CONSTRAINT
#undef ndbcSQL_CT_COLUMN_DEFAULT
#undef ndbcSQL_CT_COMMIT_DELETE
#undef ndbcSQL_CT_COMMIT_PRESERVE
#undef ndbcSQL_CT_CONSTRAINT_DEFERRABLE
#undef ndbcSQL_CT_CONSTRAINT_INITIALLY_DEFERRED
#undef ndbcSQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE
#undef ndbcSQL_CT_CONSTRAINT_NAME_DEFINITION
#undef ndbcSQL_CT_CONSTRAINT_NON_DEFERRABLE
#undef ndbcSQL_CT_CREATE_TABLE
#undef ndbcSQL_CT_GLOBAL_TEMPORARY
#undef ndbcSQL_CT_LOCAL_TEMPORARY
#undef ndbcSQL_CT_TABLE_CONSTRAINT
#undef ndbcSQL_CTR_CREATE_TRANSLATION
#undef ndbcSQL_CU_CATALOGS_NOT_SUPPORTED
#undef ndbcSQL_CU_DML_STATEMENTS
#undef ndbcSQL_CU_INDEX_DEFINITION
#undef ndbcSQL_CU_PRIVILEGE_DEFINITION
#undef ndbcSQL_CU_PROCEDURE_INVOCATION
#undef ndbcSQL_CU_TABLE_DEFINITION
#undef ndbcSQL_CUR_USE_DRIVER
#undef ndbcSQL_CUR_USE_IF_NEEDED
#undef ndbcSQL_CUR_USE_ODBC
#undef ndbcSQL_CURSOR_COMMIT_BEHAVIOR
#undef ndbcSQL_CURSOR_DYNAMIC
#undef ndbcSQL_CURSOR_FORWARD_ONLY
#undef ndbcSQL_CURSOR_KEYSET_DRIVEN
#undef ndbcSQL_CURSOR_ROLLBACK_BEHAVIOR
#undef ndbcSQL_CURSOR_SENSITIVITY
#undef ndbcSQL_CURSOR_STATIC
#undef ndbcSQL_CV_CASCADED
#undef ndbcSQL_CV_CHECK_OPTION
#undef ndbcSQL_CV_CREATE_VIEW
#undef ndbcSQL_CV_LOCAL
#undef ndbcSQL_CVT_BIGINT
#undef ndbcSQL_CVT_BINARY
#undef ndbcSQL_CVT_BIT
#undef ndbcSQL_CVT_CHAR
#undef ndbcSQL_CVT_DATE
#undef ndbcSQL_CVT_DECIMAL
#undef ndbcSQL_CVT_DOUBLE
#undef ndbcSQL_CVT_FLOAT
#undef ndbcSQL_CVT_GUID
#undef ndbcSQL_CVT_INTEGER
#undef ndbcSQL_CVT_INTERVAL_DAY_TIME
#undef ndbcSQL_CVT_INTERVAL_YEAR_MONTH
#undef ndbcSQL_CVT_LONGVARBINARY
#undef ndbcSQL_CVT_LONGVARCHAR
#undef ndbcSQL_CVT_NUMERIC
#undef ndbcSQL_CVT_REAL_ODBC
#undef ndbcSQL_CVT_SMALLINT
#undef ndbcSQL_CVT_TIME
#undef ndbcSQL_CVT_TIMESTAMP
#undef ndbcSQL_CVT_TINYINT
#undef ndbcSQL_CVT_VARBINARY
#undef ndbcSQL_CVT_VARCHAR
#undef ndbcSQL_DA_DROP_ASSERTION
#undef ndbcSQL_DATA_SOURCE_NAME
#undef ndbcSQL_DATA_SOURCE_READ_ONLY
#undef ndbcSQL_DATABASE_NAME
#undef ndbcSQL_DATETIME_LITERALS
#undef ndbcSQL_DBMS_NAME
#undef ndbcSQL_DBMS_VER
#undef ndbcSQL_DC_DROP_COLLATION
#undef ndbcSQL_DCS_DROP_CHARACTER_SET
#undef ndbcSQL_DD_CASCADE
#undef ndbcSQL_DD_DROP_DOMAIN
#undef ndbcSQL_DD_RESTRICT
#undef ndbcSQL_DDL_INDEX
#undef ndbcSQL_DEFAULT_TXN_ISOLATION
#undef ndbcSQL_DESCRIBE_PARAMETER
#undef ndbcSQL_DI_CREATE_INDEX
#undef ndbcSQL_DI_DROP_INDEX
#undef ndbcSQL_DL_SQL92_DATE
#undef ndbcSQL_DL_SQL92_INTERVAL_DAY
#undef ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_HOUR
#undef ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE
#undef ndbcSQL_DL_SQL92_INTERVAL_DAY_TO_SECOND
#undef ndbcSQL_DL_SQL92_INTERVAL_HOUR
#undef ndbcSQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE
#undef ndbcSQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND
#undef ndbcSQL_DL_SQL92_INTERVAL_MINUTE
#undef ndbcSQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND
#undef ndbcSQL_DL_SQL92_INTERVAL_MONTH
#undef ndbcSQL_DL_SQL92_INTERVAL_SECOND
#undef ndbcSQL_DL_SQL92_INTERVAL_YEAR
#undef ndbcSQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH
#undef ndbcSQL_DL_SQL92_TIME
#undef ndbcSQL_DL_SQL92_TIMESTAMP
#undef ndbcSQL_DM_VER
#undef ndbcSQL_DRIVER_HDBC
#undef ndbcSQL_DRIVER_HDESC
#undef ndbcSQL_DRIVER_HENV
#undef ndbcSQL_DRIVER_HLIB
#undef ndbcSQL_DRIVER_HSTMT
#undef ndbcSQL_DRIVER_NAME
#undef ndbcSQL_DRIVER_ODBC_VER
#undef ndbcSQL_DRIVER_VER
#undef ndbcSQL_DROP_ASSERTION
#undef ndbcSQL_DROP_CHARACTER_SET
#undef ndbcSQL_DROP_COLLATION
#undef ndbcSQL_DROP_DOMAIN
#undef ndbcSQL_DROP_SCHEMA
#undef ndbcSQL_DROP_TABLE
#undef ndbcSQL_DROP_TRANSLATION
#undef ndbcSQL_DROP_VIEW
#undef ndbcSQL_DS_CASCADE
#undef ndbcSQL_DS_DROP_SCHEMA
#undef ndbcSQL_DS_RESTRICT
#undef ndbcSQL_DT_CASCADE
#undef ndbcSQL_DT_DROP_TABLE
#undef ndbcSQL_DT_RESTRICT
#undef ndbcSQL_DTC_DONE
#undef ndbcSQL_DTR_DROP_TRANSLATION
#undef ndbcSQL_DV_CASCADE
#undef ndbcSQL_DV_DROP_VIEW
#undef ndbcSQL_DV_RESTRICT
#undef ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES1
#undef ndbcSQL_DYNAMIC_CURSOR_ATTRIBUTES2
#undef ndbcSQL_ERROR
#undef ndbcSQL_EXPRESSIONS_IN_ORDERBY
#undef ndbcSQL_FALSE
#undef ndbcSQL_FILE_CATALOG
#undef ndbcSQL_FILE_NOT_SUPPORTED
#undef ndbcSQL_FILE_TABLE
#undef ndbcSQL_FILE_USAGE
#undef ndbcSQL_FN_CVT_CAST
#undef ndbcSQL_FN_CVT_CONVERT
#undef ndbcSQL_FN_NUM_ABS
#undef ndbcSQL_FN_NUM_ACOS
#undef ndbcSQL_FN_NUM_ASIN
#undef ndbcSQL_FN_NUM_ATAN
#undef ndbcSQL_FN_NUM_ATAN2
#undef ndbcSQL_FN_NUM_CEILING
#undef ndbcSQL_FN_NUM_COS
#undef ndbcSQL_FN_NUM_COT
#undef ndbcSQL_FN_NUM_DEGREES
#undef ndbcSQL_FN_NUM_EXP
#undef ndbcSQL_FN_NUM_FLOOR
#undef ndbcSQL_FN_NUM_LOG
#undef ndbcSQL_FN_NUM_LOG10
#undef ndbcSQL_FN_NUM_MOD
#undef ndbcSQL_FN_NUM_PI
#undef ndbcSQL_FN_NUM_POWER
#undef ndbcSQL_FN_NUM_RADIANS
#undef ndbcSQL_FN_NUM_RAND
#undef ndbcSQL_FN_NUM_ROUND
#undef ndbcSQL_FN_NUM_SIGN
#undef ndbcSQL_FN_NUM_SIN
#undef ndbcSQL_FN_NUM_SQRT
#undef ndbcSQL_FN_NUM_TAN
#undef ndbcSQL_FN_NUM_TRUNCATE
#undef ndbcSQL_FN_STR_ASCII
#undef ndbcSQL_FN_STR_BIT_LENGTH
#undef ndbcSQL_FN_STR_CHAR
#undef ndbcSQL_FN_STR_CHAR_LENGTH
#undef ndbcSQL_FN_STR_CHARACTER_LENGTH
#undef ndbcSQL_FN_STR_CONCAT
#undef ndbcSQL_FN_STR_DIFFERENCE
#undef ndbcSQL_FN_STR_INSERT
#undef ndbcSQL_FN_STR_LCASE
#undef ndbcSQL_FN_STR_LEFT
#undef ndbcSQL_FN_STR_LENGTH
#undef ndbcSQL_FN_STR_LOCATE
#undef ndbcSQL_FN_STR_LOCATE_2
#undef ndbcSQL_FN_STR_LTRIM
#undef ndbcSQL_FN_STR_OCTET_LENGTH
#undef ndbcSQL_FN_STR_POSITION
#undef ndbcSQL_FN_STR_REPEAT
#undef ndbcSQL_FN_STR_REPLACE
#undef ndbcSQL_FN_STR_RIGHT
#undef ndbcSQL_FN_STR_RTRIM
#undef ndbcSQL_FN_STR_SOUNDEX
#undef ndbcSQL_FN_STR_SPACE
#undef ndbcSQL_FN_STR_SUBSTRING
#undef ndbcSQL_FN_STR_UCASE
#undef ndbcSQL_FN_SYS_DBNAME
#undef ndbcSQL_FN_SYS_IFNULL
#undef ndbcSQL_FN_SYS_USERNAME
#undef ndbcSQL_FN_TD_CURDATE
#undef ndbcSQL_FN_TD_CURRENT_DATE
#undef ndbcSQL_FN_TD_CURRENT_TIME
#undef ndbcSQL_FN_TD_CURRENT_TIMESTAMP
#undef ndbcSQL_FN_TD_CURTIME
#undef ndbcSQL_FN_TD_DAYNAME
#undef ndbcSQL_FN_TD_DAYOFMONTH
#undef ndbcSQL_FN_TD_DAYOFWEEK
#undef ndbcSQL_FN_TD_DAYOFYEAR
#undef ndbcSQL_FN_TD_EXTRACT
#undef ndbcSQL_FN_TD_HOUR
#undef ndbcSQL_FN_TD_MINUTE
#undef ndbcSQL_FN_TD_MONTH
#undef ndbcSQL_FN_TD_MONTHNAME
#undef ndbcSQL_FN_TD_NOW
#undef ndbcSQL_FN_TD_QUARTER
#undef ndbcSQL_FN_TD_SECOND
#undef ndbcSQL_FN_TD_TIMESTAMPADD
#undef ndbcSQL_FN_TD_TIMESTAMPDIFF
#undef ndbcSQL_FN_TD_WEEK
#undef ndbcSQL_FN_TD_YEAR
#undef ndbcSQL_FN_TSI_DAY
#undef ndbcSQL_FN_TSI_FRAC_SECOND
#undef ndbcSQL_FN_TSI_HOUR
#undef ndbcSQL_FN_TSI_MINUTE
#undef ndbcSQL_FN_TSI_MONTH
#undef ndbcSQL_FN_TSI_QUARTER
#undef ndbcSQL_FN_TSI_SECOND
#undef ndbcSQL_FN_TSI_WEEK
#undef ndbcSQL_FN_TSI_YEAR
#undef ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1
#undef ndbcSQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2
#undef ndbcSQL_GB_COLLATE
#undef ndbcSQL_GB_GROUP_BY_CONTAINS_SELECT
#undef ndbcSQL_GB_GROUP_BY_EQUALS_SELECT
#undef ndbcSQL_GB_NO_RELATION
#undef ndbcSQL_GB_NOT_SUPPORTED
#undef ndbcSQL_GD_ANY_COLUMN
#undef ndbcSQL_GD_ANY_ORDER
#undef ndbcSQL_GD_BLOCK
#undef ndbcSQL_GD_BOUND
#undef ndbcSQL_GD_OUTPUT_PARAMS
#undef ndbcSQL_GETDATA_EXTENSIONS
#undef ndbcSQL_GROUP_BY
#undef ndbcSQL_HANDLE_DBC
#undef ndbcSQL_HANDLE_DESC
#undef ndbcSQL_HANDLE_ENV
#undef ndbcSQL_HANDLE_STMT
#undef ndbcSQL_IC_LOWER
#undef ndbcSQL_IC_MIXED
#undef ndbcSQL_IC_SENSITIVE
#undef ndbcSQL_IC_UPPER
#undef ndbcSQL_IDENTIFIER_CASE
#undef ndbcSQL_IDENTIFIER_QUOTE_CHAR
#undef ndbcSQL_IK_ALL
#undef ndbcSQL_IK_ASC
#undef ndbcSQL_IK_DESC
#undef ndbcSQL_IK_NONE
#undef ndbcSQL_INDEX_KEYWORDS
#undef ndbcSQL_INFO_SCHEMA_VIEWS
#undef ndbcSQL_INSENSITIVE
#undef ndbcSQL_INSERT_STATEMENT
#undef ndbcSQL_INTEGRITY
#undef ndbcSQL_INVALID_HANDLE
#undef ndbcSQL_IS_INSERT_LITERALS
#undef ndbcSQL_IS_INSERT_SEARCHED
#undef ndbcSQL_IS_SELECT_INTO
#undef ndbcSQL_ISV_ASSERTIONS
#undef ndbcSQL_ISV_CHARACTER_SETS
#undef ndbcSQL_ISV_CHECK_CONSTRAINTS
#undef ndbcSQL_ISV_COLLATIONS
#undef ndbcSQL_ISV_COLUMN_DOMAIN_USAGE
#undef ndbcSQL_ISV_COLUMN_PRIVILEGES
#undef ndbcSQL_ISV_COLUMNS
#undef ndbcSQL_ISV_CONSTRAINT_COLUMN_USAGE
#undef ndbcSQL_ISV_CONSTRAINT_TABLE_USAGE
#undef ndbcSQL_ISV_DOMAIN_CONSTRAINTS
#undef ndbcSQL_ISV_DOMAINS
#undef ndbcSQL_ISV_KEY_COLUMN_USAGE
#undef ndbcSQL_ISV_REFERENTIAL_CONSTRAINTS
#undef ndbcSQL_ISV_SCHEMATA
#undef ndbcSQL_ISV_SQL_LANGUAGES
#undef ndbcSQL_ISV_TABLE_CONSTRAINTS
#undef ndbcSQL_ISV_TABLE_PRIVILEGES
#undef ndbcSQL_ISV_TABLES
#undef ndbcSQL_ISV_TRANSLATIONS
#undef ndbcSQL_ISV_USAGE_PRIVILEGES
#undef ndbcSQL_ISV_VIEW_COLUMN_USAGE
#undef ndbcSQL_ISV_VIEW_TABLE_USAGE
#undef ndbcSQL_ISV_VIEWS
#undef ndbcSQL_KEYSET_CURSOR_ATTRIBUTES1
#undef ndbcSQL_KEYSET_CURSOR_ATTRIBUTES2
#undef ndbcSQL_KEYWORDS
#undef ndbcSQL_LIKE_ESCAPE_CLAUSE
#undef ndbcSQL_MAX_ASYNC_CONCURRENT_STATEMENTS
#undef ndbcSQL_MAX_BINARY_LITERAL_LEN
#undef ndbcSQL_MAX_CATALOG_NAME_LEN
#undef ndbcSQL_MAX_CHAR_LITERAL_LEN
#undef ndbcSQL_MAX_COLUMN_NAME_LEN
#undef ndbcSQL_MAX_COLUMNS_IN_GROUP_BY
#undef ndbcSQL_MAX_COLUMNS_IN_INDEX
#undef ndbcSQL_MAX_COLUMNS_IN_ORDER_BY
#undef ndbcSQL_MAX_COLUMNS_IN_SELECT
#undef ndbcSQL_MAX_COLUMNS_IN_TABLE
#undef ndbcSQL_MAX_CONCURRENT_ACTIVITIES
#undef ndbcSQL_MAX_CURSOR_NAME_LEN
#undef ndbcSQL_MAX_DRIVER_CONNECTIONS
#undef ndbcSQL_MAX_IDENTIFIER_LEN
#undef ndbcSQL_MAX_INDEX_SIZE
#undef ndbcSQL_MAX_PROCEDURE_NAME_LEN
#undef ndbcSQL_MAX_ROW_SIZE
#undef ndbcSQL_MAX_ROW_SIZE_INCLUDES_LONG
#undef ndbcSQL_MAX_SCHEMA_NAME_LEN
#undef ndbcSQL_MAX_STATEMENT_LEN
#undef ndbcSQL_MAX_TABLE_NAME_LEN
#undef ndbcSQL_MAX_TABLES_IN_SELECT
#undef ndbcSQL_MAX_USER_NAME_LEN
#undef ndbcSQL_MODE_READ_ONLY
#undef ndbcSQL_MODE_READ_WRITE
#undef ndbcSQL_MULT_RESULT_SETS
#undef ndbcSQL_MULTIPLE_ACTIVE_TXN
#undef ndbcSQL_NC_END
#undef ndbcSQL_NC_HIGH
#undef ndbcSQL_NC_LOW
#undef ndbcSQL_NC_START
#undef ndbcSQL_NEED_DATA
#undef ndbcSQL_NEED_LONG_DATA_LEN
#undef ndbcSQL_NNC_NON_NULL
#undef ndbcSQL_NNC_NULL
#undef ndbcSQL_NO_DATA
#undef ndbcSQL_NON_NULLABLE_COLUMNS
#undef ndbcSQL_NONSCROLLABLE
#undef ndbcSQL_NOSCAN_OFF
#undef ndbcSQL_NOSCAN_ON
#undef ndbcSQL_NULL_COLLATION
#undef ndbcSQL_NUMERIC_FUNCTIONS
#undef ndbcSQL_ODBC_INTERFACE_CONFORMANCE
#undef ndbcSQL_ODBC_VER
#undef ndbcSQL_OIC_CORE
#undef ndbcSQL_OIC_LEVEL1
#undef ndbcSQL_OIC_LEVEL2
#undef ndbcSQL_OJ_ALL_COMPARISON_OPS
#undef ndbcSQL_OJ_CAPABILITIES
#undef ndbcSQL_OJ_FULL
#undef ndbcSQL_OJ_INNTER
#undef ndbcSQL_OJ_LEFT
#undef ndbcSQL_OJ_NESTED
#undef ndbcSQL_OJ_NOT_ORDERED
#undef ndbcSQL_OJ_RIGHT
#undef ndbcSQL_OPT_TRACE_OFF
#undef ndbcSQL_OPT_TRACE_ON
#undef ndbcSQL_ORDER_BY_COLUMNS_IN_SELECT
#undef ndbcSQL_OV_ODBC2
#undef ndbcSQL_OV_ODBC3
#undef ndbcSQL_OV_ODBC3_80
#undef ndbcSQL_PARAM_ARRAY_ROW_COUNTS
#undef ndbcSQL_PARAM_ARRAY_SELECTS
#undef ndbcSQL_PARAM_BIND_BY_COLUMN
#undef ndbcSQL_PARAM_DATA_AVAILABLE
#undef ndbcSQL_PARAM_DIAG_UNAVAILABLE
#undef ndbcSQL_PARAM_ERROR
#undef ndbcSQL_PARAM_IGNORE
#undef ndbcSQL_PARAM_PROCEED
#undef ndbcSQL_PARAM_SUCCESS
#undef ndbcSQL_PARAM_SUCCESS_WITH_INFO
#undef ndbcSQL_PARAM_UNUSED
#undef ndbcSQL_PARC_BATCH
#undef ndbcSQL_PARC_NO_BATCH
#undef ndbcSQL_PAS_BATCH
#undef ndbcSQL_PAS_NO_BATCH
#undef ndbcSQL_PAS_NO_SELECT
#undef ndbcSQL_POS_ADD
#undef ndbcSQL_POS_DELETE
#undef ndbcSQL_POS_OPERATIONS
#undef ndbcSQL_POS_POSITION
#undef ndbcSQL_POS_REFRESH
#undef ndbcSQL_POS_UPDATE
#undef ndbcSQL_PROCEDURE_TERM
#undef ndbcSQL_PROCEDURES
#undef ndbcSQL_QUOTED_IDENTIFIER_CASE
#undef ndbcSQL_RD_OFF
#undef ndbcSQL_RD_ON
#undef ndbcSQL_ROW_IGNORE
#undef ndbcSQL_ROW_PROCEED
#undef ndbcSQL_ROW_UPDATES
#undef ndbcSQL_SC_FIPS127_2_TRANSITIONAL
#undef ndbcSQL_SC_NON_UNIQUE
#undef ndbcSQL_SC_SQL92_ENTRY
#undef ndbcSQL_SC_SQL92_FULL
#undef ndbcSQL_SC_SQL92_INTERMEDIATE
#undef ndbcSQL_SC_TRY_UNIQUE
#undef ndbcSQL_SC_UNIQUE
#undef ndbcSQL_SCC_ISO92_CLI
#undef ndbcSQL_SCC_XOPEN_CLI_VERSION1
#undef ndbcSQL_SCHEMA_TERM
#undef ndbcSQL_SCHEMA_USAGE
#undef ndbcSQL_SCROLL_OPTIONS
#undef ndbcSQL_SCROLLABLE
#undef ndbcSQL_SDF_CURRENT_DATE
#undef ndbcSQL_SDF_CURRENT_TIME
#undef ndbcSQL_SDF_CURRENT_TIMESTAMP
#undef ndbcSQL_SEARCH_PATTERN_ESCAPE
#undef ndbcSQL_SENSITIVE
#undef ndbcSQL_SERVER_NAME
#undef ndbcSQL_SFKD_CASCADE
#undef ndbcSQL_SFKD_NO_ACTION
#undef ndbcSQL_SFKD_SET_DEFAULT
#undef ndbcSQL_SFKD_SET_NULL
#undef ndbcSQL_SFKU_CASCADE
#undef ndbcSQL_SFKU_NO_ACTION
#undef ndbcSQL_SFKU_SET_DEFAULT
#undef ndbcSQL_SFKU_SET_NULL
#undef ndbcSQL_SG_DELETE_TABLE
#undef ndbcSQL_SG_INSERT_COLUMN
#undef ndbcSQL_SG_INSERT_TABLE
#undef ndbcSQL_SG_REFERENCES_COLUMN
#undef ndbcSQL_SG_REFERENCES_TABLE
#undef ndbcSQL_SG_SELECT_TABLE
#undef ndbcSQL_SG_UPDATE_COLUMN
#undef ndbcSQL_SG_UPDATE_TABLE
#undef ndbcSQL_SG_USAGE_ON_CHARACTER_SET
#undef ndbcSQL_SG_USAGE_ON_COLLATION
#undef ndbcSQL_SG_USAGE_ON_DOMAIN
#undef ndbcSQL_SG_USAGE_ON_TRANSLATION
#undef ndbcSQL_SG_WITH_GRANT_OPTION
#undef ndbcSQL_SNVF_BIT_LENGTH
#undef ndbcSQL_SNVF_CHAR_LENGTH
#undef ndbcSQL_SNVF_CHARACTER_LENGTH
#undef ndbcSQL_SNVF_EXTRACT
#undef ndbcSQL_SNVF_OCTET_LENGTH
#undef ndbcSQL_SNVF_POSITION
#undef ndbcSQL_SO_DYNAMIC
#undef ndbcSQL_SO_FORWARD_ONLY
#undef ndbcSQL_SO_KEYSET_DRIVEN
#undef ndbcSQL_SO_MIXED
#undef ndbcSQL_SO_STATIC
#undef ndbcSQL_SP_BETWEEN
#undef ndbcSQL_SP_COMPARISON
#undef ndbcSQL_SP_EXISTS
#undef ndbcSQL_SP_IN
#undef ndbcSQL_SP_ISNOTNULL
#undef ndbcSQL_SP_ISNULL
#undef ndbcSQL_SP_LIKE
#undef ndbcSQL_SP_MATCH_FULL
#undef ndbcSQL_SP_MATCH_PARTIAL
#undef ndbcSQL_SP_MATCH_UNIQUE_FULL
#undef ndbcSQL_SP_MATCH_UNIQUE_PARTIAL
#undef ndbcSQL_SP_OVERLAPS
#undef ndbcSQL_SP_QUANTIFIED_COMPARISON
#undef ndbcSQL_SP_UNIQUE
#undef ndbcSQL_SPECIAL_CHARACTERS
#undef ndbcSQL_SQ_COMPARISON
#undef ndbcSQL_SQ_CORRELATED_SUBQUERIES
#undef ndbcSQL_SQ_EXISTS
#undef ndbcSQL_SQ_IN
#undef ndbcSQL_SQ_QUANTIFIED
#undef ndbcSQL_SQL_CONFORMANCE
#undef ndbcSQL_SQL92_DATETIME_FUNCTIONS
#undef ndbcSQL_SQL92_FOREIGN_KEY_DELETE_RULE
#undef ndbcSQL_SQL92_FOREIGN_KEY_UPDATE_RULE
#undef ndbcSQL_SQL92_GRANT
#undef ndbcSQL_SQL92_NUMERIC_VALUE_FUNCTIONS
#undef ndbcSQL_SQL92_PREDICATES
#undef ndbcSQL_SQL92_RELATIONAL_JOIN_OPERATORS
#undef ndbcSQL_SQL92_REVOKE
#undef ndbcSQL_SQL92_ROW_VALUE_CONSTRUCTOR
#undef ndbcSQL_SQL92_STRING_FUNCTIONS
#undef ndbcSQL_SQL92_VALUE_EXPRESSIONS
#undef ndbcSQL_SR_CASCADE
#undef ndbcSQL_SR_DELETE_TABLE
#undef ndbcSQL_SR_GRANT_OPTION_FOR
#undef ndbcSQL_SR_INSERT_COLUMN
#undef ndbcSQL_SR_INSERT_TABLE
#undef ndbcSQL_SR_REFERENCES_COLUMN
#undef ndbcSQL_SR_REFERENCES_TABLE
#undef ndbcSQL_SR_RESTRICT
#undef ndbcSQL_SR_SELECT_TABLE
#undef ndbcSQL_SR_UPDATE_COLUMN
#undef ndbcSQL_SR_UPDATE_TABLE
#undef ndbcSQL_SR_USAGE_ON_CHARACTER_SET
#undef ndbcSQL_SR_USAGE_ON_COLLATION
#undef ndbcSQL_SR_USAGE_ON_DOMAIN
#undef ndbcSQL_SR_USAGE_ON_TRANSLATION
#undef ndbcSQL_SRJO_CORRESPONDING_CLAUSE
#undef ndbcSQL_SRJO_CROSS_JOIN
#undef ndbcSQL_SRJO_EXCEPT_JOIN
#undef ndbcSQL_SRJO_FULL_OUTER_JOIN
#undef ndbcSQL_SRJO_INNER_JOIN
#undef ndbcSQL_SRJO_INTERSECT_JOIN
#undef ndbcSQL_SRJO_LEFT_OUTER_JOIN
#undef ndbcSQL_SRJO_NATURAL_JOIN
#undef ndbcSQL_SRJO_RIGHT_OUTER_JOIN
#undef ndbcSQL_SRJO_UNION_JOIN
#undef ndbcSQL_SRVC_DEFAULT
#undef ndbcSQL_SRVC_NULL
#undef ndbcSQL_SRVC_ROW_SUBQUERY
#undef ndbcSQL_SRVC_VALUE_EXPRESSION
#undef ndbcSQL_SSF_CONVERT
#undef ndbcSQL_SSF_LOWER
#undef ndbcSQL_SSF_SUBSTRING
#undef ndbcSQL_SSF_TRANSLATE
#undef ndbcSQL_SSF_TRIM_BOTH
#undef ndbcSQL_SSF_TRIM_LEADING
#undef ndbcSQL_SSF_TRIM_TRAILING
#undef ndbcSQL_SSF_UPPER
#undef ndbcSQL_STANDARD_CLI_CONFORMANCE
#undef ndbcSQL_STATIC_CURSOR_ATTRIBUTES1
#undef ndbcSQL_STATIC_CURSOR_ATTRIBUTES2
#undef ndbcSQL_STILL_EXECUTING
#undef ndbcSQL_STRING_FUNCTIONS
#undef ndbcSQL_SU_DML_STATEMENTS
#undef ndbcSQL_SU_INDEX_DEFINITION
#undef ndbcSQL_SU_PRIVILEGE_DEFINITION
#undef ndbcSQL_SU_PROCEDURE_INVOCATION
#undef ndbcSQL_SU_TABLE_DEFINITION
#undef ndbcSQL_SUBQUERIES
#undef ndbcSQL_SUCCESS
#undef ndbcSQL_SVE_CASE
#undef ndbcSQL_SVE_CAST
#undef ndbcSQL_SVE_COALESCE
#undef ndbcSQL_SVE_NULLIF
#undef ndbcSQL_SYSTEM_FUNCTIONS
#undef ndbcSQL_TABLE_TERM
#undef ndbcSQL_TC_ALL
#undef ndbcSQL_TC_DDL_COMMIT
#undef ndbcSQL_TC_DDL_IGNORE
#undef ndbcSQL_TC_DML
#undef ndbcSQL_TC_NONE
#undef ndbcSQL_TIMEDATE_ADD_INTERVALS
#undef ndbcSQL_TIMEDATE_DIFF_INTERVALS
#undef ndbcSQL_TIMEDATE_FUNCTIONS
#undef ndbcSQL_TRUE
#undef ndbcSQL_TXN_CAPABLE
#undef ndbcSQL_TXN_ISOLATION_OPTION
#undef ndbcSQL_TXN_READ_COMMITTED
#undef ndbcSQL_TXN_READ_UNCOMMITTED
#undef ndbcSQL_TXN_REPEATABLE_READ
#undef ndbcSQL_TXN_SERIALIZABLE
#undef ndbcSQL_U_UNION
#undef ndbcSQL_U_UNION_ALL
#undef ndbcSQL_UB_OFF
#undef ndbcSQL_UB_VARIABLE
#undef ndbcSQL_UNION
#undef ndbcSQL_UNSPECIFIED
#undef ndbcSQL_USER_NAME
#undef ndbcSQL_XOPEN_CLI_YEAR

/* Undefine string representations of ndbc specific constants.
 */
#undef ndbcINVALID_ARGUMENT
#undef ndbcINVALID_RETURN
#undef ndbcINTERNAL_ERROR