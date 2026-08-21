#include "cty.h"

#include <QFile>
#include <QMap>
#include <QStringList>

namespace {

QMap<QString, QString> &prefixMap()
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
    const QStringList records = content.split(QLatin1Char(';'));
    for (const QString &record : records) {
        const QString trimmedRecord = record.trimmed();
        if (trimmedRecord.isEmpty())
            continue;

        const QStringList fields = trimmedRecord.split(QLatin1Char(':'));
        if (fields.size() < 9)
            continue; // malformed/partial record

        const QString name = fields.at(0).trimmed();

        map.insert(bareAlias(fields.at(7)), name); // primary prefix
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

    const auto exactIt = exactMap().constFind(callsign);
    if (exactIt != exactMap().constEnd())
        return exactIt.value();

    const QMap<QString, QString> &map = prefixMap();
    for (int len = callsign.size(); len > 0; --len) {
        const auto it = map.constFind(callsign.left(len));
        if (it != map.constEnd())
            return it.value();
    }
    return QString();
}

} // namespace Cty
