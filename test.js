/*
Copyright (c) 2012 ICRL

See the file license.txt for copying permission.

Resource: test.js

This test script shows how to use ndbc to access a database and retrieve data.

Change History
Date        Author                Description
-------------------------------------------------------------------------------------------------
2012-07-28  Gregory Dow           Initial release.  Shows basic functionality.
*/

// This example script assumes that the ndbc module is in the same folder.
var ndbc = require('./ndbc');
console.log('ndbc module loaded');

// To begin using ODBC, you need to obtain an environment handle.
var env = ndbc.SQLAllocHandle('SQL_HANDLE_ENV', 0);
console.log('Output of SQLAllocHandle: ' + env.toString());

// Specifying an ODBC version is required before using other ODBC features.
var retcode = ndbc.SQLSetEnvAttr(env, 'SQL_ATTR_ODBC_VERSION', 'SQL_OV_ODBC3');
console.log('Output of SQLSetEnvAttr: ' + retcode.toString());

// In order to connect to a data source, we need a data source handle.
var dbc = ndbc.SQLAllocHandle('SQL_HANDLE_DBC', env);
console.log('Output of SQLAllocHandle: ' + dbc.toString());

// The test data source used for this script is named 'local_mysql'.
// The username and password are both 'ndbc'.
// Specify your own DSN, username, and password if you wish to run this test
// in your local environment.
retcode = ndbc.SQLConnect(dbc, 'local_mysql', 'ndbc', 'ndbc');
console.log('Output of SQLConnect: ' + retcode.toString());

// The ndbc extensions are currently designed to work in synchronous mode.
retcode = ndbc.SQLSetConnectAttr(dbc, 'SQL_ATTR_ASYNC_ENABLE', 'SQL_ASYNC_ENABLE_OFF');
console.log('Output of SQLSetConnectAttr: ' + retcode.toString());

retcode = ndbc.SQLGetConnectAttr(dbc, 'SQL_ATTR_ASYNC_ENABLE');
console.log('Output of SQLGetConnectAttr: ' + retcode.toString());

// After connecting and specifying the desired settings, allocate a statement handle
// to begin performing SQL tasks.
var stmt = ndbc.SQLAllocHandle('SQL_HANDLE_STMT', dbc);
console.log('Output of SQLAllocHandle: ' + stmt.toString());

// This example SELECT statement returns a result set.
// Any valid SQL statement can be submitted using this syntax.
retcode = ndbc.SQLExecDirect(stmt, 'SELECT * FROM NDBC_TEST.TEST_TABLE;');
console.log('Output of SQLExecDirect: ' + retcode.toString());

// Obtain a result set description from the completed statement.
var desc = ndbc.JsonDescribe(stmt);
console.log('Output of JsonDescribe: ' + desc.toString());

// Start assembling the output by obtaining header data.
var jsonOutput = ndbc.JsonHeader(stmt);

// JsonData accepts a third argument specifying the number of records to retrieve.
// If omitted, it returns one record at a time.
var jsonData = ndbc.JsonData(stmt, desc);

// Each record returned starts with ',' if the retrieval was successful, adding
// another element to the array being defined in the JSON string.
// This loop keeps retrieving data until the end of the result set or an error occurs.
while (jsonData.substring(0, 1) == ',') {
  jsonOutput = jsonOutput + jsonData;
  jsonData = ndbc.JsonData(stmt, desc);
}

// Append the remaining string data required to make the string a valid JSON object.
jsonOutput = jsonOutput + ndbc.JsonTrailer(stmt);
console.log('JSON-formatted query results: ' + jsonOutput.toString());

// Test reviving the JSON data.
var jsonObject = JSON.parse(jsonOutput);
console.log('Parsed object:');
console.log(jsonObject);

// When done with a statement handle, free it as follows.
retcode = ndbc.SQLFreeHandle('SQL_HANDLE_STMT', stmt);
console.log('Output of SQLFreeHandle: ' + retcode.toString());

// Disconnect from the database thusly.
retcode = ndbc.SQLDisconnect(dbc);
console.log('Output of SQLDisconnect: ' + retcode.toString());

// When done with a database connection, it can be freed.
retcode = ndbc.SQLFreeHandle('SQL_HANDLE_DBC', dbc);
console.log('Output of SQLFreeHandle: ' + retcode.toString());

// Finally, free the environment handle to stop using ODBC.
retcode = ndbc.SQLFreeHandle('SQL_HANDLE_ENV', env);
console.log('Output of SQLFreeHandle: ' + retcode.toString());
