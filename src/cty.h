#pragma once

#include <QString>

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

} // namespace Cty
