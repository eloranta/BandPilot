#include "database.h"

#include "dxcc_entities.h"

#include <QDate>
#include <QDir>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTime>
#include <QVector>

namespace {

const char *const kConnectionName = "bandpilot_connection";

// A handful of plausible sample QSOs used to seed an empty database so the
// UI has something to show on first run.
struct SeedContact
{
    QString call;
    QString band;
    QString frequency;
    QString mode;
    QString submode;
    int dxccEntity;
    QString grid;
};

// dxcc_entity values below are the standard ADIF/DXCC entity numbers for
// each country, e.g. https://www.country-files.com/entity-list/
QVector<SeedContact> seedTemplate()
{
    return {
        {"W1AW",   "20m", "14.074", "FT8", "",    291, "FN31pr"},
        {"G4XYZ",  "40m", "7.074",  "FT8", "",    223, "IO91wm"},
        {"JA1ABC", "15m", "21.074", "FT8", "",    339, "PM95tp"},
        {"VK2DEF", "10m", "28.450", "SSB", "USB", 150, "QF56od"},
        {"DL3GHI", "80m", "3.573",  "FT4", "",    230, "JO50cp"},
        {"OH2JKL", "17m", "18.100", "CW",  "",    224, "KP20eq"},
        {"PY2MNO", "12m", "24.910", "SSB", "USB", 108, "GG66ff"},
        {"ZS6PQR", "6m",  "50.313", "FT8", "",    462, "KG44dc"},
    };
}

bool createContactsTable(QString *errorMessage)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS contacts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  date TEXT NOT NULL,"
        "  time TEXT NOT NULL,"
        "  call TEXT NOT NULL,"
        "  band TEXT NOT NULL,"
        "  frequency TEXT NOT NULL,"
        "  mode TEXT NOT NULL,"
        "  submode TEXT,"
        "  dxcc_entity INTEGER CHECK (dxcc_entity IS NULL OR (dxcc_entity BETWEEN 1 AND 999)),"
        "  deleted_entity TEXT DEFAULT '',"
        "  grid TEXT,"
        "  rst_tx TEXT,"
        "  rst_rx TEXT,"
        "  qsl TEXT,"
        "  comment TEXT"
        ")");

    if (!ok && errorMessage)
        *errorMessage = query.lastError().text();

    return ok;
}

// Returns row count, or -1 on error.
int contactsRowCount()
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    if (!query.exec("SELECT COUNT(*) FROM contacts") || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool seedIfEmpty(QString *errorMessage)
{
    const int count = contactsRowCount();
    if (count != 0)
        return count >= 0; // already has data, or the COUNT query itself failed above

    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, dxcc_entity, grid, rst_tx, rst_rx, qsl) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :dxcc_entity, :grid, :rst_tx, :rst_rx, :qsl)");

    const QVector<SeedContact> rows = seedTemplate();
    QDate date = QDate::currentDate().addDays(-rows.size());

    for (const SeedContact &row : rows) {
        date = date.addDays(1);
        const QTime time(QRandomGenerator::global()->bounded(24),
                          QRandomGenerator::global()->bounded(60));

        query.bindValue(":date", date.toString(Qt::ISODate));
        query.bindValue(":time", time.toString("HH:mm"));
        query.bindValue(":call", row.call);
        query.bindValue(":band", row.band);
        query.bindValue(":frequency", row.frequency);
        query.bindValue(":mode", row.mode);
        query.bindValue(":submode", row.submode);
        query.bindValue(":dxcc_entity", row.dxccEntity);
        query.bindValue(":grid", row.grid);
        query.bindValue(":rst_tx", "59");
        query.bindValue(":rst_rx", "59");
        query.bindValue(":qsl", "N");

        if (!query.exec()) {
            if (errorMessage)
                *errorMessage = query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    return true;
}

bool createDxccEntityTable(QString *errorMessage)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    const bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS dxcc_entity ("
        "  entity_code INTEGER PRIMARY KEY,"
        "  entity TEXT NOT NULL"
        ")");

    if (!ok && errorMessage)
        *errorMessage = query.lastError().text();

    return ok;
}

// Returns row count, or -1 on error.
int dxccEntityRowCount()
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    if (!query.exec("SELECT COUNT(*) FROM dxcc_entity") || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool seedDxccEntityIfEmpty(QString *errorMessage)
{
    const int count = dxccEntityRowCount();
    if (count != 0)
        return count >= 0; // already has data, or the COUNT query itself failed above

    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO dxcc_entity (entity_code, entity) VALUES (:code, :entity)");

    for (const DxccEntitySeed &row : kDxccEntities) {
        query.bindValue(":code", row.code);
        query.bindValue(":entity", QString::fromUtf8(row.name));

        if (!query.exec()) {
            if (errorMessage)
                *errorMessage = query.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    return true;
}

} // namespace

namespace Database {

QString connectionName()
{
    return QString::fromLatin1(kConnectionName);
}

QString databaseFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/bandpilot.sqlite");
}

bool initialize(QString *errorMessage)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName());
    db.setDatabaseName(databaseFilePath());

    if (!db.open()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    if (!createContactsTable(errorMessage))
        return false;

    if (!seedIfEmpty(errorMessage))
        return false;

    if (!createDxccEntityTable(errorMessage))
        return false;

    if (!seedDxccEntityIfEmpty(errorMessage))
        return false;

    return true;
}

} // namespace Database
