#include "db.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// doc         one row per sitting (birleşim) transcript
// form_count  how often each word form occurs in each sitting, after cleaning;
//             forms are folded (lowercase, no circumflex), see trFold()
// lexicon     word form -> dictionary word (lemma), from Wiktionary's inflection tables
//             (src = 'wiktionary') plus old spellings we generate (src = 'old-spelling')
// origin      where a lemma came from (arabic, persian, french, turkic, ...)
// form_origin for each noun/adjective form in the corpus: its dictionary word and origin
//             (rebuilt by buildFormOrigins after each ingest)

// Bump when the tables change: an older database is then refused instead of half-working.
#define SCHEMA_VERSION 4

static const char *SCHEMA =
    "CREATE TABLE IF NOT EXISTS doc ("
    "  id TEXT PRIMARY KEY,"
    "  chamber TEXT NOT NULL, term INTEGER NOT NULL, volume INTEGER NOT NULL, sitting INTEGER NOT NULL,"
    "  date TEXT NOT NULL, year INTEGER NOT NULL,"
    "  tokens INTEGER NOT NULL,"
    "  turkish_letters REAL,"   // share of letters only Turkish has, a check on the OCR
    "  lines_in INTEGER, front_matter INTEGER, back_matter INTEGER, headings INTEGER, furniture INTEGER,"
    "  labels INTEGER, lines_out INTEGER);"
    "CREATE TABLE IF NOT EXISTS form_count ("
    "  doc_id TEXT NOT NULL, form TEXT NOT NULL, n INTEGER NOT NULL,"
    "  PRIMARY KEY (doc_id, form)) WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS form_count_form ON form_count(form);"
    "CREATE TABLE IF NOT EXISTS lexicon ("
    "  form TEXT NOT NULL, lemma TEXT NOT NULL, pos TEXT NOT NULL, src TEXT NOT NULL,"
    "  PRIMARY KEY (form, lemma, pos)) WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS lexicon_lemma ON lexicon(lemma);"
    "CREATE TABLE IF NOT EXISTS origin ("
    "  lemma TEXT NOT NULL, pos TEXT NOT NULL, origin TEXT NOT NULL, etymology TEXT,"
    "  PRIMARY KEY (lemma, pos)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS form_origin ("
    "  form TEXT PRIMARY KEY, lemma TEXT NOT NULL, origin TEXT NOT NULL) WITHOUT ROWID;";

static void die(sqlite3 *db, const char *what) {
    fprintf(stderr, "database error (%s): %s\n", what, db ? sqlite3_errmsg(db) : "out of memory");
    exit(1);
}

sqlite3 *dbOpen(const char *path) {
    sqlite3 *db;
    if(sqlite3_open(path, &db) != SQLITE_OK) die(db, path);
    sqlite3_stmt *stmt = dbPrepare(db, "PRAGMA user_version");
    dbStep(db, stmt);
    int version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_stmt *tables = dbPrepare(db, "SELECT COUNT(*) FROM sqlite_master");
    dbStep(db, tables);
    bool empty = sqlite3_column_int(tables, 0) == 0;
    sqlite3_finalize(tables);
    if(!empty && version != SCHEMA_VERSION) {
        fprintf(stderr, "%s was made by an older version of evo. Delete it and build it again (make pilot).\n", path);
        exit(1);
    }
    dbExec(db, SCHEMA);
    char sql[64];
    snprintf(sql, sizeof sql, "PRAGMA user_version = %d", SCHEMA_VERSION);
    dbExec(db, sql);
    return db;
}

void dbExec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    if(sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "database error: %s\n  in: %.200s\n", err, sql);
        exit(1);
    }
}

sqlite3_stmt *dbPrepare(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt;
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) die(db, sql);
    return stmt;
}

int dbStep(sqlite3 *db, sqlite3_stmt *stmt) {
    int rc = sqlite3_step(stmt);
    if(rc != SQLITE_ROW && rc != SQLITE_DONE) die(db, sqlite3_sql(stmt));
    return rc;
}
