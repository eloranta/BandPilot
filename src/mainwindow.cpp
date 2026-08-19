#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QSqlTableModel>
#include <QStatusBar>
#include <QTableView>

#include "database.h"
#include "version.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("BandPilot %1").arg(QStringLiteral(BANDPILOT_VERSION)));
    resize(1000, 600);

    setupUi();
    setupMenuBar();
    loadDatabase();
}

void MainWindow::setupUi()
{
    m_tableView = new QTableView(this);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    setCentralWidget(m_tableView);

    statusBar()->showMessage(tr("Starting up..."));
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        statusBar()->showMessage(
            tr("BandPilot %1 — ham radio QSO logger").arg(QStringLiteral(BANDPILOT_VERSION)), 5000);
    });
}

void MainWindow::loadDatabase()
{
    QString errorMessage;
    if (!Database::initialize(&errorMessage)) {
        statusBar()->showMessage(tr("Database error: %1").arg(errorMessage));
        return;
    }

    m_model = new QSqlTableModel(this, QSqlDatabase::database(Database::connectionName()));
    m_model->setTable(QStringLiteral("contacts"));
    m_model->setEditStrategy(QSqlTableModel::OnFieldChange);
    m_model->select();

    const auto setHeader = [this](const char *field, const QString &label) {
        m_model->setHeaderData(m_model->fieldIndex(QString::fromLatin1(field)), Qt::Horizontal, label);
    };
    setHeader("id", tr("ID"));
    setHeader("date", tr("Date"));
    setHeader("time", tr("Time"));
    setHeader("call", tr("Call"));
    setHeader("band", tr("Band"));
    setHeader("frequency", tr("Frequency"));
    setHeader("mode", tr("Mode"));
    setHeader("submode", tr("Submode"));
    setHeader("country", tr("Country"));
    setHeader("dxcc_entity", tr("DXCC #"));
    setHeader("deleted_entity", tr("Deleted Entity"));
    setHeader("grid", tr("Grid"));
    setHeader("rst_tx", tr("RST Tx"));
    setHeader("rst_rx", tr("RST Rx"));
    setHeader("qsl", tr("QSL"));
    setHeader("comment", tr("Comment"));

    m_tableView->setModel(m_model);
    m_tableView->resizeColumnsToContents();

    statusBar()->showMessage(
        tr("Database ready: %1 (%2 contacts)")
            .arg(Database::databaseFilePath())
            .arg(m_model->rowCount()));
}
