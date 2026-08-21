#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlRelationalDelegate>
#include <QSqlRelationalTableModel>
#include <QStatusBar>
#include <QTableView>

#include "database.h"
#include "version.h"

namespace {

// LoTW's own error responses (e.g. bad login/password) come back as an HTML
// page with HTTP 200, not a network error -- so a zero-record import isn't
// itself distinguishable from "no new QSOs" without looking at the body.
// Strips tags down to a short plain-text summary suitable for a status
// message.
QString lotwResponseMessage(const QByteArray &data)
{
    QString message = QString::fromUtf8(data);
    message.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    message = message.simplified();
    if (message.size() > 200)
        message = message.left(200) + QStringLiteral("...");
    return message;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("BandPilot %1").arg(QStringLiteral(BANDPILOT_VERSION)));
    resize(1000, 600);

    m_networkManager = new QNetworkAccessManager(this);

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

    QAction *importLotwAction = importMenu->addAction(tr("&LoTW..."));
    connect(importLotwAction, &QAction::triggered, this, &MainWindow::importLotw);

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

void MainWindow::importLotw()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import from LoTW"));

    auto *loginEdit = new QLineEdit(&dialog);
    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    auto *qslSinceEdit = new QLineEdit(QStringLiteral("2000-01-01"), &dialog);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QFormLayout(&dialog);
    layout->addRow(tr("Login:"), loginEdit);
    layout->addRow(tr("Password:"), passwordEdit);
    layout->addRow(tr("QSLs since:"), qslSinceEdit);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString login = loginEdit->text().trimmed();
    const QString password = passwordEdit->text();
    const QString qslSince = qslSinceEdit->text().trimmed();
    if (login.isEmpty() || password.isEmpty()) {
        statusBar()->showMessage(tr("LoTW login and password are required."), 8000);
        return;
    }
    if (!QDate::fromString(qslSince, Qt::ISODate).isValid()) {
        statusBar()->showMessage(tr("\"QSLs since\" must use YYYY-MM-DD format."), 8000);
        return;
    }

    QNetworkRequest request(Database::lotwReportUrl(login, password, qslSince));
    // The request URL carries the LoTW password as a query parameter;
    // refuse to let a redirect quietly downgrade it to plain HTTP.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_networkManager->get(request);
    statusBar()->showMessage(tr("Downloading LoTW report..."));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleLotwReply(reply); });
}

void MainWindow::handleLotwReply(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        statusBar()->showMessage(tr("LoTW download failed: %1").arg(errorText), 8000);
        return;
    }

    Database::AdifImportResult result;
    QString errorMessage;
    if (!Database::importLotwAdif(data, &result, &errorMessage)) {
        statusBar()->showMessage(tr("LoTW import failed: %1").arg(errorMessage), 8000);
        return;
    }

    if (result.imported == 0 && result.duplicates == 0 && result.invalid == 0) {
        const QString serverMessage = lotwResponseMessage(data);
        statusBar()->showMessage(serverMessage.isEmpty()
                                      ? tr("LoTW returned an empty response.")
                                      : tr("LoTW returned no QSO records: %1").arg(serverMessage),
                                  8000);
        return;
    }

    m_model->select();
    statusBar()->showMessage(tr("LoTW import: %1 new, %2 already logged, %3 skipped")
                                  .arg(result.imported)
                                  .arg(result.duplicates)
                                  .arg(result.invalid),
                              8000);
}
