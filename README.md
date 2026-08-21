# BandPilot

Ham radio QSO logger. See `project-spec-template.md` for the full spec.

## Status

First working slice per spec sections 5.1–5.3:
- Creates `bandpilot.sqlite` in the standard per-user app data location if it doesn't exist
- Creates the `contacts` table if it doesn't exist
- Seeds 8 sample QSOs if the table is empty
- Shows the contacts in a `QTableView`, and reports database status in the status bar

## Requirements

- Qt 6.11 (or any Qt 6.x with the Widgets and Sql modules; Sql needs the SQLite driver, which ships with Qt by default)
- CMake 3.19+
- A C++20 compiler (MSVC 2022, or MinGW as shipped with Qt)

## Building on Windows

Using Qt Creator (easiest): open `CMakeLists.txt` as a project, select your Qt 6 kit, and build/run.

From the command line (adjust the Qt path to your install):

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
cmake --build build --config Debug
build\Debug\BandPilot.exe
```

The `windeployqt` step in `CMakeLists.txt` runs automatically after each build on Windows and copies the required Qt DLLs next to the executable, so it can be run directly from the build folder.

## Project layout

```
BandPilot/
├── CMakeLists.txt
├── src/
│   ├── main.cpp        # entry point
│   ├── version.h        # single source of truth for the version shown in the title bar
│   ├── mainwindow.h/.cpp # UI layer: menu bar, QTableView, status bar
│   ├── database.h/.cpp   # data layer: SQLite open/create/seed, ADIF import
│   └── cty.h/.cpp        # cty.dat (callsign -> DXCC country) parsing/lookup
├── resources/            # dxcc-entities.txt seed data, icons/.qrc (icons TBD)
└── tests/                # Qt Test unit tests (tst_cty.cpp), run via ctest
```

## Known deviation from spec

Section 5.2 refers to the table as `qso`, but the schema in 5.3 creates a table
named `contacts`. This build uses `contacts` (matching the schema). Rename in
`database.cpp` if you'd rather it be `qso`.

## Next steps (not yet implemented)

- Add/edit/delete QSO forms (currently the table view is directly editable via `OnFieldChange`, but there's no dedicated entry form)
- DXCC/country lookup to populate `dxcc_entity`
- App icon / `.qrc` resources
- `QSettings`-based window state persistence
