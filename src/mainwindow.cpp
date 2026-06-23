#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QHeaderView>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QStringList>
#include <QTableView>

namespace {

constexpr const char *kConnectionName = "bandpilot";
constexpr const char *kTableName = "contacts";
constexpr const char *kSeedVersion = "2";

struct SeedContact
{
    const char *date;
    const char *timeOn;
    const char *timeOff;
    const char *call;
    const char *band;
    const char *frequency;
    const char *mode;
    const char *submode;
    const char *comment;
};

const SeedContact kSeedContacts[] = {
    {"2026-06-23", "09:14", "09:18", "OH2ABC", "20 m", "14.225", "Phone", "USB", "Strong signal from Helsinki"},
    {"2026-06-23", "10:02", "10:05", "K1XYZ", "15 m", "21.035", "CW", "", "Quick morning contact"},
    {"2026-06-23", "11:37", "11:39", "JA7QSO", "10 m", "28.074", "Data", "FT8", "Good decode on short opening"},
    {"2026-06-23", "13:20", "13:26", "DL5HAM", "40 m", "7.145", "Phone", "LSB", "Portable station"},
    {"2026-06-23", "15:48", "15:50", "VK3LOG", "17 m", "18.104", "Data", "FT4", "Long path contact"},
    {"2026-06-23", "18:12", "18:17", "AO-91", "2 m", "145.960", "Sat", "FM", "Satellite pass contact"}
};

const QStringList kContactFields = {
    QStringLiteral("date"),
    QStringLiteral("time_on"),
    QStringLiteral("time_off"),
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

        if (existingFields != kContactFields
            && !query.exec(QStringLiteral("DROP TABLE contacts"))) {
            statusBar()->showMessage(query.lastError().text());
            return false;
        }
    }

    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS contacts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "date TEXT NOT NULL,"
            "time_on TEXT NOT NULL,"
            "time_off TEXT,"
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

    if (seedVersion == QString::fromLatin1(kSeedVersion) && countQuery.value(0).toInt() > 0) {
        return true;
    }

    QSqlQuery clearQuery(database);
    if (!clearQuery.exec(QStringLiteral("DELETE FROM contacts"))) {
        statusBar()->showMessage(clearQuery.lastError().text());
        return false;
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time_on, time_off, call, band, frequency, mode, submode, comment) "
        "VALUES (:date, :time_on, :time_off, :call, :band, :frequency, :mode, :submode, :comment)"));

    for (const SeedContact &contact : kSeedContacts) {
        insertQuery.bindValue(QStringLiteral(":date"), QString::fromLatin1(contact.date));
        insertQuery.bindValue(QStringLiteral(":time_on"), QString::fromLatin1(contact.timeOn));
        insertQuery.bindValue(QStringLiteral(":time_off"), QString::fromLatin1(contact.timeOff));
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

void MainWindow::setupModel()
{
    m_model = new QSqlTableModel(this, QSqlDatabase::database(kConnectionName));
    m_model->setTable(kTableName);
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->setSort(m_model->fieldIndex("date"), Qt::AscendingOrder);
    m_model->select();

    m_model->setHeaderData(m_model->fieldIndex("date"), Qt::Horizontal, "Date");
    m_model->setHeaderData(m_model->fieldIndex("time_on"), Qt::Horizontal, "Time On");
    m_model->setHeaderData(m_model->fieldIndex("time_off"), Qt::Horizontal, "Time Off");
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
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setSortingEnabled(true);
    m_tableView->hideColumn(m_model->fieldIndex("id"));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->resizeColumnsToContents();

    setCentralWidget(m_tableView);
    statusBar()->showMessage("Database ready");
}
