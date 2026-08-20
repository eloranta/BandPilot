#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QSqlRelationalDelegate>
#include <QSqlRelationalTableModel>
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

    QMenu *importMenu = fileMenu->addMenu(tr("&Import"));
    QAction *importAdifAction = importMenu->addAction(tr("&Adif..."));
    connect(importAdifAction, &QAction::triggered, this, &MainWindow::importAdif);

    fileMenu->addSeparator();

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

    m_model = new QSqlRelationalTableModel(this, QSqlDatabase::database(Database::connectionName()));
    m_model->setTable(QStringLiteral("contacts"));

    // fieldIndex("dxcc_entity") stops resolving once the relation below is active and
    // select() has run (the model renames that field after the related table's display
    // column), so capture its position now while the name still resolves.
    const int dxccEntityColumn = m_model->fieldIndex(QStringLiteral("dxcc_entity"));

    m_model->setRelation(dxccEntityColumn,
                          QSqlRelation(QStringLiteral("dxcc_entity"), QStringLiteral("entity_code"),
                                       QStringLiteral("entity")));
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
    setHeader("deleted_entity", tr("Deleted Entity"));
    setHeader("grid", tr("Grid"));
    setHeader("rst_tx", tr("RST Tx"));
    setHeader("rst_rx", tr("RST Rx"));
    setHeader("qsl", tr("QSL"));
    setHeader("comment", tr("Comment"));
    m_model->setHeaderData(dxccEntityColumn, Qt::Horizontal, tr("DXCC Entity"));

    m_tableView->setModel(m_model);
    m_tableView->setItemDelegate(new QSqlRelationalDelegate(m_tableView));
    m_tableView->resizeColumnsToContents();

    statusBar()->showMessage(
        tr("Database ready: %1 (%2 contacts)")
            .arg(Database::databaseFilePath())
            .arg(m_model->rowCount()));
}

void MainWindow::importAdif()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Import ADIF Log"), QString(), tr("ADIF files (*.adi *.adif);;All files (*)"));
    if (filePath.isEmpty())
        return;

    QString errorMessage;
    const int imported = Database::importAdif(filePath, &errorMessage);
    if (imported < 0) {
        statusBar()->showMessage(tr("ADIF import failed: %1").arg(errorMessage));
        return;
    }

    m_model->select();
    statusBar()->showMessage(tr("Imported %1 contact(s) from %2").arg(imported).arg(filePath));
}
