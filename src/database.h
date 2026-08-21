#pragma once

#include <QString>

// Database access layer for BandPilot.
//
// Responsible for opening (and creating, if needed) the SQLite database in
// the standard per-user application data location and ensuring the
// "contacts" and "dxcc_entity" tables exist.
//
// The opened connection is registered under connectionName() so that other
// layers (e.g. QSqlTableModel in the UI) can attach to it without needing to
// pass a QSqlDatabase instance around.
namespace Database {

// Opens the database and ensures the schema (and DXCC entity seed data) is
// in place.
// Returns true on success. On failure, *errorMessage (if non-null) is set
// to a human-readable description suitable for a status bar message.
bool initialize(QString *errorMessage = nullptr);

// Name under which the connection is registered with QSqlDatabase.
QString connectionName();

// Full path to the SQLite database file on disk.
QString databaseFilePath();

// Parses an ADIF (.adi/.adif) log file and inserts each QSO record found in
// it into the "contacts" table. Returns the number of contacts imported, or
// -1 on failure (with *errorMessage, if non-null, set to a human-readable
// description). Records missing a callsign, date, or time are skipped.
int importAdif(const QString &filePath, QString *errorMessage = nullptr);

// Exposed for testing: resolves a callsign to a dxcc_entity.entity_code via
// cty.dat and the "dxcc_entity" table already seeded on connectionName()'s
// database connection -- the same lookup importAdif() uses internally to
// fill in a QSO's DXCC entity. Returns -1 if cty.dat isn't loaded, the
// callsign matches no prefix, or the matched country name doesn't
// correlate to a known entity.
int dxccEntityCodeForCallsign(const QString &callsign);

} // namespace Database
