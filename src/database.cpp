#include "database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QVector>

namespace {

const char *const kConnectionName = "bandpilot_connection";
const char *const kDxccEntitiesFileName = "dxcc-entities.txt";

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

    return true;
}

} // namespace Database
