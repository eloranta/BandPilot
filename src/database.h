#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

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

// Outcome of an ADIF-family import: how many QSO records were newly
// inserted, how many were already logged and skipped (only meaningful for
// importLotwAdif(), which deduplicates; always 0 for importAdif()), and how
// many lacked the minimum fields (callsign, date, time) needed to log a QSO.
struct AdifImportResult
{
    int imported = 0;
    int duplicates = 0;
    int invalid = 0;
};

// Parses an ADIF (.adi/.adif) log file and inserts each QSO record found in
// it into the "contacts" table. Returns the number of contacts imported, or
// -1 on failure (with *errorMessage, if non-null, set to a human-readable
// description). Records missing a callsign, date, or time are skipped.
int importAdif(const QString &filePath, QString *errorMessage = nullptr);

// Builds the HTTPS request URL for a LoTW QSL report
// (lotw.arrl.org/lotwuser/lotwreport.adi), scoped to QSOs confirmed on or
// after qslSince (yyyy-MM-dd). The login and password travel as query
// parameters, per LoTW's own report API -- callers should fetch this over
// HTTPS only and should not log or persist the resulting URL.
QUrl lotwReportUrl(const QString &login, const QString &password, const QString &qslSince);

// Imports a LoTW QSL report (raw ADIF bytes, as downloaded from
// lotwReportUrl()) into "contacts". Unlike importAdif(), a record already
// logged (same date, time, call, band, and mode) is skipped rather than
// inserted again, since a LoTW report re-lists QSOs the user may already
// have logged some other way (e.g. live via WSJT-X). Returns false only on
// a database error (with *errorMessage set); a report with no usable
// records is not itself an error and just shows up as
// result->imported == 0.
bool importLotwAdif(const QByteArray &data, AdifImportResult *result, QString *errorMessage = nullptr);

// Permanently deletes every row from "contacts". Returns the number of
// contacts deleted, or -1 on failure (with *errorMessage, if non-null, set
// to a human-readable description). Callers are responsible for confirming
// this with the user first -- it does not ask.
int clearAllContacts(QString *errorMessage = nullptr);

// A quick logbook summary for a persistent status bar display: total QSOs
// logged, distinct DXCC entities worked (logged at all), and distinct DXCC
// entities confirmed (qsl = 'Y', case-insensitive). The "(Unknown)"
// sentinel entity (see kUnknownDxccCode in database.cpp) never counts as a
// worked/confirmed entity.
struct ContactStats
{
    int totalContacts = 0;
    int entitiesWorked = 0;
    int entitiesConfirmed = 0;
};

// Computes contactStats() over the "contacts" table on connectionName()'s
// database connection. Returns false only on a database error (with
// *errorMessage set).
bool contactStats(ContactStats *stats, QString *errorMessage = nullptr);

// Mode-category and band labels for dxccEntityProgress(), in display
// order. "Satellite" is classified by ADIF PROP_MODE = "SAT" (not by the
// QSO's actual modulation), and its QSOs are excluded from CW/Phone/
// Digital -- a satellite contact is its own award category, not also a
// CW/Phone/Digital one. "Mixed" places no mode restriction at all, so it's
// true whenever any other mode category is.
constexpr int kDxccChallengeRowCount = 5; // Mixed, CW, Phone, Digital, Satellite
constexpr int kDxccChallengeBandCount = 12; // 160m through 70cm

const QStringList &dxccChallengeRowNames();
const QStringList &dxccChallengeBandNames();

// One row of the DXCC "entities worked" checklist (styled after LoTW's own
// per-entity confirmed-mode/confirmed-band report): whether this entity
// has at least one LoTW-confirmed QSO in each mode category
// (modeConfirmed, see dxccChallengeRowNames()) and on each band
// (bandConfirmed, see dxccChallengeBandNames()), plus a representative
// cty.dat primary prefix (empty if cty.dat isn't loaded, or no cty.dat
// record correlates to this entity).
struct DxccEntityProgress
{
    int entityCode = 0;
    QString entityName;
    QString prefix;
    bool modeConfirmed[kDxccChallengeRowCount] = {};
    bool bandConfirmed[kDxccChallengeBandCount] = {};
};

// One DxccEntityProgress per row of "dxcc_entity" (every current ARRL DXCC
// entity this app knows about -- the "(Unknown)" sentinel excluded), in
// entity_code order. Returns false only on a database error (with
// *errorMessage set).
bool dxccEntityProgress(QVector<DxccEntityProgress> *rows, QString *errorMessage = nullptr);

// Exposed for testing: resolves a callsign to a dxcc_entity.entity_code via
// cty.dat and the "dxcc_entity" table already seeded on connectionName()'s
// database connection -- the same lookup importAdif() uses internally to
// fill in a QSO's DXCC entity. Returns -1 if cty.dat isn't loaded, the
// callsign matches no prefix, or the matched country name doesn't
// correlate to a known entity.
int dxccEntityCodeForCallsign(const QString &callsign);

} // namespace Database
