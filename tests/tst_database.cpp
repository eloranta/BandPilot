#include <QFile>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>
#include <QTextStream>

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

QTEST_GUILESS_MAIN(TstDatabase)
#include "tst_database.moc"
