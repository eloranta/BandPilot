#include "database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QVector>

namespace {

const char *const kConnectionName = "bandpilot_connection";
const char *const kDxccEntitiesFileName = "dxcc-entities.txt";

// Sentinel entity_code standing in for "no known DXCC entity" (no real DXCC
// entity is numbered this high; the current list tops out at 522). Contacts
// use this instead of a NULL dxcc_entity because QSqlRelationalTableModel
// joins the "dxcc_entity" table with an INNER JOIN, silently hiding rows
// whose foreign key doesn't match any row in the related table.
const int kUnknownDxccCode = 999;

// One seed row parsed from dxcc-entities.txt ("<code>|<name>" per line).
struct DxccEntitySeed
{
    int code;
    QString name;
};

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

// One <eor>-delimited QSO record from an ADIF file: lower-cased field name
// (e.g. "qso_date") to its raw value.
using AdifRecord = QMap<QString, QString>;

// ADIF data fields are declared as "<name:length[:type]>" followed by
// exactly `length` bytes of value, so records are parsed by that explicit
// length rather than by scanning for the next '<' (a value may legally
// contain '<' or ':'). Anything before the header terminator (<eoh>), i.e.
// the header fields themselves, is skipped since it describes the exporting
// program rather than a QSO.
QVector<AdifRecord> parseAdifRecords(const QString &content)
{
    QVector<AdifRecord> records;

    const int headerEnd = content.indexOf(QStringLiteral("<eoh>"), 0, Qt::CaseInsensitive);
    int i = headerEnd >= 0 ? headerEnd + 5 : 0;

    AdifRecord current;
    while (i < content.size()) {
        const int lt = content.indexOf(QLatin1Char('<'), i);
        if (lt < 0)
            break;
        const int gt = content.indexOf(QLatin1Char('>'), lt);
        if (gt < 0)
            break;

        const QStringList parts = content.mid(lt + 1, gt - lt - 1).split(QLatin1Char(':'));
        const QString name = parts.value(0).trimmed().toLower();
        i = gt + 1;

        if (name == QLatin1String("eor")) {
            records.append(current);
            current = AdifRecord();
            continue;
        }

        bool ok = false;
        const int len = parts.value(1).toInt(&ok);
        if (!ok || len < 0)
            continue; // not a data field (e.g. stray/unrecognized tag)

        current.insert(name, content.mid(i, len));
        i += len;
    }

    return records;
}

QString adifDateToIso(const QString &value)
{
    if (value.size() != 8)
        return QString();
    return value.left(4) + QLatin1Char('-') + value.mid(4, 2) + QLatin1Char('-') + value.mid(6, 2);
}

QString adifTimeToHm(const QString &value)
{
    if (value.size() < 4)
        return QString();
    return value.left(2) + QLatin1Char(':') + value.mid(2, 2);
}

// Path to the DXCC entity seed file, kept alongside the database file.
QString dxccEntitiesFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + QString::fromLatin1(kDxccEntitiesFileName);
}

// Copies the bundled dxcc-entities.txt (shipped next to the executable) to
// the app data directory the first time the app runs, so it lives alongside
// the database file it seeds.
bool ensureDxccEntitiesFile(QString *errorMessage)
{
    const QString destPath = dxccEntitiesFilePath();
    if (QFile::exists(destPath))
        return true;

    const QString bundledPath = QCoreApplication::applicationDirPath() + QStringLiteral("/")
        + QString::fromLatin1(kDxccEntitiesFileName);
    if (!QFile::exists(bundledPath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Missing bundled %1").arg(bundledPath);
        return false;
    }

    if (!QFile::copy(bundledPath, destPath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to copy %1 to %2").arg(bundledPath, destPath);
        return false;
    }

    return true;
}

// Parses "<code>|<name>" lines from dxcc-entities.txt.
bool loadDxccEntities(const QString &path, QVector<DxccEntitySeed> *entities, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty())
            continue;

        const int sep = line.indexOf(QLatin1Char('|'));
        bool ok = false;
        const int code = sep >= 0 ? line.left(sep).toInt(&ok) : 0;
        if (!ok)
            continue;

        entities->append({code, line.mid(sep + 1)});
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

    QVector<DxccEntitySeed> entities;
    if (!loadDxccEntities(dxccEntitiesFilePath(), &entities, errorMessage))
        return false;

    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO dxcc_entity (entity_code, entity) VALUES (:code, :entity)");

    for (const DxccEntitySeed &row : entities) {
        query.bindValue(":code", row.code);
        query.bindValue(":entity", row.name);

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

// Makes sure the "(Unknown)" sentinel entity exists, regardless of whether
// dxcc_entity was just seeded or already populated from an earlier run.
bool ensureUnknownDxccEntity(QString *errorMessage)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare("INSERT OR IGNORE INTO dxcc_entity (entity_code, entity) VALUES (:code, :entity)");
    query.bindValue(":code", kUnknownDxccCode);
    query.bindValue(":entity", QStringLiteral("(Unknown)"));

    const bool ok = query.exec();
    if (!ok && errorMessage)
        *errorMessage = query.lastError().text();

    return ok;
}

// Contacts imported before the sentinel above existed (or with no DXCC
// info at all) may still have a NULL dxcc_entity; point those at the
// sentinel too so they aren't dropped by the relational table model's join.
bool migrateNullDxccEntities(QString *errorMessage)
{
    QSqlQuery query(QSqlDatabase::database(kConnectionName));
    query.prepare("UPDATE contacts SET dxcc_entity = :code WHERE dxcc_entity IS NULL");
    query.bindValue(":code", kUnknownDxccCode);

    const bool ok = query.exec();
    if (!ok && errorMessage)
        *errorMessage = query.lastError().text();

    return ok;
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

    if (!ensureDxccEntitiesFile(errorMessage))
        return false;

    if (!createDxccEntityTable(errorMessage))
        return false;

    if (!seedDxccEntityIfEmpty(errorMessage))
        return false;

    if (!ensureUnknownDxccEntity(errorMessage))
        return false;

    if (!migrateNullDxccEntities(errorMessage))
        return false;

    return true;
}

int importAdif(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return -1;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    const QVector<AdifRecord> records = parseAdifRecords(content);

    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return -1;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, dxcc_entity, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :dxcc_entity, :grid, :rst_tx, :rst_rx, :qsl, :comment)");

    int imported = 0;
    for (const AdifRecord &record : records) {
        const QString call = record.value(QStringLiteral("call"));
        const QString date = adifDateToIso(record.value(QStringLiteral("qso_date")));
        const QString time = adifTimeToHm(record.value(QStringLiteral("time_on")));
        if (call.isEmpty() || date.isEmpty() || time.isEmpty())
            continue; // not enough to log a QSO; skip malformed/incomplete record

        query.bindValue(":date", date);
        query.bindValue(":time", time);
        query.bindValue(":call", call);
        query.bindValue(":band", record.value(QStringLiteral("band")));
        query.bindValue(":frequency", record.value(QStringLiteral("freq")));
        query.bindValue(":mode", record.value(QStringLiteral("mode")));
        query.bindValue(":submode", record.value(QStringLiteral("submode")));
        query.bindValue(":grid", record.value(QStringLiteral("gridsquare")));
        query.bindValue(":rst_tx", record.value(QStringLiteral("rst_sent")));
        query.bindValue(":rst_rx", record.value(QStringLiteral("rst_rcvd")));
        query.bindValue(":qsl", record.value(QStringLiteral("qsl_rcvd")));
        query.bindValue(":comment", record.contains(QStringLiteral("comment"))
                                         ? record.value(QStringLiteral("comment"))
                                         : record.value(QStringLiteral("notes")));

        bool dxccOk = false;
        const int dxccCode = record.value(QStringLiteral("dxcc")).toInt(&dxccOk);
        query.bindValue(":dxcc_entity", dxccOk && dxccCode >= 1 && dxccCode <= 999 ? dxccCode
                                                                                    : kUnknownDxccCode);

        if (!query.exec()) {
            if (errorMessage)
                *errorMessage = query.lastError().text();
            db.rollback();
            return -1;
        }
        ++imported;
    }

    if (!db.commit()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return -1;
    }

    return imported;
}

} // namespace Database
