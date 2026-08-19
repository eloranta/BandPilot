#pragma once

#include <QString>

// Database access layer for BandPilot.
//
// Responsible for opening (and creating, if needed) the SQLite database in
// the standard per-user application data location, ensuring the "contacts"
// table exists, and seeding it with sample QSOs the first time the app runs
// against an empty table.
//
// The opened connection is registered under connectionName() so that other
// layers (e.g. QSqlTableModel in the UI) can attach to it without needing to
// pass a QSqlDatabase instance around.
namespace Database {

// Opens the database and ensures schema + seed data are in place.
// Returns true on success. On failure, *errorMessage (if non-null) is set
// to a human-readable description suitable for a status bar message.
bool initialize(QString *errorMessage = nullptr);

// Name under which the connection is registered with QSqlDatabase.
QString connectionName();

// Full path to the SQLite database file on disk.
QString databaseFilePath();

} // namespace Database
