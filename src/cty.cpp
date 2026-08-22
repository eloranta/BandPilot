#include "cty.h"

#include <QFile>
#include <QMap>
#include <QSet>
#include <QStringList>

namespace {

QMap<QString, QString> &prefixMap()
{
    static QMap<QString, QString> map;
    return map;
}

// Country name -> that record's own primary-prefix field (e.g. "Monaco" ->
// "3A"), one entry per cty.dat record. Separate from prefixMap() (which is
// keyed by prefix, not name, and includes every alias too) -- this exists
// purely so callers that already have a country name (from
// countryForCallsign() or allCountryNames()) can recover a representative
// prefix for it, e.g. to display alongside the entity in a table.
QMap<QString, QString> &countryPrimaryPrefixMap()
{
    static QMap<QString, QString> map;
    return map;
}

// Exact-callsign overrides ("=CALL" entries), keyed by the full callsign
// verbatim. Kept separate from prefixMap() because these must only match a
// callsign that equals the key exactly -- unlike ordinary prefixes, they
// must NOT also match as a prefix of some longer callsign (e.g. the
// override "=KL7A" for a specific ham must not swallow "KL7ABC").
QMap<QString, QString> &exactMap()
{
    static QMap<QString, QString> map;
    return map;
}

bool &loadedFlag()
{
    static bool loaded = false;
    return loaded;
}

// Strips a cty.dat alias down to a bare prefix/callsign: the leading "="
// (full-callsign marker, kept implicitly by leaving the rest of the string
// untouched) and any trailing override annotations -- "(CQzone)",
// "[ITUzone]", "<lat/lon>", "{continent}", "~UTCoffset~". Unlike some
// parsers, this does NOT truncate at '/': aliases such as "=9M6/LA6VM" are
// full callsigns that legitimately contain a slash, and truncating them
// would turn an exact-callsign override into a bogus generic prefix.
QString bareAlias(QString alias)
{
    alias = alias.trimmed();
    if (alias.startsWith(QLatin1Char('=')))
        alias.remove(0, 1);

    int cut = alias.size();
    for (const QChar opener : {QLatin1Char('('), QLatin1Char('['), QLatin1Char('<'),
                                QLatin1Char('{'), QLatin1Char('~')}) {
        const int idx = alias.indexOf(opener);
        if (idx >= 0 && idx < cut)
            cut = idx;
    }
    alias.truncate(cut);
    return alias;
}

// Longest-prefix lookup against prefixMap() only (no exact-callsign
// overrides -- those only ever apply to a full literal callsign, never to
// a substring of one).
QString longestPrefixMatch(const QString &token)
{
    const QMap<QString, QString> &map = prefixMap();
    for (int len = token.size(); len > 0; --len) {
        const auto it = map.constFind(token.left(len));
        if (it != map.constEnd())
            return it.value();
    }
    return QString();
}

} // namespace

namespace Cty {

bool load(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    const QString content = QString::fromLatin1(file.readAll());
    file.close();

    // Records are ';'-terminated, each with 9 ':'-delimited fields: name,
    // CQ zone, ITU zone, continent, lat, lon, UTC offset, primary prefix,
    // then a comma-separated alias list (which may span several physical
    // lines; that's fine since we only split on ',' and ':').
    QMap<QString, QString> map;
    QMap<QString, QString> exact;
    QMap<QString, QString> countryPrefix;
    const QStringList records = content.split(QLatin1Char(';'));
    for (const QString &record : records) {
        const QString trimmedRecord = record.trimmed();
        if (trimmedRecord.isEmpty())
            continue;

        const QStringList fields = trimmedRecord.split(QLatin1Char(':'));
        if (fields.size() < 9)
            continue; // malformed/partial record

        const QString name = fields.at(0).trimmed();
        const QString primaryPrefix = bareAlias(fields.at(7));

        map.insert(primaryPrefix, name);
        countryPrefix.insert(name, primaryPrefix);
        for (const QString &rawAlias : fields.at(8).split(QLatin1Char(','))) {
            const QString trimmedAlias = rawAlias.trimmed();
            const QString bare = bareAlias(trimmedAlias);
            if (bare.isEmpty())
                continue;
            if (trimmedAlias.startsWith(QLatin1Char('=')))
                exact.insert(bare, name);
            else
                map.insert(bare, name);
        }
    }

    if (map.isEmpty() && exact.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No records parsed from %1").arg(filePath);
        return false;
    }

    prefixMap() = std::move(map);
    exactMap() = std::move(exact);
    countryPrimaryPrefixMap() = std::move(countryPrefix);
    loadedFlag() = true;
    return true;
}

bool isLoaded()
{
    return loadedFlag();
}

QString countryForCallsign(const QString &callsign)
{
    if (!loadedFlag())
        return QString();

    // An exact-callsign override match on the literal string (which may
    // itself contain a '/', e.g. "=9M6/LA6VM") always takes precedence,
    // including over the portable-suffix heuristic below.
    const auto exactIt = exactMap().constFind(callsign);
    if (exactIt != exactMap().constEnd())
        return exactIt.value();

    // A portable operation denotes a DXCC entity change and takes
    // precedence over the home call's own prefix -- but hams write this
    // two different ways, and both are common: "homecall/COUNTRYPREFIX"
    // (e.g. "K6VHF/HR9", a US ham signing Honduras) and
    // "COUNTRYPREFIX/homecall" (e.g. "3A/IW1RBI", an Italian ham signing
    // Monaco). The genuine country-changing segment is essentially always
    // the *shorter* of the two -- real DXCC prefixes are 1-4 characters,
    // while a home callsign is longer -- so try whichever side of the
    // slash is shorter first (ties favor the suffix, matching plain
    // "homecall/AREA" digits like "W1AW/4"). The other side is never
    // tried on its own: a home call very often itself starts with some
    // country's real prefix (e.g. "IW1RBI" legitimately starts with
    // Italy's "I"), so blindly prefix-matching the *longer* segment would
    // just silently resolve to the visiting ham's home country instead of
    // where they were actually operating.
    //
    // Excluded from either side: operating-mode indicators (mobile,
    // portable, maritime/aeronautical mobile, QRP) and bare call-area
    // digits, neither of which changes the country. This is a heuristic,
    // not exact: a handful of countries (e.g. Nordic special-activation
    // suffixes like "/LH", "/SA") reuse another country's prefix letters
    // for a non-country marker; cty.dat resolves those via curated exact
    // overrides, which are still checked first, above.
    const int slash = callsign.lastIndexOf(QLatin1Char('/'));
    if (slash > 0 && slash < callsign.size() - 1) {
        const QString before = callsign.left(slash);
        const QString after = callsign.mid(slash + 1);
        const QString &candidate = after.size() <= before.size() ? after : before;

        static const QSet<QString> kOperatingModeSuffixes = {
            QStringLiteral("M"),  QStringLiteral("P"),   QStringLiteral("MM"),
            QStringLiteral("AM"), QStringLiteral("A"),   QStringLiteral("QRP"),
        };
        bool numeric = !candidate.isEmpty();
        for (const QChar c : candidate) {
            if (!c.isDigit()) {
                numeric = false;
                break;
            }
        }
        if (!numeric && !kOperatingModeSuffixes.contains(candidate)) {
            const QString candidateCountry = longestPrefixMatch(candidate);
            if (!candidateCountry.isEmpty())
                return candidateCountry;
        }
    }

    return longestPrefixMatch(callsign);
}

QStringList allCountryNames()
{
    if (!loadedFlag())
        return {};
    return countryPrimaryPrefixMap().keys();
}

QString primaryPrefixForCountry(const QString &countryName)
{
    if (!loadedFlag())
        return QString();
    return countryPrimaryPrefixMap().value(countryName);
}

} // namespace Cty
