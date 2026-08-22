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
    void primaryPrefixAndCountryEnumeration();

    // These reload Cty:: from the real, checked-in cty.dat (tests/data/cty.dat)
    // instead of the synthetic fixture above, so they must run after the
    // synthetic-fixture tests -- once loaded here, the real table stays
    // active for the rest of the test run.
    void realCtyDatLoads();
    void realCtyDatKnownCallsigns();
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

void TstCty::primaryPrefixAndCountryEnumeration()
{
    const QStringList names = Cty::allCountryNames();
    QCOMPARE(names.size(), 5); // one per record in kFixture
    QVERIFY(names.contains(QStringLiteral("Test Country A")));
    QVERIFY(names.contains(QStringLiteral("Test Exception Land")));

    // Each record's own primary-prefix field, not an alias -- "Test
    // Exception Land"'s primary prefix is "9M6X", distinct from its only
    // alias, the exact override "=9M6/LA6VM(...)".
    QCOMPARE(Cty::primaryPrefixForCountry(QStringLiteral("Test Country A")), QStringLiteral("W"));
    QCOMPARE(Cty::primaryPrefixForCountry(QStringLiteral("Test Country B (Hawaii)")), QStringLiteral("KH6"));
    QCOMPARE(Cty::primaryPrefixForCountry(QStringLiteral("Test Exception Land")), QStringLiteral("9M6X"));
    QCOMPARE(Cty::primaryPrefixForCountry(QStringLiteral("Solo Primary Land")), QStringLiteral("ZP1"));
    QCOMPARE(Cty::primaryPrefixForCountry(QStringLiteral("No Such Country")), QString());
}

void TstCty::realCtyDatLoads()
{
    const QString path = QFINDTESTDATA("data/cty.dat");
    QVERIFY(!path.isEmpty());

    QString loadError;
    QVERIFY2(Cty::load(path, &loadError), qPrintable(loadError));
    QVERIFY(Cty::isLoaded());
}

void TstCty::realCtyDatKnownCallsigns()
{
    // Sanity-check a handful of well-known real-world callsign prefixes
    // against AD1C's actual "Big CTY" data, including two cases (Hawaii and
    // Alaska) where a more specific real-world prefix must win over the
    // generic "K"/United States entry -- the same longest-match rule
    // exercised with synthetic data above.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("W1AW")), QStringLiteral("United States"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("KH6XYZ")), QStringLiteral("Hawaii"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("KL7ABC")), QStringLiteral("Alaska"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("OH2BH")), QStringLiteral("Finland"));

    // "=KL7A" is a real exact-callsign override in cty.dat's United States
    // record (a mainland ham holding that vanity call). A callsign that
    // merely starts with "KL7A" but isn't that exact call -- like
    // "KL7ABC" above -- must NOT inherit the override; it must fall
    // through to the ordinary "KL" prefix match (Alaska). This regression
    // was found by testing against the real file: a naive longest-prefix
    // scan that doesn't treat "=" entries as exact-only would wrongly
    // match "KL7A" as a 4-character prefix of "KL7ABC".
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("KL7A")), QStringLiteral("United States"));

    // A US ham (K6VHF) operating portable from Honduras signs "K6VHF/HR9":
    // the "/HR9" suffix -- not the "K6VHF" home-call prefix -- determines
    // the DXCC entity for a portable operation like this.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("K6VHF/HR9")), QStringLiteral("Honduras"));

    // A curated exact override wins even when its suffix looks like a
    // portable-country indicator: "OH1BGG/SA" is one of AD1C's own
    // Finland exceptions (a Finnish special-activation suffix, not a
    // Sweden operation), and must stay Finland despite "SA" also being a
    // real Swedish prefix.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("OH1BGG/SA")), QStringLiteral("Finland"));

    // The opposite portable convention -- "COUNTRYPREFIX/homecall" (common
    // in Europe/Africa), e.g. an Italian ham (home call IW1RBI) operating
    // from Monaco signs "3A/IW1RBI". The short "3A" segment before the
    // slash is the country change; "IW1RBI" is just the home call, and
    // must NOT be prefix-matched on its own even though it legitimately
    // starts with Italy's real prefix "I" -- that's exactly the false
    // match this heuristic exists to avoid. Found via real BandPilot data:
    // three Monaco QSOs (3A/IW1RBI, 3A/PB8DX, 3A/F6EXV) were silently
    // misattributed to Italy, the Netherlands, and France respectively.
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("3A/IW1RBI")), QStringLiteral("Monaco"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("3A/PB8DX")), QStringLiteral("Monaco"));
    QCOMPARE(Cty::countryForCallsign(QStringLiteral("3A/F6EXV")), QStringLiteral("Monaco"));
}

QTEST_APPLESS_MAIN(TstCty)
#include "tst_cty.moc"
