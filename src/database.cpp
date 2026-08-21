#include "database.h"

#include "cty.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QUrlQuery>
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

// Path to the cty.dat callsign-prefix file, kept alongside the database
// file. Unlike dxcc-entities.txt, this isn't bundled with the app (it's a
// frequently-updated external file the user downloads themselves), so its
// absence is not an error: ADIF import just skips the callsign-based DXCC
// lookup and falls back to "(Unknown)".
QString ctyFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/cty.dat");
}

// Normalizes an entity name for fuzzy matching between cty.dat's naming
// (Jim Reisert AD1C's "Big CTY") and this app's ARRL-derived dxcc_entity
// names: drops parentheticals, punctuation, and a handful of common filler
// or alternate-spelling words, then sorts the remaining words so word-order
// differences don't matter.
QString normalizeEntityName(QString name)
{
    static const QSet<QString> kStopWords = {
        QStringLiteral("the"),      QStringLiteral("of"),        QStringLiteral("republic"),
        QStringLiteral("democratic"), QStringLiteral("peoples"), QStringLiteral("islamic"),
        QStringLiteral("federal"),  QStringLiteral("fed"),       QStringLiteral("rep"),
        QStringLiteral("repub"),    QStringLiteral("kingdom"),   QStringLiteral("state"),
        QStringLiteral("plurinational"), QStringLiteral("bolivarian"), QStringLiteral("islands"),
        QStringLiteral("island"),   QStringLiteral("is"),        QStringLiteral("isl"),
        QStringLiteral("isls"),     QStringLiteral("i"),         QStringLiteral("rock"),
        QStringLiteral("rocks"),    QStringLiteral("reef"),      QStringLiteral("reefs"),
        QStringLiteral("atoll"),    QStringLiteral("group"),     QStringLiteral("archipelago"),
        QStringLiteral("center"),   QStringLiteral("centre"),    QStringLiteral("ctr"),
        QStringLiteral("intl"),     QStringLiteral("international"), QStringLiteral("city"),
    };

    name = name.toLower();
    name.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));
    name.replace(QLatin1Char('&'), QStringLiteral(" and "));
    name.remove(QRegularExpression(QStringLiteral("[.,()'/-]")));

    QStringList words;
    for (const QString &word :
         name.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (!kStopWords.contains(word))
            words.append(word);
    }
    words.sort();
    return words.join(QLatin1Char(' '));
}

// Normalized dxcc_entity name -> entity_code, built once from the database
// and cached for the process lifetime (dxcc_entity is seeded once at
// startup and never changes afterwards). Names whose normalized form is
// ambiguous (matches more than one distinct entity, e.g. "Cocos I." vs
// "Cocos (Keeling) Is.") are dropped rather than guessed at.
const QMap<QString, int> &dxccNameIndex()
{
    static const QMap<QString, int> index = [] {
        QMap<QString, int> result;
        QSet<QString> ambiguous;

        QSqlQuery query(QStringLiteral("SELECT entity_code, entity FROM dxcc_entity"),
                         QSqlDatabase::database(kConnectionName));
        while (query.next()) {
            const int code = query.value(0).toInt();
            const QString key = normalizeEntityName(query.value(1).toString());
            if (result.contains(key) && result.value(key) != code)
                ambiguous.insert(key);
            else
                result.insert(key, code);
        }
        for (const QString &key : ambiguous)
            result.remove(key);

        return result;
    }();

    return index;
}

// A handful of cty.dat entity names that name-normalization alone can't
// bridge to their ARRL dxcc_entity counterpart: genuine alternate spellings
// (Rodriguez/Rodrigues), abbreviation styles the normalizer doesn't expand
// (St./Saint, N.Z./New Zealand, Pr./Prince), or cases where cty.dat splits
// one DXCC entity into several zone-bookkeeping entries (Asiatic/European
// Turkey, Sicily/African Italy) that ARRL counts as a single entity. Keyed
// by the cty.dat name lower-cased; values are spelled as in
// dxcc-entities.txt and still go through normalizeEntityName()/the index.
const QHash<QString, QString> &dxccNameOverrides()
{
    static const QHash<QString, QString> overrides = {
        {QStringLiteral("united states"), QStringLiteral("United States of America")},
        {QStringLiteral("cape verde"), QStringLiteral("Cabo Verde (Repub of)")},
        {QStringLiteral("vatican city"), QStringLiteral("Vatican")},
        {QStringLiteral("central african republic"), QStringLiteral("Central Africa")},
        {QStringLiteral("sicily"), QStringLiteral("Italy")},
        {QStringLiteral("african italy"), QStringLiteral("Italy")},
        {QStringLiteral("asiatic turkey"), QStringLiteral("Turkey")},
        {QStringLiteral("european turkey"), QStringLiteral("Turkey")},
        {QStringLiteral("north cook islands"), QStringLiteral("N. Cook Is.")},
        {QStringLiteral("south cook islands"), QStringLiteral("S. Cook Is.")},
        {QStringLiteral("western kiribati"), QStringLiteral("W. Kiribati (Gilbert Is.)")},
        {QStringLiteral("central kiribati"), QStringLiteral("C. Kiribati (British Phoenix Is)")},
        {QStringLiteral("eastern kiribati"), QStringLiteral("E. Kiribati (Line Is.)")},
        {QStringLiteral("st. barthelemy"), QStringLiteral("Saint Barthelemy")},
        {QStringLiteral("st. martin"), QStringLiteral("Saint Martin")},
        {QStringLiteral("us virgin islands"), QStringLiteral("Virgin Is.")},
        {QStringLiteral("rodriguez island"), QStringLiteral("Rodrigues I.")},
        {QStringLiteral("uk base areas on cyprus"), QStringLiteral("UK Sov. Base Areas on Cyprus")},
        {QStringLiteral("n.z. subantarctic is."), QStringLiteral("New Zealand Subantarctic Islands")},
        {QStringLiteral("pr. edward & marion is."), QStringLiteral("Prince Edward & Marion Is.")},
        {QStringLiteral("bear island"), QStringLiteral("Svalbard")},
        {QStringLiteral("vietnam"), QStringLiteral("Viet Nam")},
    };
    return overrides;
}

// cty.dat entity names that can't be bridged via dxccNameOverrides() above
// because their correct ARRL counterpart's normalized form collides with a
// *different* entity's normalized form -- normalizeEntityName() strips
// "Island(s)"/"I."/"Is." and drops parentheticals, so "Cocos I." (Costa
// Rica, entity 37) and "Cocos (Keeling) Is." (Australia, entity 38) both
// normalize to "cocos" and dxccNameIndex() correctly refuses to guess
// between them, dropping the key entirely. These map straight to the ARRL
// entity_code (see resources/dxcc-entities.txt), bypassing the name index.
// Keyed by the cty.dat name lower-cased.
const QHash<QString, int> &dxccCodeOverrides()
{
    static const QHash<QString, int> overrides = {
        {QStringLiteral("cocos island"), 37},
        {QStringLiteral("cocos (keeling) islands"), 38},
    };
    return overrides;
}

// Best-effort DXCC entity code for a callsign via cty.dat (if loaded),
// correlated to this app's dxcc_entity table by (direct code override,
// then name override, then fuzzy name match). Returns -1 if cty.dat isn't
// loaded, the callsign matches no prefix, or the matched country name
// doesn't correlate to a known entity.
int dxccCodeForCallsign(const QString &callsign)
{
    const QString country = Cty::countryForCallsign(callsign);
    if (country.isEmpty())
        return -1;

    const QString key = country.trimmed().toLower();

    const auto codeOverrideIt = dxccCodeOverrides().constFind(key);
    if (codeOverrideIt != dxccCodeOverrides().constEnd())
        return codeOverrideIt.value();

    const auto overrideIt = dxccNameOverrides().constFind(key);
    const QString lookupName =
        overrideIt != dxccNameOverrides().constEnd() ? overrideIt.value() : country;

    return dxccNameIndex().value(normalizeEntityName(lookupName), -1);
}

// Re-tries the callsign -> DXCC lookup for any contact still sitting at the
// "(Unknown)" sentinel (or, defensively, still NULL) now that cty.dat is
// loaded, so QSOs imported before cty.dat existed -- or before it was last
// updated -- get resolved without needing to be re-imported.
bool backfillUnresolvedDxccEntities(QString *errorMessage)
{
    if (!Cty::isLoaded())
        return true; // nothing to look up without a cty.dat

    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    QSqlQuery select(db);
    if (!select.exec(QStringLiteral(
            "SELECT id, call FROM contacts WHERE dxcc_entity IS NULL OR dxcc_entity = %1")
                          .arg(kUnknownDxccCode))) {
        if (errorMessage)
            *errorMessage = select.lastError().text();
        return false;
    }

    struct Resolution
    {
        int id;
        int code;
    };
    QVector<Resolution> resolutions;
    while (select.next()) {
        const int code = dxccCodeForCallsign(select.value(1).toString());
        if (code >= 1 && code <= 999)
            resolutions.append({select.value(0).toInt(), code});
    }

    if (resolutions.isEmpty())
        return true;

    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery update(db);
    update.prepare(QStringLiteral("UPDATE contacts SET dxcc_entity = :code WHERE id = :id"));
    for (const Resolution &resolution : resolutions) {
        update.bindValue(":code", resolution.code);
        update.bindValue(":id", resolution.id);
        if (!update.exec()) {
            if (errorMessage)
                *errorMessage = update.lastError().text();
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

// Inserts each parsed ADIF record as a new contact, looking up its DXCC
// entity via cty.dat. Shared by importAdif() (deduplicate = false: every
// record is inserted) and importLotwAdif() (deduplicate = true: a record
// matching an existing contact's date, time, call, band, and mode is
// skipped instead, since a LoTW QSL report re-lists QSOs that may already
// be logged some other way). Returns false only on a database error (with
// *errorMessage set); malformed/duplicate records are just tallied and
// skipped, not treated as failures.
bool importAdifRecords(const QVector<AdifRecord> &records, bool deduplicate, Database::AdifImportResult *result,
                        QString *errorMessage)
{
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return false;
    }

    QSqlQuery duplicateQuery(db);
    if (deduplicate) {
        duplicateQuery.prepare(
            "SELECT 1 FROM contacts WHERE date = :date AND time = :time "
            "AND UPPER(call) = UPPER(:call) AND band = :band AND mode = :mode LIMIT 1");
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO contacts "
        "(date, time, call, band, frequency, mode, submode, dxcc_entity, grid, rst_tx, rst_rx, qsl, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :dxcc_entity, :grid, :rst_tx, :rst_rx, :qsl, :comment)");

    for (const AdifRecord &record : records) {
        const QString call = record.value(QStringLiteral("call"));
        const QString date = adifDateToIso(record.value(QStringLiteral("qso_date")));
        const QString time = adifTimeToHm(record.value(QStringLiteral("time_on")));
        // The "band"/"mode"/"frequency" columns are NOT NULL, but a real
        // ADIF record (e.g. from LoTW, which often omits FREQ) may simply
        // lack the field. QMap::value() returns a null QString for a
        // missing key, and Qt's SQL layer binds a null QString as SQL NULL
        // rather than '' -- violating the NOT NULL constraint. The
        // two-argument value() form supplies a non-null empty-string
        // default instead, so a missing field just logs as blank.
        const QString band = record.value(QStringLiteral("band"), QStringLiteral(""));
        const QString mode = record.value(QStringLiteral("mode"), QStringLiteral(""));
        if (call.isEmpty() || date.isEmpty() || time.isEmpty()) {
            ++result->invalid;
            continue; // not enough to log a QSO; skip malformed/incomplete record
        }

        if (deduplicate) {
            duplicateQuery.bindValue(":date", date);
            duplicateQuery.bindValue(":time", time);
            duplicateQuery.bindValue(":call", call);
            duplicateQuery.bindValue(":band", band);
            duplicateQuery.bindValue(":mode", mode);
            if (!duplicateQuery.exec()) {
                if (errorMessage)
                    *errorMessage = duplicateQuery.lastError().text();
                db.rollback();
                return false;
            }
            if (duplicateQuery.next()) {
                ++result->duplicates;
                continue;
            }
        }

        query.bindValue(":date", date);
        query.bindValue(":time", time);
        query.bindValue(":call", call);
        query.bindValue(":band", band);
        query.bindValue(":frequency", record.value(QStringLiteral("freq"), QStringLiteral("")));
        query.bindValue(":mode", mode);
        query.bindValue(":submode", record.value(QStringLiteral("submode")));
        query.bindValue(":grid", record.value(QStringLiteral("gridsquare")));
        query.bindValue(":rst_tx", record.value(QStringLiteral("rst_sent")));
        query.bindValue(":rst_rx", record.value(QStringLiteral("rst_rcvd")));
        query.bindValue(":qsl", record.value(QStringLiteral("qsl_rcvd")));
        query.bindValue(":comment", record.contains(QStringLiteral("comment"))
                                         ? record.value(QStringLiteral("comment"))
                                         : record.value(QStringLiteral("notes")));

        bool dxccOk = false;
        int dxccCode = record.value(QStringLiteral("dxcc")).toInt(&dxccOk);
        if (!dxccOk || dxccCode < 1 || dxccCode > 999)
            dxccCode = dxccCodeForCallsign(call); // -1 if no cty.dat match
        if (dxccCode < 1 || dxccCode > 999)
            dxccCode = kUnknownDxccCode;
        query.bindValue(":dxcc_entity", dxccCode);

        if (!query.exec()) {
            if (errorMessage)
                *errorMessage = query.lastError().text();
            db.rollback();
            return false;
        }
        ++result->imported;
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

    if (!ensureUnknownDxccEntity(errorMessage))
        return false;

    if (!migrateNullDxccEntities(errorMessage))
        return false;

    Cty::load(ctyFilePath()); // best-effort; ADIF import works fine without it

    if (!backfillUnresolvedDxccEntities(errorMessage))
        return false;

    return true;
}

int dxccEntityCodeForCallsign(const QString &callsign)
{
    return dxccCodeForCallsign(callsign);
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

    AdifImportResult result;
    if (!importAdifRecords(records, /*deduplicate=*/false, &result, errorMessage))
        return -1;

    return result.imported;
}

QUrl lotwReportUrl(const QString &login, const QString &password, const QString &qslSince)
{
    QUrl url(QStringLiteral("https://lotw.arrl.org/lotwuser/lotwreport.adi"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("login"), login);
    query.addQueryItem(QStringLiteral("password"), password);
    query.addQueryItem(QStringLiteral("qso_query"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("qso_qsl"), QStringLiteral("yes"));
    query.addQueryItem(QStringLiteral("qso_qslsince"), qslSince);
    url.setQuery(query);
    return url;
}

bool importLotwAdif(const QByteArray &data, AdifImportResult *result, QString *errorMessage)
{
    const QString content = QString::fromUtf8(data);
    const QVector<AdifRecord> records = parseAdifRecords(content);

    return importAdifRecords(records, /*deduplicate=*/true, result, errorMessage);
}

int clearAllContacts(QString *errorMessage)
{
    QSqlDatabase db = QSqlDatabase::database(kConnectionName);
    if (!db.transaction()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return -1;
    }

    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")) || !countQuery.next()) {
        if (errorMessage)
            *errorMessage = countQuery.lastError().text();
        db.rollback();
        return -1;
    }
    const int count = countQuery.value(0).toInt();

    QSqlQuery deleteQuery(db);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM contacts"))) {
        if (errorMessage)
            *errorMessage = deleteQuery.lastError().text();
        db.rollback();
        return -1;
    }

    if (!db.commit()) {
        if (errorMessage)
            *errorMessage = db.lastError().text();
        return -1;
    }
    return count;
}

} // namespace Database
