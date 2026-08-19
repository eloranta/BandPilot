# [BandPilot] — Specification

**Status:** Draft
**Author:** HE
**Last updated:**

---

## 1. Overview

**What is this app?**
General logging program for ham radio use.

**Target platform(s):**
- [x] Windows
- [ ] macOS
- [ ] Linux
- [ ] Embedded / other: IPhone/Android 

**Qt version:** 
**UI toolkit:** Widgets
**C++ standard:** 

---

## 2. Goals & non-goals

**Goals**
Create a qso database. Feed some seed data for now.

**Non-goals** (explicitly out of scope, to avoid scope creep)
-

---

## 3. Architecture

**High-level structure**
Three tier architecture: Ui, business logic, database level.

**Module / class breakdown**

| Module / Class | Responsibility | Depends on |
|---|---|---|
| main.cpp | program main() |  |
| `MainWindow` | Top-level UI, wires up other components |  |
| TableView | QTableView code |  |
| SqlModel | QSqlModel code |  |
| DataBase | Code handling database di |  |

**Threading model**

No threads used for now.

"Does anything run off the GUI thread? (e.g. worker threads via `QThread`, `QtConcurrent`, signal/slot across threads.) Note any thread-safety concerns."

**Separation of concerns**
use of signals/slots

---

## 4. Build system

- **Build tool:** CMake
- **Minimum CMake version** (if applicable):
- **Dependencies:**

| Dependency | Version | Purpose | How obtained (vcpkg/Conan/system/submodule) |
|---|---|---|---|
| Qt | | | |
| | | | |

- **Directory layout:**
```
BandPilot/
├── src/
├── include/
├── ui/            # .ui files, if using Widgets Designer
├── resources/      # .qrc, icons, images
├── tests/
└── CMakeLists.txt
```

---

## 5. Features

List each feature as a concrete, testable behavior — not a vague goal.

### 5.1 [create database]
- **Description:** when app started create sqlite db "bandpilot.sqlite" if not exists in standard location
- **User-facing behavior:** show result in statusline
- **Edge cases / error handling:**
- **Acceptance criteria:**

### 5.2 [create qso table]

- ##### Description: add table "qso" if not exists

- **User-facing behavior:** show result in statusline

### 5.3 [seed the table]

- ##### Description: if table is empty seed it with some generated data

- the schema: 

  CREATE TABLE IF NOT EXISTS contacts (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      date TEXT NOT NULL,
      time TEXT NOT NULL,
      call TEXT NOT NULL,
      band TEXT NOT NULL,
      frequency TEXT NOT NULL,
      mode TEXT NOT NULL,
      submode TEXT,
      country TEXT,
      dxcc_entity TEXT,
      deleted_entity TEXT,
      grid TEXT,
      rst_tx TEXT,
      rst_rx TEXT,
      qsl TEXT,
      comment TEXT
    )

- **User-facing behavior:** the seed data is shown in view



---

## 6. UI / UX

**Layout overview**
Sketch, wireframe link, or plain-text description of the main window(s)/screen(s).

**Key widgets / components**

main window with menubar, embedded tableview and statusbar



|      |      |      |
|---|---|---|
| | | |

**Styling approach**
Qt Style Sheets (QSS).

**Interaction notes**
Keyboard shortcuts, drag-and-drop, context menus, accessibility considerations.

---

## 7. Data model

**Core data structures**
Key classes/structs representing the app's data, and their relationships.

**Persistence**
- Format: (SQLite via `QSqlDatabase`   / `QSettings`)
- File location(s):
- Schema / versioning strategy: done later

**External integrations** (APIs, network services, hardware, etc.)
- Endpoint/protocol:
- Auth:
- Error/retry handling:

---

## 8. Error handling & logging

- Exception policy (exceptions vs error codes vs `std::expected`)
- Logging approach (`QLoggingCategory`, log file location, verbosity levels)
- Crash reporting (if any)

---

## 9. Testing

- **Framework:** Qt Test / Catch2 / GoogleTest not for now
- **Unit test coverage targets:**
- **Integration/UI test approach:** (e.g. `QTest` with `QTestEventList` for simulated input)
- **CI setup:** (if applicable)

---

## 10. Packaging & deployment

- Installer/packaging tool: (windeployqt/macdeployqt, Qt Installer Framework, AppImage, etc.) not for now
- Versioning scheme: version number in source code - shown in caption bar after app name
- Release checklist:

---

## 11. Open questions

- [ ]
- [ ]

---

## 12. Milestones / rough timeline

| Milestone | Description | Target date |
|---|---|---|
| M1 | | |
| M2 | | |
