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
#include <QTableView>

namespace {

constexpr const char *kConnectionName = "bandpilot";
constexpr const char *kTableName = "contacts";

struct SeedContact
{
    const char *date;
    const char *time;
    const char *call;
    const char *band;
    const char *mode;
};

const SeedContact kSeedContacts[] = {
    {"2026-06-23", "09:14", "OH2ABC", "20 m", "SSB"},
    {"2026-06-23", "10:02", "K1XYZ", "15 m", "CW"},
    {"2026-06-23", "11:37", "JA7QSO", "10 m", "FT8"},
    {"2026-06-23", "13:20", "DL5HAM", "40 m", "SSB"},
    {"2026-06-23", "15:48", "VK3LOG", "17 m", "FT4"}
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
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS contacts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "date TEXT NOT NULL,"
            "time TEXT NOT NULL,"
            "call TEXT NOT NULL,"
            "band TEXT NOT NULL,"
            "mode TEXT NOT NULL"
            ")"))) {
        statusBar()->showMessage(query.lastError().text());
        return false;
    }

    return seedDatabase();
}

bool MainWindow::seedDatabase()
{
    QSqlDatabase database = QSqlDatabase::database(kConnectionName);
    QSqlQuery countQuery(database);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM contacts")) || !countQuery.next()) {
        statusBar()->showMessage(countQuery.lastError().text());
        return false;
    }

    if (countQuery.value(0).toInt() > 0) {
        return true;
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO contacts (date, time, call, band, mode) "
        "VALUES (:date, :time, :call, :band, :mode)"));

    for (const SeedContact &contact : kSeedContacts) {
        insertQuery.bindValue(QStringLiteral(":date"), QString::fromLatin1(contact.date));
        insertQuery.bindValue(QStringLiteral(":time"), QString::fromLatin1(contact.time));
        insertQuery.bindValue(QStringLiteral(":call"), QString::fromLatin1(contact.call));
        insertQuery.bindValue(QStringLiteral(":band"), QString::fromLatin1(contact.band));
        insertQuery.bindValue(QStringLiteral(":mode"), QString::fromLatin1(contact.mode));

        if (!insertQuery.exec()) {
            statusBar()->showMessage(insertQuery.lastError().text());
            return false;
        }
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
    m_model->setHeaderData(m_model->fieldIndex("time"), Qt::Horizontal, "Time");
    m_model->setHeaderData(m_model->fieldIndex("call"), Qt::Horizontal, "Call");
    m_model->setHeaderData(m_model->fieldIndex("band"), Qt::Horizontal, "Band");
    m_model->setHeaderData(m_model->fieldIndex("mode"), Qt::Horizontal, "Mode");
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
