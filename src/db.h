#ifndef DB_H
#define DB_H

#include "../third_party/sqlite/sqlite3.h"

// Opens (or creates) the corpus database and makes sure the tables exist.
// Every database error in this project is fatal: it prints the message and exits.
sqlite3 *dbOpen(const char *path);
void dbExec(sqlite3 *db, const char *sql);
sqlite3_stmt *dbPrepare(sqlite3 *db, const char *sql);
// sqlite3_step that exits on error; returns SQLITE_ROW or SQLITE_DONE.
int dbStep(sqlite3 *db, sqlite3_stmt *stmt);

#endif
