#include <QFile>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>
#include <QTextStream>
#include <QVector>

#include <algorithm>

#include "cty.h"
#include "database.h"

// Exercises Database::dxccEntityCodeForCallsign() -- the callsign -> DXCC
// entity_code correlation ADIF import relies on -- against an isolated,
// in-memory "dxcc_entity" table seeded from the real, bundled
// resources/dxcc-entities.txt, and the real cty.dat fixture also used by
// tst_cty (tests/data/cty.dat). No real app data (SQLite file, cty.dat,
// dxcc-entities.txt under AppDataLocation) is touched.
class TstDatabase : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void shetlandIslandsIsUnknown();
    void viennaIntlCtrIsUnreachableAustriaWins();

    void vietnamResolves();
    void cocosIslandsResolveDistinctly();

    void dxccEntityProgressTracksModesAndBands();

    void importLotwAdifSkipsDuplicates();
    void importLotwAdifToleratesMissingFields();
    void clearAllContactsDeletesEverything();
};

void TstDatabase::initTestCase()
{
    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), Database::connectionName());
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(db.open(), qPrintable(db.lastError().text()));

    QSqlQuery createTable(db);
    QVERIFY2(createTable.exec(QStringLiteral("CREATE TABLE dxcc_entity ("
                                              "  entity_code INTEGER PRIMARY KEY,"
                                              "  entity TEXT NOT NULL"
                                              ")")),
             qPrintable(createTable.lastError().text()));

    QSqlQuery createContacts(db);
    QVERIFY2(createContacts.exec(QStringLiteral("CREATE TABLE contacts ("
                                                 "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                 "  date TEXT NOT NULL,"
                                                 "  time TEXT NOT NULL,"
                                                 "  call TEXT NOT NULL,"
                                                 "  band TEXT NOT NULL,"
                                                 "  frequency TEXT NOT NULL,"
                                                 "  mode TEXT NOT NULL,"
                                                 "  submode TEXT,"
                                                 "  dxcc_entity INTEGER,"
                                                 "  deleted_entity TEXT DEFAULT '',"
                                                 "  grid TEXT,"
                                                 "  rst_tx TEXT,"
                                                 "  rst_rx TEXT,"
                                                 "  qsl TEXT,"
                                                 "  comment TEXT,"
                                                 "  prop_mode TEXT"
                                                 ")")),
             qPrintable(createContacts.lastError().text()));

    const QString entitiesPath = QFINDTESTDATA("../resources/dxcc-entities.txt");
    QVERIFY(!entitiesPath.isEmpty());
    QFile entitiesFile(entitiesPath);
    QVERIFY(entitiesFile.open(QIODevice::ReadOnly | QIODevice::Text));

    QSqlQuery insert(db);
    QVERIFY(insert.prepare(
        QStringLiteral("INSERT INTO dxcc_entity (entity_code, entity) VALUES (:code, :entity)")));

    QTextStream in(&entitiesFile);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty())
            continue;

        const int sep = line.indexOf(QLatin1Char('|'));
        bool ok = false;
        const int code = sep >= 0 ? line.left(sep).toInt(&ok) : 0;
        if (!ok)
            continue;

        insert.bindValue(QStringLiteral(":code"), code);
        insert.bindValue(QStringLiteral(":entity"), line.mid(sep + 1));
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    }

    const QString ctyPath = QFINDTESTDATA("data/cty.dat");
    QVERIFY(!ctyPath.isEmpty());
    QString loadError;
    QVERIFY2(Cty::load(ctyPath, &loadError), qPrintable(loadError));
}

void TstDatabase::cleanupTestCase()
{
    QSqlDatabase::removeDatabase(Database::connectionName());
}

void TstDatabase::shetlandIslandsIsUnknown()
{
    // Shetland Islands' cty.dat primary prefix is asterisked ("*GM/s") --
    // AD1C's marker for "informational only, not a separate DXCC entity" --
    // and dxcc-entities.txt correctly has no corresponding row. "GB0SI" is
    // one of the record's real exact-callsign overrides, so the lookup
    // must report "no known entity" (-1) rather than guessing.
    QCOMPARE(Database::dxccEntityCodeForCallsign(QStringLiteral("GB0SI")), -1);
}

void TstDatabase::viennaIntlCtrIsUnreachableAustriaWins()
{
    // "Vienna Intl Ctr" is also asterisk-marked ("*4U1V") and every one of
    // its exact-callsign overrides (e.g. "=4U1VIC") is re-listed verbatim
    // under Austria's own, later record -- so Austria always wins and
    // "Vienna Intl Ctr" itself can never actually be returned by
    // Cty::countryForCallsign() for any real callsign. This locks in that
    // UN-Vienna calls correctly resolve to Austria (entity 206), matching
    // current ARRL DXCC rules, rather than silently landing on the
    // superseded pseudo-entity.
    QCOMPARE(Database::dxccEntityCodeForCallsign(QStringLiteral("4U1VIC")), 206);
}

void TstDatabase::vietnamResolves()
{
    // "Vietnam" (cty.dat) only matches "Viet Nam" (dxcc-entities.txt,
    // entity 293) via the dxccNameOverrides() bridge.
    QCOMPARE(Database::dxccEntityCodeForCallsign(QStringLiteral("3W1ABC")), 293);
}

void TstDatabase::cocosIslandsResolveDistinctly()
{
    // "Cocos Island" (Costa Rica, TI9, entity 37) and "Cocos (Keeling)
    // Islands" (Australia, VK9C, entity 38) are two real, distinct DXCC
    // entities whose normalized names collide ("cocos"), so they resolve
    // via dxccCodeOverrides() straight to their ARRL entity_code rather
    // than the (correctly ambiguity-dropped) fuzzy name index.
    QCOMPARE(Database::dxccEntityCodeForCallsign(QStringLiteral("TI9ABC")), 37);
    QCOMPARE(Database::dxccEntityCodeForCallsign(QStringLiteral("VK9CX")), 38);
}

namespace {

QString adifField(const QString &name, const QString &value)
{
    return QStringLiteral("<%1:%2>%3").arg(name).arg(value.size()).arg(value);
}

} // namespace

void TstDatabase::importLotwAdifSkipsDuplicates()
{
    QSqlDatabase db = QSqlDatabase::database(Database::connectionName());
    QSqlQuery seed(db);
    QVERIFY2(seed.exec(QStringLiteral(
                 "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, qsl) "
                 "VALUES ('2024-01-01', '12:34', 'W1AW', '20M', '14.074', 'FT8', '', '')")),
             qPrintable(seed.lastError().text()));

    // Two LoTW records: one already logged above (must be skipped as a
    // duplicate, matched by date+time+call+band+mode), one new.
    QString adif = QStringLiteral("<ADIF_VER:5>3.1.0<EOH>\n");
    adif += adifField(QStringLiteral("QSO_DATE"), QStringLiteral("20240101"));
    adif += adifField(QStringLiteral("TIME_ON"), QStringLiteral("1234"));
    adif += adifField(QStringLiteral("CALL"), QStringLiteral("W1AW"));
    adif += adifField(QStringLiteral("BAND"), QStringLiteral("20M"));
    adif += adifField(QStringLiteral("MODE"), QStringLiteral("FT8"));
    adif += QStringLiteral("<EOR>\n");
    adif += adifField(QStringLiteral("QSO_DATE"), QStringLiteral("20240102"));
    adif += adifField(QStringLiteral("TIME_ON"), QStringLiteral("0100"));
    adif += adifField(QStringLiteral("CALL"), QStringLiteral("K5ABC"));
    adif += adifField(QStringLiteral("BAND"), QStringLiteral("40M"));
    adif += adifField(QStringLiteral("FREQ"), QStringLiteral("7.040"));
    adif += adifField(QStringLiteral("MODE"), QStringLiteral("CW"));
    adif += adifField(QStringLiteral("QSL_RCVD"), QStringLiteral("Y"));
    adif += QStringLiteral("<EOR>\n");

    Database::AdifImportResult result;
    QString errorMessage;
    QVERIFY2(Database::importLotwAdif(adif.toUtf8(), &result, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(result.imported, 1);
    QCOMPARE(result.duplicates, 1);
    QCOMPARE(result.invalid, 0);

    QSqlQuery check(db);
    QVERIFY2(check.exec(QStringLiteral("SELECT qsl FROM contacts WHERE call = 'K5ABC'")),
             qPrintable(check.lastError().text()));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("Y"));

    // The pre-existing W1AW contact must still be exactly one row -- not
    // duplicated by the re-imported LoTW record.
    QSqlQuery count(db);
    count.prepare(QStringLiteral("SELECT COUNT(*) FROM contacts WHERE call = 'W1AW'"));
    QVERIFY2(count.exec(), qPrintable(count.lastError().text()));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TstDatabase::importLotwAdifToleratesMissingFields()
{
    // Regression test: a real LoTW report often omits FREQ (and some
    // records may lack BAND/MODE too). "band"/"frequency"/"mode" are NOT
    // NULL columns, and record.value() previously returned a null QString
    // for a missing ADIF field, which Qt's SQL layer binds as SQL NULL --
    // violating the constraint and failing the whole import with "NOT NULL
    // constraint failed: contacts.frequency".
    QSqlDatabase db = QSqlDatabase::database(Database::connectionName());

    QString adif = QStringLiteral("<ADIF_VER:5>3.1.0<EOH>\n");
    adif += adifField(QStringLiteral("QSO_DATE"), QStringLiteral("20240103"));
    adif += adifField(QStringLiteral("TIME_ON"), QStringLiteral("0200"));
    adif += adifField(QStringLiteral("CALL"), QStringLiteral("N0FREQ"));
    adif += adifField(QStringLiteral("BAND"), QStringLiteral("15M"));
    adif += adifField(QStringLiteral("MODE"), QStringLiteral("SSB"));
    // FREQ deliberately omitted.
    adif += QStringLiteral("<EOR>\n");
    adif += adifField(QStringLiteral("QSO_DATE"), QStringLiteral("20240104"));
    adif += adifField(QStringLiteral("TIME_ON"), QStringLiteral("0300"));
    adif += adifField(QStringLiteral("CALL"), QStringLiteral("N0BAND"));
    // BAND, FREQ, and MODE all deliberately omitted.
    adif += QStringLiteral("<EOR>\n");

    Database::AdifImportResult result;
    QString errorMessage;
    QVERIFY2(Database::importLotwAdif(adif.toUtf8(), &result, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(result.imported, 2);
    QCOMPARE(result.duplicates, 0);
    QCOMPARE(result.invalid, 0);

    QSqlQuery check(db);
    QVERIFY2(check.exec(QStringLiteral(
                 "SELECT call, frequency, band, mode FROM contacts "
                 "WHERE call IN ('N0FREQ', 'N0BAND') ORDER BY call")),
             qPrintable(check.lastError().text()));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("N0BAND"));
    QCOMPARE(check.value(1).toString(), QString()); // frequency: blank, not NULL
    QCOMPARE(check.value(2).toString(), QString()); // band: blank, not NULL
    QCOMPARE(check.value(3).toString(), QString()); // mode: blank, not NULL
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("N0FREQ"));
    QCOMPARE(check.value(1).toString(), QString()); // frequency: blank, not NULL
    QCOMPARE(check.value(2).toString(), QStringLiteral("15M"));
    QCOMPARE(check.value(3).toString(), QStringLiteral("SSB"));

    // Clean up so later row-count assertions (clearAllContactsDeletesEverything)
    // aren't affected by this test's own inserts.
    QSqlQuery cleanup(db);
    QVERIFY2(cleanup.exec(QStringLiteral("DELETE FROM contacts WHERE call IN ('N0FREQ', 'N0BAND')")),
             qPrintable(cleanup.lastError().text()));
}

void TstDatabase::dxccEntityProgressTracksModesAndBands()
{
    QSqlDatabase db = QSqlDatabase::database(Database::connectionName());
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, prop_mode, dxcc_entity, qsl) "
        "VALUES (:date, :time, :call, :band, '14.000', :mode, :prop_mode, :entity, :qsl)"));

    struct Row
    {
        const char *call;
        const char *band;
        const char *mode;
        const char *propMode;
        int entity;
        const char *qsl;
    };
    // Entity 1 (Canada): CW on 20M, CW again on 40M, and SSB on 20M (same
    // band as the CW contact -- exercises that a cell counts distinct
    // entities, not distinct QSOs, and that Mixed/Challenge dedupe across
    // modes on the same band). Entity 6 (Alaska): FM via satellite on 2M
    // (must land in Satellite only, not Phone, despite FM being a phone
    // mode) plus one unconfirmed QSO that must be excluded entirely.
    const Row rows[] = {
        {"VE1AAA", "20M", "CW", "", 1, "Y"},
        {"VE1AAA", "40M", "CW", "", 1, "Y"},
        {"VE1AAA", "20M", "SSB", "", 1, "Y"},
        {"KL7BBB", "2M", "FM", "SAT", 6, "Y"},
        {"KL7BBB", "10M", "CW", "", 6, "N"}, // not confirmed -- must be excluded
    };
    for (const Row &row : rows) {
        insert.bindValue(QStringLiteral(":date"), QStringLiteral("2024-02-01"));
        insert.bindValue(QStringLiteral(":time"), QStringLiteral("00:00"));
        insert.bindValue(QStringLiteral(":call"), QString::fromLatin1(row.call));
        insert.bindValue(QStringLiteral(":band"), QString::fromLatin1(row.band));
        insert.bindValue(QStringLiteral(":mode"), QString::fromLatin1(row.mode));
        insert.bindValue(QStringLiteral(":prop_mode"), QString::fromLatin1(row.propMode));
        insert.bindValue(QStringLiteral(":entity"), row.entity);
        insert.bindValue(QStringLiteral(":qsl"), QString::fromLatin1(row.qsl));
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    }

    QVector<Database::DxccEntityProgress> progress;
    QString errorMessage;
    QVERIFY2(Database::dxccEntityProgress(&progress, &errorMessage), qPrintable(errorMessage));

    // Every current ARRL entity is listed, worked or not.
    QCOMPARE(progress.size(), 340);
    QVERIFY(std::is_sorted(progress.begin(), progress.end(),
                            [](const auto &a, const auto &b) { return a.entityCode < b.entityCode; }));

    const auto findRow = [&](int entityCode) -> const Database::DxccEntityProgress & {
        const auto it = std::find_if(progress.cbegin(), progress.cend(), [entityCode](const auto &row) {
            return row.entityCode == entityCode;
        });
        Q_ASSERT(it != progress.cend());
        return *it;
    };

    const QStringList &rowNames = Database::dxccChallengeRowNames();
    const QStringList &bandNames = Database::dxccChallengeBandNames();
    const int mixedRow = rowNames.indexOf(QStringLiteral("Mixed"));
    const int cwRow = rowNames.indexOf(QStringLiteral("CW"));
    const int phoneRow = rowNames.indexOf(QStringLiteral("Phone"));
    const int digitalRow = rowNames.indexOf(QStringLiteral("Digital"));
    const int satelliteRow = rowNames.indexOf(QStringLiteral("Satellite"));
    const int band20m = bandNames.indexOf(QStringLiteral("20M"));
    const int band40m = bandNames.indexOf(QStringLiteral("40M"));
    const int band2m = bandNames.indexOf(QStringLiteral("2M"));
    const int band10m = bandNames.indexOf(QStringLiteral("10M"));

    // Entity 1 (Canada): confirmed via both CW and SSB on 20M, plus CW on
    // 40M -- Mixed/CW/Phone all confirmed, Digital/Satellite not.
    const Database::DxccEntityProgress &canada = findRow(1);
    QCOMPARE(canada.prefix, QStringLiteral("VE")); // real cty.dat primary prefix
    QVERIFY(canada.modeConfirmed[mixedRow]);
    QVERIFY(canada.modeConfirmed[cwRow]);
    QVERIFY(canada.modeConfirmed[phoneRow]);
    QVERIFY(!canada.modeConfirmed[digitalRow]);
    QVERIFY(!canada.modeConfirmed[satelliteRow]);
    QVERIFY(canada.bandConfirmed[band20m]);
    QVERIFY(canada.bandConfirmed[band40m]);
    QVERIFY(!canada.bandConfirmed[band2m]);

    // Entity 6 (Alaska): FM via satellite on 2M (must land in Satellite
    // only, not Phone, despite FM being a phone mode) plus one unconfirmed
    // QSO on 10M that must be excluded entirely.
    const Database::DxccEntityProgress &alaska = findRow(6);
    QCOMPARE(alaska.prefix, QStringLiteral("KL"));
    QVERIFY(alaska.modeConfirmed[mixedRow]);
    QVERIFY(!alaska.modeConfirmed[phoneRow]);
    QVERIFY(alaska.modeConfirmed[satelliteRow]);
    QVERIFY(alaska.bandConfirmed[band2m]);
    QVERIFY(!alaska.bandConfirmed[band10m]); // unconfirmed QSO excluded

    // An entity with no QSOs at all is still listed, entirely unconfirmed.
    const Database::DxccEntityProgress &untouched = findRow(7); // Albania
    QVERIFY(!untouched.modeConfirmed[mixedRow]);
    for (bool confirmed : untouched.bandConfirmed)
        QVERIFY(!confirmed);

    QSqlQuery cleanup(db);
    QVERIFY2(cleanup.exec(QStringLiteral("DELETE FROM contacts WHERE call IN ('VE1AAA', 'KL7BBB')")),
             qPrintable(cleanup.lastError().text()));
}

void TstDatabase::clearAllContactsDeletesEverything()
{
    // Two contacts (W1AW, K5ABC) are left over from
    // importLotwAdifSkipsDuplicates() above.
    QString errorMessage;
    const int deleted = Database::clearAllContacts(&errorMessage);
    QCOMPARE(deleted, 2);

    QSqlDatabase db = QSqlDatabase::database(Database::connectionName());
    QSqlQuery count(db);
    QVERIFY2(count.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")),
             qPrintable(count.lastError().text()));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 0);
}

QTEST_GUILESS_MAIN(TstDatabase)
#include "tst_database.moc"
