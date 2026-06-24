#include "mainwindow.h"
#include "udpreceiver.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>

#include <algorithm>

namespace {

constexpr const char *kConnectionName = "bandpilot";
constexpr const char *kTableName = "contacts";
constexpr const char *kSeedVersion = "3";

struct SeedContact
{
    const char *date;
    const char *time;
    const char *call;
    const char *band;
    const char *frequency;
    const char *mode;
    const char *submode;
    const char *comment;
};

const SeedContact kSeedContacts[] = {
    {"2026-06-23", "09:18", "OH2ABC", "20 m", "14.225", "Phone", "USB", "Strong signal from Helsinki"},
    {"2026-06-23", "10:05", "K1XYZ", "15 m", "21.035", "CW", "", "Quick morning contact"},
    {"2026-06-23", "11:39", "JA7QSO", "10 m", "28.074", "Data", "FT8", "Good decode on short opening"},
    {"2026-06-23", "13:26", "DL5HAM", "40 m", "7.145", "Phone", "LSB", "Portable station"},
    {"2026-06-23", "15:50", "VK3LOG", "17 m", "18.104", "Data", "FT4", "Long path contact"},
    {"2026-06-23", "18:17", "AO-91", "2 m", "145.960", "Sat", "FM", "Satellite pass contact"}
};

const QStringList kContactFields = {
    QStringLiteral("date"),
    QStringLiteral("time"),
    QStringLiteral("call"),
    QStringLiteral("band"),
    QStringLiteral("frequency"),
    QStringLiteral("mode"),
    QStringLiteral("submode"),
    QStringLiteral("comment")
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("BandPilot");
    resize(760, 420);

    if (!initializeDatabase()) {
        QMessageBox::critical(this, "Database Error",
                              "Could not initialize the BandPilot database.");
    }

    setupModel();
    setupUi();

    m_udpReceiver = new UdpReceiver(this);
    connect(m_udpReceiver, &UdpReceiver::loggedContactReceived, this, [this](const UdpLoggedContact &contact) {
        if (addLoggedContact(contact)) {
            statusBar()->showMessage(QStringLiteral("Logged UDP contact: %1").arg(contact.call), 5000);
        }
    });

    if (!m_udpReceiver->start()) {
        statusBar()->showMessage("UDP listener failed");
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tableView && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete) {
            deleteSelectedContacts();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::initializeDatabase()
{
    QSqlDatabase database;
    if (QSqlDatabase::contains(kConnectionName)) {
        database = QSqlDatabase::database(kConnectionName);
    } else {
        database = QSqlDatabase::addDatabase("QSQLITE", kConnectionName);
    }

    const QString databasePath = QCoreApplication::applicationDirPath()
                                 + QDir::separator()
                                 + "bandpilot.sqlite";
    database.setDatabaseName(databasePath);

    if (!database.open()) {
        statusBar()->showMessage(database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (database.tables().contains(QString::fromLatin1(kTableName))) {
        QSqlRecord record = database.record(kTableName);
        QStringList existingFields;
        for (int index = 0; index < record.count(); ++index) {
            if (record.fieldName(index) != QStringLiteral("id")) {
                existingFields.append(record.fieldName(index));
            }
        }

        if (existingFields != kContactFields) {
            if (existingFields == QStringList{
                    QStringLiteral("date"),
                    QStringLiteral("time_on"),
                    QStringLiteral("time_off"),
                    QStringLiteral("call"),
                    QStringLiteral("band"),
                    QStringLiteral("frequency"),
                    QStringLiteral("mode"),
                    QStringLiteral("submode"),
                    QStringLiteral("comment")
                }) {
                if (!query.exec(QStringLiteral("ALTER TABLE contacts RENAME TO contacts_old"))
                    || !query.exec(QStringLiteral(
                        "CREATE TABLE contacts ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "date TEXT NOT NULL,"
                        "time TEXT NOT NULL,"
                        "call TEXT NOT NULL,"
                        "band TEXT NOT NULL,"
                        "frequency TEXT NOT NULL,"
                        "mode TEXT NOT NULL,"
                        "submode TEXT,"
                        "comment TEXT"
                        ")"))
                    || !query.exec(QStringLiteral(
                        "INSERT INTO contacts (id, date, time, call, band, frequency, mode, submode, comment) "
                        "SELECT id, date, COALESCE(NULLIF(time_off, ''), time_on), call, band, frequency, mode, submode, comment "
                        "FROM contacts_old"))
                    || !query.exec(QStringLiteral("DROP TABLE contacts_old"))) {
                    statusBar()->showMessage(query.lastError().text());
                    return false;
                }
            } else if (!query.exec(QStringLiteral("DROP TABLE contacts"))) {
                statusBar()->showMessage(query.lastError().text());
                return false;
            }
        }
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS contacts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "date TEXT NOT NULL,"
            "time TEXT NOT NULL,"
            "call TEXT NOT NULL,"
            "band TEXT NOT NULL,"
            "frequency TEXT NOT NULL,"
            "mode TEXT NOT NULL,"
            "submode TEXT,"
            "comment TEXT"
            ")"))) {
        statusBar()->showMessage(query.lastError().text());
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS app_metadata ("
            "key TEXT PRIMARY KEY,"
            "value TEXT NOT NULL"
            ")"))) {
        statusBar()->showMessage(query.lastError().text());
        return false;
    }

    return seedDatabase();
}

bool MainWindow::seedDatabase()
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);

    QSqlQuery metadataQuery(database);
    metadataQuery.prepare(QStringLiteral("SELECT value FROM app_metadata WHERE key = :key"));
    metadataQuery.bindValue(QStringLiteral(":key"), QStringLiteral("seed_version"));

    const bool hasSeedVersion = metadataQuery.exec() && metadataQuery.next();
    const QString seedVersion = hasSeedVersion ? metadataQuery.value(0).toString() : QString();

    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")) || !countQuery.next()) {
        statusBar()->showMessage(countQuery.lastError().text());
        return false;
    }

    if (countQuery.value(0).toInt() > 0) {
        if (seedVersion != QString::fromLatin1(kSeedVersion)) {
            QSqlQuery versionQuery(database);
            versionQuery.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO app_metadata (key, value) "
                "VALUES (:key, :value)"));
            versionQuery.bindValue(QStringLiteral(":key"), QStringLiteral("seed_version"));
            versionQuery.bindValue(QStringLiteral(":value"), QString::fromLatin1(kSeedVersion));
            if (!versionQuery.exec()) {
                statusBar()->showMessage(versionQuery.lastError().text());
                return false;
            }
        }
        return true;
    }

    QSqlQuery clearQuery(database);
    if (!clearQuery.exec(QStringLiteral("DELETE FROM contacts"))) {
        statusBar()->showMessage(clearQuery.lastError().text());
        return false;
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :comment)"));

    for (const SeedContact &contact : kSeedContacts) {
        insertQuery.bindValue(QStringLiteral(":date"), QString::fromLatin1(contact.date));
        insertQuery.bindValue(QStringLiteral(":time"), QString::fromLatin1(contact.time));
        insertQuery.bindValue(QStringLiteral(":call"), QString::fromLatin1(contact.call));
        insertQuery.bindValue(QStringLiteral(":band"), QString::fromLatin1(contact.band));
        insertQuery.bindValue(QStringLiteral(":frequency"), QString::fromLatin1(contact.frequency));
        insertQuery.bindValue(QStringLiteral(":mode"), QString::fromLatin1(contact.mode));
        insertQuery.bindValue(QStringLiteral(":submode"), QString::fromLatin1(contact.submode));
        insertQuery.bindValue(QStringLiteral(":comment"), QString::fromLatin1(contact.comment));

        if (!insertQuery.exec()) {
            statusBar()->showMessage(insertQuery.lastError().text());
            return false;
        }
    }

    QSqlQuery versionQuery(database);
    versionQuery.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_metadata (key, value) "
        "VALUES (:key, :value)"));
    versionQuery.bindValue(QStringLiteral(":key"), QStringLiteral("seed_version"));
    versionQuery.bindValue(QStringLiteral(":value"), QString::fromLatin1(kSeedVersion));

    if (!versionQuery.exec()) {
        statusBar()->showMessage(versionQuery.lastError().text());
        return false;
    }

    return true;
}

bool MainWindow::addLoggedContact(const UdpLoggedContact &contact)
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, frequency, mode, submode, comment) "
        "VALUES (:date, :time, :call, :band, :frequency, :mode, :submode, :comment)"));
    insertQuery.bindValue(QStringLiteral(":date"), contact.date.toString(Qt::ISODate));
    insertQuery.bindValue(QStringLiteral(":time"), contact.time.toString(QStringLiteral("HH:mm:ss")));
    insertQuery.bindValue(QStringLiteral(":call"), contact.call);
    insertQuery.bindValue(QStringLiteral(":band"), contact.band);
    insertQuery.bindValue(QStringLiteral(":frequency"), contact.frequency);
    insertQuery.bindValue(QStringLiteral(":mode"), contact.mode);
    insertQuery.bindValue(QStringLiteral(":submode"), contact.submode);
    insertQuery.bindValue(QStringLiteral(":comment"), contact.comment);

    if (!insertQuery.exec()) {
        statusBar()->showMessage(insertQuery.lastError().text(), 5000);
        return false;
    }

    m_model->select();
    m_tableView->resizeColumnsToContents();
    return true;
}

bool MainWindow::deleteSelectedContacts()
{
    const QModelIndexList selectedRows = m_tableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        return false;
    }

    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (const QModelIndex &index : selectedRows) {
        rows.append(index.row());
    }

    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows) {
        if (!m_model->removeRow(row)) {
            statusBar()->showMessage(m_model->lastError().text(), 5000);
            m_model->revertAll();
            return false;
        }
    }

    if (!m_model->submitAll()) {
        statusBar()->showMessage(m_model->lastError().text(), 5000);
        m_model->revertAll();
        m_model->select();
        return false;
    }

    const int deletedCount = rows.size();
    m_model->select();
    statusBar()->showMessage(QStringLiteral("Deleted %1 contact%2")
                                 .arg(deletedCount)
                                 .arg(deletedCount == 1 ? QString() : QStringLiteral("s")),
                             5000);
    return true;
}

void MainWindow::setupModel()
{
    m_model = new QSqlTableModel(this, QSqlDatabase::database(kConnectionName));
    m_model->setTable(kTableName);
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->setSort(m_model->fieldIndex("date"), Qt::AscendingOrder);
    m_model->select();

    m_model->setHeaderData(m_model->fieldIndex("date"), Qt::Horizontal, "Date");
    m_model->setHeaderData(m_model->fieldIndex("time"), Qt::Horizontal, "Time");
    m_model->setHeaderData(m_model->fieldIndex("call"), Qt::Horizontal, "Call");
    m_model->setHeaderData(m_model->fieldIndex("band"), Qt::Horizontal, "Band");
    m_model->setHeaderData(m_model->fieldIndex("frequency"), Qt::Horizontal, "Frequency");
    m_model->setHeaderData(m_model->fieldIndex("mode"), Qt::Horizontal, "Mode");
    m_model->setHeaderData(m_model->fieldIndex("submode"), Qt::Horizontal, "Submode");
    m_model->setHeaderData(m_model->fieldIndex("comment"), Qt::Horizontal, "Comment");
}

void MainWindow::setupUi()
{
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setSortingEnabled(true);
    m_tableView->installEventFilter(this);
    m_tableView->hideColumn(m_model->fieldIndex("id"));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->resizeColumnsToContents();

    setCentralWidget(m_tableView);
    statusBar()->showMessage("Database ready");
}
