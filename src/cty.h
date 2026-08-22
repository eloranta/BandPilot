#pragma once

#include <QString>
#include <QStringList>

// Parses AD1C's "Big CTY" cty.dat country file and looks up the DXCC
// country name for a callsign by longest-matching-prefix. Deliberately
// knows nothing about this app's own "dxcc_entity" table or entity codes —
// Database:: is responsible for correlating the name returned here to an
// ARRL DXCC entity code.
namespace Cty {

// Loads (or reloads) the prefix table from a cty.dat file. Returns false
// (with *errorMessage, if non-null, set to a human-readable description) if
// the file can't be read or no records could be parsed from it.
bool load(const QString &filePath, QString *errorMessage = nullptr);

// True once load() has succeeded at least once in this process.
bool isLoaded();

// Returns the DXCC country name for callsign, spelled as cty.dat spells it,
// or an empty string if nothing has been loaded or no prefix matches.
QString countryForCallsign(const QString &callsign);

// Every distinct country name known to the loaded cty.dat (one per
// record), in no particular order. Empty if nothing has been loaded. For
// callers that need to enumerate records rather than look up a single
// callsign (e.g. building a representative prefix per DXCC entity).
QStringList allCountryNames();

// Returns the primary-prefix field (as cty.dat spells it, e.g. "3A" for
// Monaco) of the record named countryName -- as returned by
// countryForCallsign() or found in allCountryNames() -- or an empty string
// if nothing has been loaded or countryName isn't a known record.
QString primaryPrefixForCountry(const QString &countryName);

} // namespace Cty
