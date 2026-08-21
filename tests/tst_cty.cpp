#include <QObject>
#include <QTemporaryFile>
#include <QTest>

#include "cty.h"

namespace {

// A small, self-contained cty.dat fixture covering: a shared two-alias
// prefix entry, a more specific prefix that must win over a shorter one, a
// primary-prefix field that is never repeated in its alias list, and an
// exact full-callsign ("=") override containing '/' that must NOT be
// truncated into a bogus generic prefix.
const char kFixture[] =
    "Test Country A:            5:  10:  NA:   40.00:  -100.00:    -5.0:  W:\n"
    "    W,K;\n"
    "Test Country B (Hawaii):  31:  61:  OC:   20.00:  -157.00:   -10.0:  KH6:\n"
    "    KH6;\n"
    "Test Malaysia:            28:  54:  AS:    4.00:   102.00:    -8.0:  9M6:\n"
    "    9M6;\n"
    "Test Exception Land:      26:  50:  AS:    9.00:  -114.00:    -8.0:  9M6X:\n"
    "    =9M6/LA6VM(37)[48];\n"
    "Solo Primary Land:         1:   1:  NA:    0.00:     0.00:     0.0:  ZP1:\n"
    "    =ZP1XYZ;\n";

} // namespace

class TstCty : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void basicPrefixMatch();
    void longestPrefixWins();
    void primaryPrefixWithoutAliasEntry();
    void exactCallOverrideKeepsSlash();
    void noMatchReturnsEmpty();
};

void TstCty::initTestCase()
{
    QVERIFY(!Cty::isLoaded()); // nothing loaded yet in this process

    QString loadError;
    QVERIFY(!Cty::load(QStringLiteral("Z:/does/not/exist/cty.dat"), &loadError));
    QVERIFY(!loadError.isEmpty());
    QVERIFY(!Cty::isLoaded()); // a failed load must not flip the flag

    QTemporaryFile fixture;
    QVERIFY(fixture.open());
    fixture.write(kFixture);
    fixture.flush();

    QVERIFY2(Cty::load(fixture.fileName(), &loadError), qPrintable(loadError));
    QVERIFY(Cty::isLoaded());
}

void TstCty::basicPrefixMatch()
{
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("W1AW")), QStringLiteral("Test Country A"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("K5ABC")), QStringLiteral("Test Country A"));
}

void TstCty::longestPrefixWins()
{
    // "KH6" (Hawaii) must win over the shorter, generic "K" prefix.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("KH6XYZ")),
              QStringLiteral("Test Country B (Hawaii)"));
}

void TstCty::primaryPrefixWithoutAliasEntry()
{
    // "ZP1" only appears in the primary-prefix field, never in the alias
    // list itself; a real callsign under it must still resolve.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("ZP1ABC")), QStringLiteral("Solo Primary Land"));
}

void TstCty::exactCallOverrideKeepsSlash()
{
    // The exact-callsign override "=9M6/LA6VM(37)[48]" must match verbatim,
    // slash and all -- truncating at '/' would collapse it into the
    // unrelated generic "9M6" prefix (see cty.cpp's bareAlias() comment).
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("9M6/LA6VM")),
              QStringLiteral("Test Exception Land"));

    // A callsign that merely starts with "9M6" but isn't the exact
    // exception must fall through to the ordinary prefix.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("9M6ABC")), QStringLiteral("Test Malaysia"));
}

void TstCty::noMatchReturnsEmpty()
{
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("ZZ9ZZZ")), QString());
    QCOMPARE(Cty::countryForCallsign(QString()), QString());
}

QTEST_APPLESS_MAIN(TstCty)
#include "tst_cty.moc"
