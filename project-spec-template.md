# BandPilot — Specification

**Status:** Living document (reflects current implementation)
**Author:** HE
**Last updated:** 2026-08-21

---

## 1. Overview

**What is this app?**
BandPilot is a desktop QSO (ham radio contact) logging application. It maintains a local SQLite log of contacts, resolves each contact's DXCC entity (country) from its callsign via AD1C's cty.dat prefix file, imports logs from ADIF files and from ARRL's Logbook of The World (LoTW), and tracks DXCC award progress (Mixed/CW/Phone/Digital/Satellite and per-band credits, plus the DXCC Challenge).

**Target platform(s):**
- [x] Windows
- [ ] macOS
- [ ] Linux
- [ ] Embedded / other

(Built and tested on Windows with MinGW; nothing in the code is Windows-specific, but only Windows is currently built/deployed via windeployqt.)

**Qt version:** 6.9.0 (Qt6, Widgets-based)
**UI toolkit:** Widgets
**C++ standard:** C++20

---

## 2. Goals & non-goals

**Goals**
- Maintain a local, durable log of QSOs (SQLite database, `bandpilot.sqlite`, in the standard per-user app-data location).
- Correlate each contact's callsign to a DXCC entity automatically, using AD1C's cty.dat (longest-prefix match, exact-callsign overrides, and portable-suffix ("homecall/COUNTRYPREFIX") resolution).
- Import existing logs from ADIF (`.adi`/`.adif`) files.
- Import QSL confirmations from LoTW directly (HTTPS report download), without duplicating already-logged QSOs.
- Show DXCC award progress: total QSOs, entities worked/confirmed, and a per-award (mode + band + Challenge) credit breakdown, styled after LoTW's own "DXCC Award Account Status" page.

**Non-goals** (explicitly out of scope, to avoid scope creep)
- Live/real-time logging from WSJT-X or other rig-control software (no UDP listener; contacts arrive only via ADIF/LoTW import today).
- macOS/Linux builds (not currently built or tested there, though the code has no Windows-specific dependencies).
- Multi-user / cloud sync / shared logbooks.
- Tracking ARRL's DXCC *award-application* workflow (new/submitted/in-process/granted) — the app only knows "LoTW-confirmed or not," not whether credit has been formally applied for or granted.
- Awards other than DXCC (no WAS, WAZ, VUCC, etc.).

---

## 3. Architecture

**High-level structure**
Two effective layers on top of a thin UI shell: a UI layer (`MainWindow`) that owns all widgets, menus, and user interaction, and a data/business layer (`Database::` and `Cty::` namespaces) that owns all SQL and all cty.dat parsing/lookup. `MainWindow` never issues SQL directly; it calls into `Database::` functions and reports results via the status bar.

**Module / class breakdown**

| Module / Class | Responsibility | Depends on |
|---|---|---|
| `main.cpp` | Program entry point; constructs `QApplication`, sets org/app name (for `QSettings`/`QStandardPaths`), shows `MainWindow` | `MainWindow` |
| `MainWindow` (`mainwindow.h/.cpp`) | Top-level UI: menu bar, tab widget (Contacts table + DXCC tab), status bar, dialogs (ADIF file picker, LoTW login), context menu, LoTW network fetch | `Database`, Qt Widgets, Qt Network |
| `Database` namespace (`database.h/.cpp`) | Owns the SQLite connection; schema creation/migration; ADIF parsing and import (plain + LoTW, with dedup); DXCC entity name correlation (cty.dat name → ARRL `dxcc_entity` code); contact/DXCC-award statistics | `Cty`, Qt Sql |
| `Cty` namespace (`cty.h/.cpp`) | Parses AD1C's cty.dat; resolves a callsign to a DXCC country name by longest-prefix match, exact-callsign override, or portable-suffix heuristic. Knows nothing about this app's own `dxcc_entity` table — `Database::` does the name correlation | (none; pure parsing/lookup) |

There are no separate `TableView`/`SqlModel` classes — the contacts view is a plain `QTableView` bound to a `QSqlRelationalTableModel` (joined to `dxcc_entity` for the entity name), both owned directly by `MainWindow`.

**Threading model**
Everything runs on the GUI thread. The one asynchronous operation is the LoTW HTTPS request (`QNetworkAccessManager::get`), which is non-blocking I/O via the Qt event loop (`QNetworkReply::finished`), not a separate thread. No `QThread`/`QtConcurrent` use; no cross-thread data access.

**Separation of concerns**
`Database::` functions take/return plain data (no `QWidget` dependency) and report failure via `bool` return + `QString *errorMessage` out-param, so they're usable both from `MainWindow` and from the test suite without any UI. `Cty::` is a free-standing prefix-matching engine with no database or UI knowledge at all. `MainWindow` wires UI events (menu actions, `QNetworkReply::finished`, table edits) to `Database::` calls via Qt signals/slots, and is the only layer that touches `QMessageBox`/`QFileDialog`/`QSettings`/the status bar.

---

## 4. Build system

- **Build tool:** CMake
- **Minimum CMake version:** 3.19
- **Dependencies:**

| Dependency | Version | Purpose | How obtained |
|---|---|---|---|
| Qt6 | 6.9.0 | Widgets (UI), Sql (SQLite access), Network (LoTW HTTPS), Test (unit tests) | System Qt install (`find_package(Qt6 ...)`) |
| SQLite | bundled with Qt's `QSQLITE` driver | Local database engine | Via Qt's `Qt6::Sql` plugin |

- **Directory layout** (actual, not aspirational):
```
BandPilot/
├── src/                  # main.cpp, mainwindow.h/.cpp, database.h/.cpp, cty.h/.cpp, version.h
├── resources/            # dxcc-entities.txt (ARRL DXCC entity list, code|name per line)
├── tests/                # tst_cty.cpp, tst_database.cpp, tests/data/cty.dat (real fixture), CMakeLists.txt
└── CMakeLists.txt
```
No `include/` or `ui/` directories — headers live next to their `.cpp` files, and the UI is built entirely in code (no Qt Designer `.ui` files).

Windows builds copy the Qt runtime DLLs next to the executable via `windeployqt` (POST_BUILD step), and copy `resources/dxcc-entities.txt` next to the executable so `Database::initialize()` can seed the `dxcc_entity` table on first run.

---

## 5. Features

### 5.1 Database initialization
- **Description:** On startup, `Database::initialize()` opens (creating if needed) `bandpilot.sqlite` in the standard per-user app-data location, creates the `contacts` and `dxcc_entity` tables if missing, runs schema migrations for existing databases (e.g. adding the `prop_mode` column), seeds `dxcc_entity` from the bundled `resources/dxcc-entities.txt` if empty, and best-effort loads `cty.dat` (see 5.5) if the user has placed one alongside the database.
- **User-facing behavior:** Status bar shows `Database ready: <path> (<N> contacts)` on success, or `Database error: <message>` on failure.
- **Edge cases / error handling:** Missing `cty.dat` is not an error — DXCC lookup during import just falls back to "(Unknown)". A missing bundled `dxcc-entities.txt` next to the executable *is* a startup error.

### 5.2 Contacts table view
- **Description:** The "Contacts" tab shows every row of `contacts` in a `QTableView` bound via `QSqlRelationalTableModel` (with `dxcc_entity` resolved to its entity name through a join). Cells are editable in place (`OnFieldChange` edit strategy commits each field as it's changed).
- **User-facing behavior:** Sortable, resizable columns; DXCC entity shown by name, not code.

### 5.3 ADIF import (File > Import > Adif...)
- **Description:** Prompts for an `.adi`/`.adif` file, parses it (length-prefixed ADIF field parser), and inserts every valid record (must have a callsign, date, and time) as a new contact, resolving DXCC entity from the callsign via cty.dat.
- **User-facing behavior:** Status bar reports `Imported N contact(s) from <path>` or the failure reason.
- **Edge cases:** Records missing call/date/time are silently skipped. Missing `band`/`frequency`/`mode` (not uncommon in real-world exports) bind as empty strings, not NULL, since those columns are `NOT NULL`.

### 5.4 LoTW import (File > Import > LoTW...)
- **Description:** Prompts for LoTW login, password, and a "QSLs since" date; downloads the QSL report over HTTPS (`lotw.arrl.org/lotwuser/lotwreport.adi`); imports records not already logged (deduplicated by date+time+call+band+mode, so re-running doesn't create duplicates). Login/password are remembered across sessions via `QSettings` (password XOR-obfuscated + base64, not encrypted — documented in code as such).
- **User-facing behavior:** Status bar reports `LoTW import: N new, M already logged, K skipped`. Failures (network error, no records/bad login) are shown on the status bar *and* logged via `qWarning()` (the password itself is never logged).
- **Edge cases:** The request is pinned to HTTPS with `NoLessSafeRedirectPolicy` since the password travels as a query parameter.

### 5.5 DXCC entity resolution (`Cty::`)
- **Description:** Parses AD1C's "Big CTY" cty.dat format and resolves a callsign to a DXCC country name by longest-matching prefix, with exact-callsign overrides (`=CALL` entries) taking precedence, and a portable-suffix heuristic for calls like `K6VHF/HR9` (the part after the last `/` is tried first, unless it's an operating-mode indicator like `/M`/`/MM`/`/QRP` or a bare call-area digit). `Database::` then correlates that country name to this app's own ARRL-derived `dxcc_entity` table via name normalization plus a small hand-curated override table for known spelling/ambiguity mismatches (e.g. "Vietnam"/"Viet Nam", the Cocos Island/Cocos (Keeling) Islands collision).
- **User-facing behavior:** Invisible to the user except through its result — the DXCC Entity column and the DXCC tab's credit counts.
- **Edge cases:** cty.dat is user-supplied (not bundled — it's a frequently updated external download), placed at `<app-data>/cty.dat`; its absence just means every import falls back to "(Unknown)".

### 5.6 Clear all contacts (right-click context menu)
- **Description:** Right-clicking the contacts table offers "Clear All...", which permanently deletes every contact after a confirmation dialog (`QMessageBox::warning`, defaulting to Cancel).
- **User-facing behavior:** Status bar reports `Cleared N contact(s)`.

### 5.7 Status bar summary
- **Description:** A permanent status bar label shows `N QSOs · M DXCC confirmed / K worked`, recomputed after every import, clear, and in-place table edit.

### 5.8 DXCC tab
- **Description:** A second tab listing every current ARRL DXCC entity (one row each, worked or not), styled after LoTW's own per-entity "countries confirmed" checklist report. Columns: a representative Prefix (derived from cty.dat, if loaded), Entity name, five mode columns (Mix/Ph/CW/RT/SAT — "X" if at least one QSO for that entity is LoTW-confirmed in that mode category, "Mix" meaning any mode), and twelve band columns (160m–70cm — "X" if confirmed on that band, any mode). Sortable by clicking any column header.
- **Edge cases:** Satellite QSOs are identified by ADIF's `PROP_MODE = SAT` field (not by modulation), so a satellite QSO logged as e.g. FM marks only the Satellite column, not Phone. Entities with no cty.dat-correlated record (or when cty.dat isn't loaded) show a blank Prefix. Deleted/former DXCC entities are not shown — this app only has ARRL's current entity list (`resources/dxcc-entities.txt`), not a deleted-entities list.

---

## 6. UI / UX

**Layout overview**
A single main window: menu bar (`File > Import > Adif.../LoTW...`, `File > Quit`, `Help > About`) at the top, a `QTabWidget` filling the central area with two tabs ("Contacts" — the QSO table; "DXCC" — the award credit list), and a status bar at the bottom (transient messages on the left, a permanent stats label on the right).

**Key widgets / components**

| Widget | Purpose |
|---|---|
| `QTabWidget` | Switches between the Contacts table and the DXCC tab |
| `QTableView` + `QSqlRelationalTableModel` | Editable contacts grid |
| `QTableWidget` | Read-only DXCC award credit list |
| `QMenuBar` / `QMenu` / `QAction` | File > Import (Adif/LoTW), Quit, Help > About |
| `QStatusBar` + permanent `QLabel` | Transient operation feedback + persistent stats |
| `QDialog` (ad hoc, via `QFormLayout`) | LoTW login prompt (login/password/QSLs-since) |
| Context menu (`QMenu` on `customContextMenuRequested`) | "Clear All..." on the contacts table |

**Styling approach**
None — default platform (Windows) widget style; no QSS applied.

**Interaction notes**
Right-click context menu on the contacts table for destructive actions (currently just Clear All, gated by a confirmation dialog). No keyboard shortcuts defined beyond the platform-standard Quit shortcut. No drag-and-drop.

---

## 7. Data model

**Core data structures**

`contacts` table (one row per QSO):
```sql
CREATE TABLE contacts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT NOT NULL,           -- ISO yyyy-MM-dd
    time TEXT NOT NULL,           -- HH:mm
    call TEXT NOT NULL,
    band TEXT NOT NULL,
    frequency TEXT NOT NULL,
    mode TEXT NOT NULL,
    submode TEXT,
    dxcc_entity INTEGER CHECK (dxcc_entity IS NULL OR (dxcc_entity BETWEEN 1 AND 999)),
    deleted_entity TEXT DEFAULT '',
    grid TEXT,
    rst_tx TEXT,
    rst_rx TEXT,
    qsl TEXT,                     -- ADIF QSL_RCVD, e.g. 'Y'
    comment TEXT,
    prop_mode TEXT                -- ADIF PROP_MODE, e.g. 'SAT' for satellite
)
```

`dxcc_entity` table (ARRL DXCC entity list, seeded from `resources/dxcc-entities.txt`):
```sql
CREATE TABLE dxcc_entity (
    entity_code INTEGER PRIMARY KEY,
    entity TEXT NOT NULL
)
```
Entity code 999 is a reserved "(Unknown)" sentinel, used instead of `NULL` so `QSqlRelationalTableModel`'s inner join doesn't silently hide unresolved contacts.

**Persistence**
- Format: SQLite via `QSqlDatabase` (`bandpilot.sqlite`), plus `QSettings` for LoTW login/password (native format — registry on Windows).
- File location: `QStandardPaths::AppDataLocation` (database, `cty.dat`, `dxcc-entities.txt`).
- Schema / versioning strategy: no formal migration framework; ad hoc idempotent migrations run on every `initialize()` call (e.g. `ensureContactsPropModeColumn()` checks `PRAGMA table_info` before `ALTER TABLE`).

**External integrations**
- **LoTW QSL report:** `GET https://lotw.arrl.org/lotwuser/lotwreport.adi?login=...&password=...&qso_query=1&qso_qsl=yes&qso_qslsince=...`. Auth is the user's LoTW login/password, sent as query parameters over HTTPS (pinned redirect policy). No retry logic; a failed request just reports the error.
- **cty.dat:** a local file the user places at `<app-data>/cty.dat` (not fetched automatically; AD1C's "Big CTY" is a frequently-updated external download).

---

## 8. Error handling & logging

- **Exception policy:** No exceptions. Every fallible `Database::`/`Cty::` function returns `bool` (or a sentinel like `-1`) plus an optional `QString *errorMessage` out-parameter.
- **Logging approach:** No `QLoggingCategory` in use. `qWarning()` is used for LoTW import failures (network error, DB error, empty/no-record response) and for stats-computation failures, in addition to the corresponding status bar message. No log file — output goes to the default Qt message handler (console/debugger).
- **Crash reporting:** None.

---

## 9. Testing

- **Framework:** Qt Test, two targets under `tests/`:
  - `tst_cty` (9 tests) — exercises `Cty::` against both a small synthetic cty.dat fixture (edge cases: longest-prefix precedence, exact-call overrides, primary-prefix-only entries) and the real, checked-in `tests/data/cty.dat` (real-world callsigns, including the portable-suffix heuristic and a documented false-positive trade-off).
  - `tst_database` (10 tests) — exercises `Database::` against an isolated in-memory SQLite database seeded from the real `resources/dxcc-entities.txt`, covering DXCC name-correlation edge cases (Vietnam, Cocos Island/Cocos (Keeling) Islands, informational-only cty.dat entries), LoTW import dedup, missing-field tolerance, `clearAllContacts()`, and `dxccAwardCredits()`.
- **Unit test coverage targets:** No formal target; `Database::`/`Cty::` logic is covered as it's added. `MainWindow` (UI wiring) is not unit-tested — verified by manual/build smoke-testing only.
- **Integration/UI test approach:** None automated; UI changes are smoke-tested by launching the built app.
- **CI setup:** None currently; tests are run locally via `ctest` (from the build directory) or by running `tst_cty.exe`/`tst_database.exe` directly.

---

## 10. Packaging & deployment

- **Installer/packaging tool:** None yet — `windeployqt` bundles the Qt runtime DLLs next to `BandPilot.exe` as a POST_BUILD step; no installer.
- **Versioning scheme:** `BANDPILOT_VERSION` in `src/version.h` (currently `"0.1.0"`), shown in the main window's title bar.
- **Release checklist:** None formalized yet.

---

## 11. Open questions

- [ ] Live logging (WSJT-X UDP or similar) is out of scope today — worth reconsidering if ADIF/LoTW-only import proves too manual in practice.
- [ ] `prop_mode` (needed for Satellite DXCC tracking) was only just added — existing databases won't show any satellite QSOs until re-imported from a source that includes ADIF's `PROP_MODE` field.
- [ ] No band/mode value normalization on import (e.g. "20m" vs "20M", "SSB" vs "USB") — DXCC award/Challenge counts assume case-insensitive but otherwise consistent band naming from the import source.
- [ ] macOS/Linux support: nothing platform-specific in the code, but untested and not built there.

---

## 12. Milestones / rough timeline

No formal timeline. Delivered incrementally: database/contacts table → DXCC entity seed data → ADIF import → cty.dat-based DXCC resolution → LoTW import → Clear All → status bar stats → DXCC award tab.
